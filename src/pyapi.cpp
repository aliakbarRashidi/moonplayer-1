#include "pyapi.h"
#include "accessmanager.h"
#include "parserwebcatch.h"
#include "platform/paths.h"
#include "reslibrary.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>


bool win_debug = false;

/*****************************************
 ******** Some useful functions **********
 ****************************************/
#define RETURN_IF_ERROR(retval)  if ((retval) == nullptr){printPythonException(); return;}
#define EXIT_IF_ERROR(retval)    if ((retval) == nullptr){printPythonException(); exit(EXIT_FAILURE);}


/************************************************
 ** Define get_content() function for python **
 ************************************************/

GetUrl *geturl_obj = nullptr;
PyObject *exc_GetUrlError = nullptr;

GetUrl::GetUrl(QObject *parent) : QObject(parent)
{
    reply = nullptr;
    callbackFunc = nullptr;
    data = nullptr;
    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, SIGNAL(timeout()), this, SLOT(onTimeOut()));
    exc_GetUrlError = PyErr_NewException("moonplayer.GetUrlError", nullptr, nullptr);
}

void GetUrl::start(const char *url, PyObject *callback, PyObject *_data,
                   const QByteArray &referer, const QByteArray &postData)
{
    //save callback function
    callbackFunc = callback;
    data = _data;
    Py_IncRef(callbackFunc);
    Py_IncRef(data);
    //start request
    QNetworkRequest request = QNetworkRequest(QString::fromUtf8(url));
    if (!referer.isEmpty())
        request.setRawHeader("Referer", referer);
    if (postData.isEmpty())
        reply = access_manager->get(request);
    else
    {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        reply = access_manager->post(request, postData);
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onFinished()));
    timer->start(10000);
    // Set moonplayer.final_url in Python
    PyObject *str = PyString_FromString(url);
    PyObject_SetAttrString(apiModule, "final_url", str);
    Py_DecRef(str);
}

void GetUrl::onTimeOut()
{
    Q_ASSERT(reply);
    reply->abort();
}

void GetUrl::onFinished()
{
    timer->stop();
    //check redirection
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 301 || status == 302)
    {
        reply->deleteLater();
        QByteArray final_url = reply->rawHeader("Location");
        PyObject *str = PyString_FromString(final_url.constData());
        PyObject_SetAttrString(apiModule, "final_url", str);
        Py_DecRef(str);
        //start request
        QNetworkRequest request = QNetworkRequest(QString::fromUtf8(final_url));
        reply = access_manager->get(request);
        connect(reply, SIGNAL(finished()), this, SLOT(onFinished()));
        return;
    }
    PyObject *callback = callbackFunc;
    PyObject *_data = data;
    callbackFunc = nullptr;
    data = nullptr;
    QNetworkReply::NetworkError error = reply->error();
    QByteArray barray = reply->readAll();
    reply->deleteLater();
    if (error != QNetworkReply::NoError)
    {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errStr = QString().sprintf("Network Error: %d\n%s\n", status, reply->errorString().toUtf8().constData());
        QMessageBox::warning(nullptr, "Error", errStr);
        Py_DecRef(_data);
        Py_DecRef(callback);
        return;
    }
    PyObject *retVal = PyObject_CallFunction(callback, "sO", barray.constData(), _data);
    Py_DecRef(_data);
    Py_DecRef(callback);
    RETURN_IF_ERROR(retVal)
    Py_DecRef(retVal);
}

static PyObject *get_content(PyObject *, PyObject *args)
{
    PyObject *callback, *data;
    const char *url, *referer = nullptr;
    if (!PyArg_ParseTuple(args, "sOO|s", &url, &callback, &data, &referer))
        return nullptr;
    if (geturl_obj->hasTask())
    {
        PyErr_SetString(exc_GetUrlError, "Another task is running.");
        return nullptr;
    }
    geturl_obj->start(url, callback, data, referer, nullptr);
    Py_IncRef(Py_None);
    return Py_None;
}

static PyObject *post_content(PyObject *, PyObject *args)
{
    PyObject *callback, *data;
    const char *url, *post, *referer = nullptr;
    if (!PyArg_ParseTuple(args, "ssOO|s", &url, &post, &callback, &data, &referer))
        return nullptr;
    if (geturl_obj->hasTask())
    {
        PyErr_SetString(exc_GetUrlError, "Another task is running.");
        return nullptr;
    }
    geturl_obj->start(url, callback, data, referer, post);
    Py_IncRef(Py_None);
    return Py_None;
}

static PyObject *bind_referer(PyObject *, PyObject *args)
{
    const char *host, *url;
    if (!PyArg_ParseTuple(args, "ss", &host, &url))
        return nullptr;
    referer_table[QString::fromUtf8(host)] = url;
    return Py_None;
}

static PyObject *force_unseekable(PyObject *, PyObject *args)
{
    const char *s;
    if (!PyArg_ParseTuple(args, "s", &s))
        return nullptr;
    QString host = QString::fromUtf8(s);
    if (!unseekable_hosts.contains(host))
        unseekable_hosts.append(host);
    return Py_None;
}

/********************
 * Dialog functions *
 ********************/
static PyObject *warn(PyObject *, PyObject *args)
{
    const char *msg;
    if (!PyArg_ParseTuple(args, "s", &msg))
        return nullptr;
    QMessageBox::warning(nullptr, "Warning", QString::fromUtf8(msg));
    Py_IncRef(Py_None);
    return Py_None;
}

static PyObject *question(PyObject *, PyObject *args)
{
    const char *msg;
    if (!PyArg_ParseTuple(args, "s", &msg))
        return nullptr;
    if (QMessageBox::question(nullptr, "question", QString::fromUtf8(msg), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
    {
        Py_IncRef(Py_True);
        return Py_True;
    }
    Py_IncRef(Py_False);
    return Py_False;
}


/*******************
 ** ResLibrary    **
 *******************/
static PyObject *res_show(PyObject *, PyObject *args)
{
    PyObject *list = nullptr;
    if (!PyArg_ParseTuple(args, "O", &list))
        return nullptr;
    if (!PyList_Check(list))
        return nullptr;
    res_library->clearItem();
    int size = PyList_Size(list);
    for (int i = 0; i < size; i++)
    {
        PyObject *dict = PyList_GetItem(list, i);
        PyObject *name_obj, *pic_url_obj, *flag_obj;
        QString name, pic_url, flag;
        if (nullptr == (name_obj = PyDict_GetItemString(dict, "name")))
            return nullptr;
        if (nullptr == (flag_obj = PyDict_GetItemString(dict, "url")))
            return nullptr;
        if (nullptr == (pic_url_obj = PyDict_GetItemString(dict, "pic_url")))
            return nullptr;
        if ((name = PyString_AsQString(name_obj)).isNull())
            return nullptr;
        if ((flag = PyString_AsQString(flag_obj)).isNull())
            return nullptr;
        if ((pic_url = PyString_AsQString(pic_url_obj)).isNull())
            return nullptr;
        res_library->addItem(name, pic_url, flag);
    }
    Py_IncRef(Py_None);
    return Py_None;
}

static PyObject *show_detail(PyObject *, PyObject *args)
{
    PyObject *dict = nullptr;
    if (!PyArg_ParseTuple(args, "O", &dict))
        return nullptr;
    if (!PyDict_Check(dict))
    {
        PyErr_SetString(PyExc_TypeError, "The argument is not a dict.");
        return nullptr;
    }
    QVariantHash data = PyObject_AsQVariant(dict).toHash();
    res_library->openDetailPage(data);
    Py_IncRef(Py_None);
    return Py_None;
}

/*******************
 ** Video parsing **
 *******************/
static PyObject *finish_parsing(PyObject *, PyObject *args)
{
    PyObject *dict = nullptr;
    if (!PyArg_ParseTuple(args, "O", &dict))
        return nullptr;
    if (!PyDict_Check(dict))
    {
        PyErr_SetString(PyExc_TypeError, "The argument is not a dict.");
        return nullptr;
    }
    QVariantHash data = PyObject_AsQVariant(dict).toHash();
    parser_webcatch->onParseFinished(data);
    Py_IncRef(Py_None);
    return Py_None;
}

/*******************
 ** Define module **
 *******************/

static PyMethodDef methods[] = {
    {"download_page",    get_content,      METH_VARARGS, "Send a HTTP-GET request (Obsolete method)"},
    {"get_content",      get_content,      METH_VARARGS, "Send a HTTP-GET request"},
    {"post_content",     post_content,     METH_VARARGS, "Send a HTTP-POST request"},
    {"bind_referer",     bind_referer,     METH_VARARGS, "Bind a host with referer"},
    {"force_unseekable", force_unseekable, METH_VARARGS, "Force stream with specific host to be unseekable"},
    {"finish_parsing",   finish_parsing,   METH_VARARGS, "Finish video parsing"},
    {"warn",             warn,             METH_VARARGS, "Show warning message"},
    {"question",         question,         METH_VARARGS, "Show a question dialog"},
    {"res_show",         res_show,         METH_VARARGS, "Show resources result"},
    {"show_detail",      show_detail,      METH_VARARGS, "Show detail"},
    {nullptr, nullptr, 0, nullptr}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef moonplayerModule =
{
    PyModuleDef_HEAD_INIT,
    "moonplayer",  // m_name
    nullptr,       // m_doc
    -1,            // m_size
    methods,       // m_methods
    nullptr,       // m_slots
    nullptr,       // m_traverse
    nullptr,       // m_clear
    nullptr        // m_free
};
#endif

PyObject *apiModule = nullptr;

void initPython()
{
    //init python
    setenv("PYTHONIOENCODING", "utf-8", 1);
    Py_Initialize();
    if (!Py_IsInitialized())
    {
        qDebug("Cannot initialize python.");
        exit(-1);
    }

    //init module
    geturl_obj = new GetUrl(qApp);
#if PY_MAJOR_VERSION >= 3
    apiModule = PyModule_Create(&moonplayerModule);
    PyObject *modules = PySys_GetObject("modules");
    PyDict_SetItemString(modules, "moonplayer", apiModule);
#else
    apiModule = Py_InitModule("moonplayer", methods);
#endif

    PyModule_AddStringConstant(apiModule, "final_url", "");
    Py_IncRef(exc_GetUrlError);
    PyModule_AddObject(apiModule, "GetUrlError", exc_GetUrlError);

    // plugins' dir
    PyRun_SimpleString("import sys");
    PyRun_SimpleString(QString("sys.path.insert(0, '%1/plugins')").arg(getAppPath()).toUtf8().constData());
    PyRun_SimpleString(QString("sys.path.append('%1/plugins')").arg(getUserPath()).toUtf8().constData());
}

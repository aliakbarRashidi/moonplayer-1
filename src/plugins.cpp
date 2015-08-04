#include "plugins.h"
#include <QDir>
#include <QHash>
#include "pyapi.h"
#include "settings_plugins.h"
#ifdef Q_OS_WIN
#include "settings_player.h"
#endif

/************************
 ** Initialize plugins **
 ************************/
int n_plugins = 0;
Plugin **plugins = NULL;
QString plugins_msg;

void initPlugins()
{
    PyRun_SimpleString("import sys");
#ifdef Q_OS_WIN
    PyRun_SimpleString("sys.path.append('plugins')");
#else
    PyRun_SimpleString("import os");
    PyRun_SimpleString("sys.path.insert(0, '/usr/share/moonplayer/plugins')");
    PyRun_SimpleString("sys.path.append(os.environ['HOME'] + '/.moonplayer/plugins')");
#endif

    //load plugins
    static Plugin *array[128];
    plugins = array;
#ifdef Q_OS_WIN
    QDir pluginsDir = QDir(Settings::path);
    pluginsDir.cd("plugins");
    QStringList list = pluginsDir.entryList(QDir::Files, QDir::Name);
    foreach (QString item, list) {
        if ((item.startsWith("plugin_") || item.startsWith("res_")) && item.endsWith(".py"))
            plugins_msg += item + "\n";
    }
#else
    plugins_msg = "System plugins:\n    ";
    QDir pluginsDir = QDir("/usr/share/moonplayer/plugins");
    QStringList list = pluginsDir.entryList(QDir::Files, QDir::Name);
    foreach (QString item, list) {
        if ((item.startsWith("plugin_") || item.startsWith("res_")) && item.endsWith(".py"))
            plugins_msg += item + "\n    ";
    }
    plugins_msg += "\nPlugins installed by user:\n    ";
    pluginsDir = QDir::home();
    pluginsDir.cd(".moonplayer");
    pluginsDir.cd("plugins");
    QStringList list_users = pluginsDir.entryList(QDir::Files, QDir::Name);
    list += list_users;
    foreach (QString item, list_users) {
        if ((item.startsWith("plugin_") || item.startsWith("res_")) && item.endsWith(".py"))
            plugins_msg += item + "\n    ";
    }
#endif
    while (!list.isEmpty())
    {
        QString filename = list.takeFirst();
        if (filename.startsWith("plugin_") && filename.endsWith(".py"))
        {
            array[n_plugins] = new Plugin(filename.section('.', 0, 0));
            n_plugins++;
        }
    }
}

/**************************
 *** Hosts & name table ***
 **************************/
static QHash<QString, Plugin*> host2plugin;
static QHash<QString, Plugin*> name2plugin;

Plugin *getPluginByHost(const QString &host)
{
    return host2plugin[host];
}

Plugin *getPluginByName(const QString &name)
{
    return name2plugin[name];
}


/*********************
 *** Plugin object ***
 ********************/
Plugin::Plugin(const QString &moduleName)
{
    //load module
    module = PyImport_ImportModule(moduleName.toUtf8().constData());
    if (module == NULL)
    {
        PyErr_Print();
        exit(-1);
    }
    name = moduleName.section('_', 1);

    // Get parse() function
    parseFunc = PyObject_GetAttrString(module, "parse");
    if (parseFunc == NULL)
    {
        PyErr_Print();
        exit(-1);
    }

    //get search() function
    searchFunc = PyObject_GetAttrString(module, "search");
    if (searchFunc == NULL)
    {
        PyErr_Print();
        exit(-1);
    }

    searchAlbumFunc = PyObject_GetAttrString(module, "search_album");
    if (searchAlbumFunc == NULL)
        PyErr_Clear();

    //get hosts
    PyObject *hosts = PyObject_GetAttrString(module, "hosts");
    if (hosts)
    {
        int size = PyTuple_Size(hosts);
        if (size < 0)
        {
            PyErr_Print();
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < size; i++)
        {
            const char *str = PyString_AsString(PyTuple_GetItem(hosts, i));
            host2plugin[QString::fromUtf8(str)] = this;
        }
    }
    else
        PyErr_Clear();

    // add to name table
    name2plugin[name] = this;
}

void Plugin::parse(const char *url, bool is_down)
{
    int options = (Settings::quality == Settings::SUPER) ? OPT_QL_SUPER : (Settings::quality == Settings::HIGH) ? OPT_QL_HIGH : 0;
    if (is_down)
        options |= OPT_DOWNLOAD;
    call_py_func_vsi(parseFunc, url, options);
}

void Plugin::search(const QString &kw, int page)
{
    call_py_func_vsi(searchFunc, kw.toUtf8().constData(), page);
}

void Plugin::searchAlbum(const QString &kw, int page)
{
    Q_ASSERT(searchAlbumFunc);
    call_py_func_vsi(searchAlbumFunc, kw.toUtf8().constData(), page);
}

#include "streamget.h"
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include "platform/paths.h"

StreamGet::StreamGet(const QUrl &url, const QString &filename, QObject *parent) :
    DownloaderItem (filename, parent)
{
    args << "-y" << "-i" << url.toString() << "-c" << "copy" << "-bsf:a" << "aac_adtstoasc" << filename;
    process = nullptr;
    timer = nullptr;
    duration = 0;
}

void StreamGet::start()
{
    process = new QProcess(this);
    connect(process, SIGNAL(finished(int)), this, SLOT(onProcFinished(int)));
    process->start(ffmpegFilePath(), args);
    emit progressChanged(0, true);
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &StreamGet::readOutput);
    timer->start(1000);
}

void StreamGet::readOutput()
{
    QByteArray output = process->readAllStandardError();
    if (duration == 0) // read duration first
    {
        int i = output.indexOf("Duration: ");
        if (i != -1)
        {
            int hh = output.mid(i+10, 2).toInt();
            int mm = output.mid(i+13, 2).toInt();
            int ss = output.mid(i+16, 2).toInt();
            duration = hh * 3600 + mm * 60 + ss;
        }
    }
    else
    {
        int i = output.lastIndexOf("time=");
        if (i != -1)
        {
            int hh = output.mid(i+5, 2).toInt();
            int mm = output.mid(i+8, 2).toInt();
            int ss = output.mid(i+11, 2).toInt();
            int time = hh * 3600 + mm * 60 + ss;
            emit progressChanged(time * 100 / duration, true);
        }
    }
}

void StreamGet::pause()
{
    QMessageBox::warning(nullptr, "Error", tr("Cannot pause the download of stream medias"));
}

void StreamGet::stop()
{
    timer->stop();
    if (process)
    {
        process->write("q");
        process->waitForFinished(1000);
        if (process->state() == QProcess::Running)
            process->kill();
    }
    process->deleteLater();
    process = nullptr;
    timer->deleteLater();
    timer = nullptr;
}

StreamGet::~StreamGet()
{
    stop();
}

void StreamGet::onProcFinished(int code)
{
    timer->stop();
    if (code) // Error
    {
        QByteArray errOutput = process->readAllStandardError();
        // audio filter does not match
        if (errOutput.contains("Error initializing bitstream filter: aac_adtstoasc"))
        {
            args.removeOne("-bsf:a");
            args.removeOne("aac_adtstoasc");
            process->start(ffmpegFilePath(), args);
            timer->start(1000);
            return;
        }
        else // Other error
            qDebug("%s", errOutput.constData());
    }
    emit finished(this, code);
    process->deleteLater();
    process = nullptr;
    timer->deleteLater();
    timer = nullptr;

}

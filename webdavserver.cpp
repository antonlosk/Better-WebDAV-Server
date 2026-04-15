#include "webdavserver.h"
#include "webdavworker.h"
#include "mainwindow.h"

#include <QThread>
#include <QMetaObject>

WebDAVServer::WebDAVServer(MainWindow *mw, const QString &rootPath, QObject *parent)
    : QObject(parent), worker(nullptr), mainWindow(mw), rootPath(rootPath)
{
}

WebDAVServer::~WebDAVServer()
{
    stopServer();
}

bool WebDAVServer::startServer(quint16 port)
{
    if (worker) return true;

    worker = new WebDAVWorker(mainWindow, port, rootPath);
    worker->moveToThread(&workerThread);

    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &WebDAVWorker::appendLog, this, &WebDAVServer::appendLog);
    connect(worker, &WebDAVWorker::started, this, [this](bool success) {
        emit serverStarted(success);
    });
    connect(&workerThread, &QThread::started, worker, &WebDAVWorker::start);

    workerThread.start();
    return true;
}

void WebDAVServer::stopServer()
{
    if (!worker) return;

    QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);

    workerThread.quit();
    workerThread.wait();
    worker = nullptr;
}
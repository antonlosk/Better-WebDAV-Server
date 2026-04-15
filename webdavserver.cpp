#include "webdavserver.h"
#include "webdavworker.h"
#include "mainwindow.h"

WebDAVServer::WebDAVServer(MainWindow *mw, QObject *parent)
    : QObject(parent), worker(nullptr), mainWindow(mw)
{
    // Воркер создадим при старте
}

WebDAVServer::~WebDAVServer()
{
    stopServer();
}

bool WebDAVServer::startServer(quint16 port)
{
    if (worker) return true; // уже запущен

    worker = new WebDAVWorker(mainWindow, port);
    worker->moveToThread(&workerThread);

    // Подключаем сигналы
    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &WebDAVWorker::appendLog, this, &WebDAVServer::appendLog);
    // Запускаем сервер при старте потока
    connect(&workerThread, &QThread::started, worker, &WebDAVWorker::start);

    workerThread.start();
    return true;
}

void WebDAVServer::stopServer()
{
    if (!worker) return;

    worker->stop();
    workerThread.quit();
    workerThread.wait();
    worker = nullptr;
}
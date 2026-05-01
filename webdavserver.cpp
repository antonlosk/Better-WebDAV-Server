#include "webdavserver.h"
#include "webdavworker.h"
#include <QDir>
#include <QTcpServer>

// ─────────────────────────────────────────────────────────────────────────────
WebDavServer::WebDavServer(QObject *parent)
    : QObject(parent)
    , m_thread(new QThread(this))
    , m_worker(new WebDavWorker)
{
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    connect(m_worker, &WebDavWorker::logMessage,
            this,     &WebDavServer::logMessage,
            Qt::QueuedConnection);

    connect(m_worker, &WebDavWorker::serverStarted,
            this,     &WebDavServer::onServerStarted,
            Qt::QueuedConnection);
    connect(m_worker, &WebDavWorker::serverStartFailed,
            this,     &WebDavServer::onServerStartFailed,
            Qt::QueuedConnection);

    connect(m_worker, &WebDavWorker::serverStopped,
            this,     &WebDavServer::onServerStopped,
            Qt::QueuedConnection);

    connect(m_worker, &WebDavWorker::clientConnected,
            this,     &WebDavServer::clientConnected,
            Qt::QueuedConnection);

    connect(m_worker, &WebDavWorker::clientDisconnected,
            this,     &WebDavServer::clientDisconnected,
            Qt::QueuedConnection);

    connect(this,     &WebDavServer::_startRequested,
            m_worker, &WebDavWorker::startServer,
            Qt::QueuedConnection);

    connect(this,     &WebDavServer::_stopRequested,
            m_worker, &WebDavWorker::stopServer,
            Qt::QueuedConnection);

    m_thread->setObjectName("WebDAV-Worker");
    m_thread->start();
}

// ─────────────────────────────────────────────────────────────────────────────
WebDavServer::~WebDavServer()
{
    emit _stopRequested();
    m_thread->quit();
    m_thread->wait();
}

// ─────────────────────────────────────────────────────────────────────────────
bool WebDavServer::start(const QString &rootPath, quint16 port)
{
    if (m_running) {
        emit logMessage("Server is already running.", "WARN");
        return false;
    }
    if (m_startPending) {
        emit logMessage("Server is already starting.", "WARN");
        return false;
    }
    if (!QDir(rootPath).exists()) {
        emit logMessage("Directory does not exist: " + rootPath, "ERROR");
        emit serverStartFailed("Directory does not exist: " + rootPath);
        return false;
    }

    // Fast synchronous port probe to avoid false success.
    QTcpServer probe;
    if (!probe.listen(QHostAddress::Any, port)) {
        const QString reason = "Failed to start server: " + probe.errorString();
        emit logMessage(reason, "ERROR");
        emit serverStartFailed(reason);
        return false;
    }
    probe.close();

    m_rootPath = rootPath;
    m_port     = port;
    m_startPending = true;
    emit _startRequested(rootPath, port);
    return true;
}

void WebDavServer::stop()
{
    m_startPending = false;
    emit _stopRequested();
}

// ─────────────────────────────────────────────────────────────────────────────
bool    WebDavServer::isRunning() const { return m_running;  }
quint16 WebDavServer::port()      const { return m_port;     }
QString WebDavServer::rootPath()  const { return m_rootPath; }

// ─────────────────────────────────────────────────────────────────────────────
void WebDavServer::onServerStarted(quint16 port)
{
    m_running = true;
    m_startPending = false;
    m_port    = port;
    emit serverStarted(port);
}

void WebDavServer::onServerStartFailed(const QString &reason)
{
    m_running = false;
    m_startPending = false;
    emit serverStartFailed(reason);
}

void WebDavServer::onServerStopped()
{
    m_running = false;
    m_startPending = false;
    emit serverStopped();
}
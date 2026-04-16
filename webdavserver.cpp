#include "webdavserver.h"
#include "webdavclienthandler.h"

WebDavServer::WebDavServer(QObject *parent) : QTcpServer(parent), m_isRunning(false)
{
}

void WebDavServer::start(const QString &rootPath)
{
    m_rootPath = rootPath;
    if (!this->listen(QHostAddress::Any, 8080)) {
        emit logMessage(QString("Ошибка запуска сервера: %1").arg(this->errorString()));
        return;
    }
    m_isRunning = true;
    emit stateChanged(true);
    emit logMessage(QString("Сервер запущен на порту %1, корневая папка: %2").arg(this->serverPort()).arg(rootPath));
}

void WebDavServer::stop()
{
    this->close();
    m_isRunning = false;
    emit stateChanged(false);
}

bool WebDavServer::isRunning() const
{
    return m_isRunning;
}

void WebDavServer::incomingConnection(qintptr handle)
{
    // Создаем обработчик в новом потоке
    QThread *thread = new QThread(this);
    WebDavClientHandler *handler = new WebDavClientHandler(handle, m_rootPath);
    handler->moveToThread(thread);

    // Подключаем сигналы для управления жизненным циклом потока
    connect(thread, &QThread::started, handler, &WebDavClientHandler::run);
    connect(handler, &WebDavClientHandler::finished, thread, &QThread::quit);
    connect(handler, &WebDavClientHandler::finished, handler, &WebDavClientHandler::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    // Пробрасываем лог-сообщения
    connect(handler, &WebDavClientHandler::logMessage, this, &WebDavServer::logMessage);

    thread->start();
}
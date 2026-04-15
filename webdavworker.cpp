#include "webdavworker.h"
#include "webdavrequesthandler.h"
#include "webdavxmlbuilder.h"
#include "fileutils.h"
#include "mainwindow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QXmlStreamWriter>
#include <QMimeDatabase>
#include <QMimeType>
#include <QUrl>
#include <QRegularExpression>
#include <utility>

WebDAVWorker::WebDAVWorker(MainWindow *mw, quint16 p, QObject *parent)
    : QObject(parent), tcpServer(nullptr), mainWindow(mw), port(p), isRunning(false)
{
    requestHandler = new WebDAVRequestHandler(ROOT_PATH, this);
    connect(requestHandler, &WebDAVRequestHandler::appendLog, this, &WebDAVWorker::appendLog);
}

WebDAVWorker::~WebDAVWorker()
{
    stop();
}

void WebDAVWorker::start()
{
    if (isRunning) return;
    tcpServer = new QTcpServer(this);
    if (!tcpServer->listen(QHostAddress::Any, port)) {
        emit appendLog(QString("Failed to listen on port %1: %2").arg(port).arg(tcpServer->errorString()));
        delete tcpServer;
        tcpServer = nullptr;
        emit started(false);
        emit finished();
        return;
    }
    connect(tcpServer, &QTcpServer::newConnection, this, &WebDAVWorker::onNewConnection);
    isRunning = true;
    emit appendLog(QString("WebDAV Server: Started on port %1").arg(port));
    emit started(true);
}

void WebDAVWorker::stop()
{
    if (!isRunning) return;
    tcpServer->close();
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        QTcpSocket *socket = it.key();
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState)
            socket->abort();
        delete socket;
    }
    clients.clear();
    delete tcpServer;
    tcpServer = nullptr;
    isRunning = false;
    emit appendLog("WebDAV Server: Stopped");
    emit finished();
}

void WebDAVWorker::onNewConnection()
{
    QTcpSocket *socket = tcpServer->nextPendingConnection();
    if (!socket) return;
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket, &QTcpSocket::readyRead, this, &WebDAVWorker::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &WebDAVWorker::onDisconnected);
    ClientState state;
    clients.insert(socket, state);
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setProperty("socket", QVariant::fromValue(socket));
    connect(timer, &QTimer::timeout, this, &WebDAVWorker::onSocketTimeout);
    timer->start(300000);
    socket->setProperty("timeoutTimer", QVariant::fromValue(timer));
    emit appendLog("New client connected");
}

void WebDAVWorker::onSocketTimeout()
{
    QTimer *timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;
    QTcpSocket *socket = timer->property("socket").value<QTcpSocket*>();
    if (socket && clients.contains(socket)) {
        emit appendLog("Socket timeout, disconnecting");
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState)
            socket->abort();
    }
    // Очищаем свойство, чтобы избежать висячего указателя
    if (socket) {
        socket->setProperty("timeoutTimer", QVariant());
    }
    timer->deleteLater();
}

void WebDAVWorker::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    QTimer *timer = socket->property("timeoutTimer").value<QTimer*>();
    if (timer) {
        timer->stop();
        timer->deleteLater();
        socket->setProperty("timeoutTimer", QVariant()); // очищаем
    }
    if (clients.contains(socket)) {
        ClientState &state = clients[socket];
        // Удаляем только недокачанные файлы
        if (state.putFile && !state.uploadCompleted) {
            QString filePath = state.putFile->fileName();
            state.putFile->close();
            delete state.putFile;
            state.putFile = nullptr;
            QFile::remove(filePath);
            emit appendLog(QString("Removed incomplete PUT file: %1").arg(filePath));
        }
        if (state.uploadFile && !state.uploadCompleted) {
            QString filePath = state.uploadFile->fileName();
            state.uploadFile->close();
            delete state.uploadFile;
            state.uploadFile = nullptr;
            QFile::remove(filePath);
            emit appendLog(QString("Removed incomplete chunked PUT file: %1").arg(filePath));
        }
        // Безусловные блоки удалены — двойного удаления не будет
        clients.remove(socket);
    }
    emit appendLog("Client disconnected");
    socket->deleteLater();
}

void WebDAVWorker::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    if (!clients.contains(socket)) return;
    QTimer *timer = socket->property("timeoutTimer").value<QTimer*>();
    if (timer) timer->start(300000);

    ClientState &state = clients[socket];
    QByteArray newData = socket->readAll();
    state.buffer.append(newData);

    if (!state.headerParsed) {
        int headerEnd = state.buffer.indexOf("\r\n\r\n");
        if (headerEnd == -1) {
            headerEnd = state.buffer.indexOf("\n\n");
            if (headerEnd != -1) headerEnd += 2;
        } else {
            headerEnd += 4;
        }
        if (headerEnd == -1) {
            emit appendLog("Headers not complete yet");
            return;
        }

        QByteArray headerData = state.buffer.left(headerEnd);
        state.buffer.remove(0, headerEnd);
        state.requestHeaders = headerData;

        emit appendLog("=== Headers ===");
        for (const QByteArray &line : headerData.split('\n')) {
            emit appendLog(QString::fromLatin1(line.trimmed()));
        }
        emit appendLog("===============");

        int firstLineEnd = headerData.indexOf('\n');
        if (firstLineEnd == -1) firstLineEnd = headerData.size();

        QByteArray requestLine = headerData.left(firstLineEnd).trimmed();
        QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() < 3) {
            WebDAVXmlBuilder::sendResponse(socket, 400, "text/plain", "Bad Request");
            socket->disconnectFromHost();
            return;
        }

        state.method = QString::fromLatin1(parts[0]);
        state.path = QString::fromLatin1(parts[1]);
        state.version = QString::fromLatin1(parts[2]);

        QByteArray headersBlock = headerData.mid(firstLineEnd + 1);
        int contentLength = 0;
        bool expectContinue = false;
        bool chunked = false;
        for (const QByteArray &line : headersBlock.split('\n')) {
            QByteArray trimmed = line.trimmed();
            int colonPos = trimmed.indexOf(':');
            if (colonPos == -1) continue;
            QByteArray name = trimmed.left(colonPos).toLower();
            QByteArray value = trimmed.mid(colonPos + 1).trimmed();
            if (name == "content-length") {
                contentLength = value.toInt();
            } else if (name == "expect" && value.toLower() == "100-continue") {
                expectContinue = true;
            } else if (name == "transfer-encoding") {
                QList<QByteArray> encodings = value.split(',');
                if (!encodings.isEmpty()) {
                    QByteArray last = encodings.last().trimmed().toLower();
                    if (last == "chunked")
                        chunked = true;
                }
            } else if (name == "depth") {
                QByteArray depthVal = value.toLower();
                if (depthVal == "0") state.depth = 0;
                else if (depthVal == "1") state.depth = 1;
                else if (depthVal == "infinity") state.depth = -1;
            }
        }

        state.contentLength = contentLength;
        state.chunked = chunked;
        state.expectContinue = expectContinue;
        state.headerParsed = true;

        emit appendLog(QString("Method: %1, Path: %2, Content-Length: %3, Chunked: %4, Expect: %5, Depth: %6")
                           .arg(state.method, state.path).arg(contentLength).arg(chunked).arg(expectContinue).arg(state.depth));

        if (expectContinue && !state.sentContinue) {
            socket->write("HTTP/1.1 100 Continue\r\n\r\n");
            socket->flush();
            state.sentContinue = true;
            emit appendLog("Sent 100 Continue");
        }

        if (!chunked && contentLength == 0) {
            emit appendLog(QString("No body, handling %1 %2").arg(state.method, state.path));
            requestHandler->handleRequest(socket, state);
            if (socket->state() == QAbstractSocket::ConnectedState)
                socket->disconnectFromHost();
            return;
        }
    }

    if (state.chunked) {
        requestHandler->processChunkedBody(socket, state);
    } else {
        if (state.method == "PUT") {
            requestHandler->processContentLengthPut(socket, state);
        } else {
            if (requestHandler->isBodyTooLarge(state.contentLength)) {
                WebDAVXmlBuilder::sendResponse(socket, 413, "text/plain", "Payload Too Large");
                socket->disconnectFromHost();
                return;
            }
            if (state.buffer.size() < state.contentLength) {
                return;
            }
            QByteArray body = state.buffer.left(state.contentLength);
            state.buffer.remove(0, state.contentLength);
            emit appendLog(QString("Body received: %1 bytes for %2").arg(body.size()).arg(state.method));
            state.buffer = body;
            requestHandler->handleRequest(socket, state);
            if (socket->state() == QAbstractSocket::ConnectedState)
                socket->disconnectFromHost();
        }
    }
}
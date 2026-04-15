#include "webdavworker.h"
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

WebDAVWorker::WebDAVWorker(MainWindow *mw, quint16 p, QObject *parent)
    : QObject(parent), tcpServer(nullptr), mainWindow(mw), port(p), isRunning(false)
{
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
        emit finished();
        return;
    }
    connect(tcpServer, &QTcpServer::newConnection, this, &WebDAVWorker::onNewConnection);
    isRunning = true;
    emit appendLog(QString("WebDAV Server: Started on port %1").arg(port));
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
    }
    if (clients.contains(socket)) {
        ClientState &state = clients[socket];
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
        if (state.uploadFile) {
            state.uploadFile->close();
            delete state.uploadFile;
        }
        if (state.putFile) {
            state.putFile->close();
            delete state.putFile;
        }
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
            sendResponse(socket, 400, "text/plain", "Bad Request");
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
            } else if (name == "expect" && value.toLower().contains("100-continue")) {
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
            handleRequest(socket, state);
            if (socket->state() == QAbstractSocket::ConnectedState)
                socket->disconnectFromHost();
            return;
        }
    }

    if (state.chunked) {
        processChunkedBody(socket, state);
    } else {
        if (state.method == "PUT") {
            processContentLengthPut(socket, state);
        } else {
            if (state.contentLength > MAX_MEMORY_BUFFER) {
                sendResponse(socket, 413, "text/plain", "Payload Too Large");
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
            handleRequest(socket, state);
            if (socket->state() == QAbstractSocket::ConnectedState)
                socket->disconnectFromHost();
        }
    }
}

void WebDAVWorker::processContentLengthPut(QTcpSocket *socket, ClientState &state)
{
    if (!state.putFile) {
        QString decodedPath = urlDecode(state.path);
        QString localPath = safeLocalPath(decodedPath);
        if (localPath.isEmpty()) {
            sendResponse(socket, 403, "text/plain", "Forbidden");
            socket->disconnectFromHost();
            return;
        }
        QFileInfo fileInfo(localPath);
        QDir dir = fileInfo.dir();
        if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
            sendResponse(socket, 409, "text/plain", "Conflict");
            socket->disconnectFromHost();
            return;
        }
        state.putFile = new QFile(localPath);
        if (!state.putFile->open(QIODevice::WriteOnly)) {
            sendResponse(socket, 403, "text/plain", "Forbidden");
            delete state.putFile;
            state.putFile = nullptr;
            socket->disconnectFromHost();
            return;
        }
        state.putBytesWritten = 0;
        emit appendLog(QString("Opened file for PUT: %1").arg(localPath));
    }

    qint64 available = state.buffer.size();
    if (available > 0) {
        qint64 written = state.putFile->write(state.buffer.constData(), available);
        if (written != available) {
            QString filePath = state.putFile->fileName();
            state.putFile->close();
            delete state.putFile;
            state.putFile = nullptr;
            QFile::remove(filePath);
            sendResponse(socket, 500, "text/plain", "Write error");
            socket->disconnectFromHost();
            emit appendLog(QString("PUT write error: %1").arg(filePath));
            return;
        }
        state.putBytesWritten += written;
        emit appendLog(QString("Wrote %1 bytes to file, total %2/%3").arg(written).arg(state.putBytesWritten).arg(state.contentLength));
        state.buffer.clear();
    }

    if (state.putBytesWritten >= state.contentLength) {
        state.putFile->close();
        delete state.putFile;
        state.putFile = nullptr;
        state.uploadCompleted = true;
        sendResponse(socket, 201, "text/plain", "Created");
        emit appendLog(QString("PUT completed: %1 bytes").arg(state.contentLength));
        if (socket->state() == QAbstractSocket::ConnectedState)
            socket->disconnectFromHost();
    }
}

void WebDAVWorker::processChunkedBody(QTcpSocket *socket, ClientState &state)
{
    state.chunkBuffer.append(state.buffer);
    state.buffer.clear();

    while (true) {
        int crlfPos = state.chunkBuffer.indexOf("\r\n");
        if (crlfPos == -1) break;

        QByteArray sizeLine = state.chunkBuffer.left(crlfPos);
        int semicolonPos = sizeLine.indexOf(';');
        if (semicolonPos != -1) sizeLine = sizeLine.left(semicolonPos);
        bool ok;
        int chunkSize = sizeLine.toInt(&ok, 16);
        if (!ok) {
            emit appendLog(QString("Invalid chunk size: %1").arg(QString::fromLatin1(sizeLine)));
            sendResponse(socket, 400, "text/plain", "Bad chunk size");
            socket->disconnectFromHost();
            return;
        }

        if (chunkSize == 0) {
            state.chunkBuffer.remove(0, crlfPos + 2);
            int endOfTrailers = state.chunkBuffer.indexOf("\r\n\r\n");
            if (endOfTrailers != -1) state.chunkBuffer.remove(0, endOfTrailers + 4);
            emit appendLog("End of chunked body");

            if (state.uploadFile) {
                state.uploadFile->close();
                delete state.uploadFile;
                state.uploadFile = nullptr;
                state.uploadCompleted = true;
                sendResponse(socket, 201, "text/plain", "Created");
                emit appendLog(QString("PUT chunked completed: %1 bytes").arg(state.totalBodyWritten));
            } else {
                handleRequest(socket, state);
            }
            if (socket->state() == QAbstractSocket::ConnectedState)
                socket->disconnectFromHost();
            break;
        }

        int dataStart = crlfPos + 2;
        int dataEnd = dataStart + chunkSize;
        if (state.chunkBuffer.size() < dataEnd + 2) {
            break;
        }
        if (state.chunkBuffer.mid(dataEnd, 2) != "\r\n") {
            emit appendLog("Missing CRLF after chunk data");
            sendResponse(socket, 400, "text/plain", "Invalid chunk format");
            socket->disconnectFromHost();
            return;
        }

        QByteArray chunkData = state.chunkBuffer.mid(dataStart, chunkSize);
        state.chunkBuffer.remove(0, dataEnd + 2);

        if (state.method == "PUT") {
            if (!state.uploadFile) {
                QString decodedPath = urlDecode(state.path);
                QString localPath = safeLocalPath(decodedPath);
                if (localPath.isEmpty()) {
                    sendResponse(socket, 403, "text/plain", "Forbidden");
                    socket->disconnectFromHost();
                    return;
                }
                QFileInfo fileInfo(localPath);
                QDir dir = fileInfo.dir();
                if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
                    sendResponse(socket, 409, "text/plain", "Conflict");
                    socket->disconnectFromHost();
                    return;
                }
                state.uploadFile = new QFile(localPath);
                if (!state.uploadFile->open(QIODevice::WriteOnly)) {
                    sendResponse(socket, 403, "text/plain", "Forbidden");
                    delete state.uploadFile;
                    state.uploadFile = nullptr;
                    socket->disconnectFromHost();
                    return;
                }
                state.uploadPath = localPath;
                emit appendLog(QString("Opened file for chunked PUT: %1").arg(localPath));
            }

            qint64 written = state.uploadFile->write(chunkData);
            if (written != chunkData.size()) {
                QString filePath = state.uploadFile->fileName();
                state.uploadFile->close();
                delete state.uploadFile;
                state.uploadFile = nullptr;
                QFile::remove(filePath);
                sendResponse(socket, 500, "text/plain", "Write error");
                socket->disconnectFromHost();
                emit appendLog(QString("Chunk write error: %1").arg(filePath));
                return;
            }
            state.totalBodyWritten += written;
        } else {
            if (state.buffer.size() + chunkData.size() > MAX_MEMORY_BUFFER) {
                sendResponse(socket, 413, "text/plain", "Payload Too Large");
                socket->disconnectFromHost();
                return;
            }
            state.buffer.append(chunkData);
        }
    }
}

void WebDAVWorker::handleRequest(QTcpSocket *socket, ClientState &state)
{
    QString decodedPath = urlDecode(state.path);

    if (state.method == "PROPFIND") {
        handlePropfind(socket, state, decodedPath);
    } else if (state.method == "MKCOL") {
        handleMkcol(socket, state, decodedPath);
    } else if (state.method == "OPTIONS") {
        handleOptions(socket, state);
    } else if (state.method == "GET") {
        handleGet(socket, state, decodedPath);
    } else if (state.method == "HEAD") {
        handleHead(socket, state, decodedPath);
    } else if (state.method == "PUT") {
        handlePut(socket, state, decodedPath);
    } else if (state.method == "DELETE") {
        handleDelete(socket, state, decodedPath);
    } else if (state.method == "MOVE") {
        handleMove(socket, state);
    } else {
        sendResponse(socket, 405, "text/plain", "Method Not Allowed");
    }
    state.requestHandled = true;
}

QString WebDAVWorker::urlDecode(const QString &path) const
{
    QByteArray encoded = path.toUtf8();
    return QUrl::fromPercentEncoding(encoded);
}

QString WebDAVWorker::safeLocalPath(const QString &decodedPath) const
{
    QString cleanPath = QDir::cleanPath(ROOT_PATH + decodedPath);
    if (!cleanPath.startsWith(ROOT_PATH, Qt::CaseInsensitive))
        return QString();
    return cleanPath;
}

void WebDAVWorker::handlePropfind(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    if (state.depth == -1) {
        sendResponse(socket, 403, "text/plain", "Depth: infinity not supported");
        emit appendLog("PROPFIND with Depth: infinity rejected");
        return;
    }

    QString localPath = safeLocalPath(decodedPath);
    if (localPath.isEmpty()) {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists()) {
        sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }

    int depth = state.depth;
    QByteArray xmlBody;
    QXmlStreamWriter xml(&xmlBody);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("D:multistatus");
    xml.writeAttribute("xmlns:D", "DAV:");

    xml.writeStartElement("D:response");
    xml.writeTextElement("D:href", state.path.toUtf8());
    xml.writeStartElement("D:propstat");
    xml.writeStartElement("D:prop");
    xml.writeTextElement("D:displayname", fileInfo.fileName().isEmpty() ? "/" : fileInfo.fileName());
    if (fileInfo.isDir()) {
        xml.writeStartElement("D:resourcetype");
        xml.writeEmptyElement("D:collection");
        xml.writeEndElement();
    } else {
        xml.writeStartElement("D:resourcetype");
        xml.writeEndElement();
        xml.writeTextElement("D:getcontentlength", QString::number(fileInfo.size()));
        xml.writeTextElement("D:getlastmodified", fileInfo.lastModified().toString(Qt::RFC2822Date));
        if (fileInfo.birthTime().isValid())
            xml.writeTextElement("D:creationdate", fileInfo.birthTime().toString(Qt::ISODate));
    }
    xml.writeEndElement(); // prop
    xml.writeTextElement("D:status", "HTTP/1.1 200 OK");
    xml.writeEndElement(); // propstat
    xml.writeEndElement(); // response

    if (depth == 1 && fileInfo.isDir()) {
        QDir dir(localPath);
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            xml.writeStartElement("D:response");
            QString entryPath = state.path;
            if (!entryPath.endsWith('/')) entryPath += '/';
            entryPath += QString::fromUtf8(QUrl::toPercentEncoding(entry.fileName()));
            xml.writeTextElement("D:href", entryPath.toUtf8());

            xml.writeStartElement("D:propstat");
            xml.writeStartElement("D:prop");
            xml.writeTextElement("D:displayname", entry.fileName());
            if (entry.isDir()) {
                xml.writeStartElement("D:resourcetype");
                xml.writeEmptyElement("D:collection");
                xml.writeEndElement();
            } else {
                xml.writeStartElement("D:resourcetype");
                xml.writeEndElement();
                xml.writeTextElement("D:getcontentlength", QString::number(entry.size()));
                xml.writeTextElement("D:getlastmodified", entry.lastModified().toString(Qt::RFC2822Date));
            }
            xml.writeEndElement(); // prop
            xml.writeTextElement("D:status", "HTTP/1.1 200 OK");
            xml.writeEndElement(); // propstat
            xml.writeEndElement(); // response
        }
    }

    xml.writeEndElement(); // multistatus
    xml.writeEndDocument();

    QMap<QByteArray, QByteArray> headers;
    headers["DAV"] = "1,2";
    sendResponse(socket, 207, "application/xml; charset=utf-8", xmlBody, headers);
}

void WebDAVWorker::handleMkcol(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = safeLocalPath(decodedPath);
    if (localPath.isEmpty()) {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QDir dir;
    if (dir.mkpath(localPath)) {
        sendResponse(socket, 201, "text/plain", "Created");
        emit appendLog(QString("MKCOL created: %1").arg(localPath));
    } else {
        sendResponse(socket, 409, "text/plain", "Conflict");
        emit appendLog(QString("MKCOL failed: %1").arg(localPath));
    }
}

void WebDAVWorker::handleOptions(QTcpSocket *socket, ClientState &state)
{
    Q_UNUSED(state)
    QMap<QByteArray, QByteArray> headers;
    headers["Allow"] = "OPTIONS, PROPFIND, MKCOL, GET, HEAD, PUT, DELETE, MOVE";
    headers["DAV"] = "1,2";
    sendResponse(socket, 200, "text/plain", "", headers);
}

void WebDAVWorker::handleGet(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = safeLocalPath(decodedPath);
    if (localPath.isEmpty()) {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists() || fileInfo.isDir()) {
        sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }
    QFile *file = new QFile(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        delete file;
        return;
    }
    QByteArray mimeType = getMimeType(fileInfo.fileName());
    sendStreamResponse(socket, 200, mimeType, file);
    file->deleteLater();
}

void WebDAVWorker::handleHead(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = safeLocalPath(decodedPath);
    if (localPath.isEmpty()) {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists() || fileInfo.isDir()) {
        sendHeadersOnly(socket, 404, "text/plain", 0);
        return;
    }
    QByteArray mimeType = getMimeType(fileInfo.fileName());
    sendHeadersOnly(socket, 200, mimeType, fileInfo.size());
}

void WebDAVWorker::handlePut(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(socket)
    if (!state.chunked && state.contentLength == 0) {
        QString localPath = safeLocalPath(decodedPath);
        if (localPath.isEmpty()) {
            sendResponse(socket, 403, "text/plain", "Forbidden");
            return;
        }
        QFile file(localPath);
        if (!file.open(QIODevice::WriteOnly)) {
            sendResponse(socket, 403, "text/plain", "Forbidden");
            return;
        }
        file.close();
        sendResponse(socket, 201, "text/plain", "Created");
        emit appendLog(QString("PUT created empty file: %1").arg(localPath));
    }
}

void WebDAVWorker::handleDelete(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = safeLocalPath(decodedPath);
    if (localPath.isEmpty()) {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists()) {
        sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }
    bool success = false;
    if (fileInfo.isDir()) {
        QDir dir(localPath);
        success = dir.removeRecursively();
    } else {
        QFile file(localPath);
        success = file.remove();
    }
    if (success) {
        sendResponse(socket, 200, "text/plain", "OK");
        emit appendLog(QString("DELETE removed: %1").arg(localPath));
    } else {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        emit appendLog(QString("DELETE failed: %1").arg(localPath));
    }
}

void WebDAVWorker::handleMove(QTcpSocket *socket, ClientState &state)
{
    QByteArray headers = state.requestHeaders;
    QString destPath;
    for (const QByteArray &line : headers.split('\n')) {
        QByteArray lowerLine = line.toLower();
        if (lowerLine.startsWith("destination:")) {
            int colonPos = line.indexOf(':');
            if (colonPos != -1) {
                QByteArray value = line.mid(colonPos + 1).trimmed();
                destPath = QString::fromUtf8(value);
                break;
            }
        }
    }
    if (destPath.isEmpty()) {
        sendResponse(socket, 400, "text/plain", "Destination header missing");
        return;
    }

    QUrl destUrl(destPath);
    QString destDecoded = urlDecode(destUrl.path());
    QString srcDecoded = urlDecode(state.path);

    QString srcLocal = safeLocalPath(srcDecoded);
    QString destLocal = safeLocalPath(destDecoded);
    if (srcLocal.isEmpty() || destLocal.isEmpty()) {
        sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }

    QFileInfo srcInfo(srcLocal);
    if (!srcInfo.exists()) {
        sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }

    QFileInfo destInfo(destLocal);
    QDir destDir = destInfo.dir();
    if (!destDir.exists() && !destDir.mkpath(".")) {
        sendResponse(socket, 409, "text/plain", "Conflict: cannot create destination directory");
        return;
    }

    if (destInfo.exists()) {
        if (destInfo.isDir()) {
            QDir dir(destLocal);
            if (!dir.removeRecursively()) {
                sendResponse(socket, 403, "text/plain", "Cannot overwrite destination");
                return;
            }
        } else {
            QFile::remove(destLocal);
        }
    }

    bool success = QFile::rename(srcLocal, destLocal);
    if (!success && srcInfo.isDir()) {
        std::function<bool(const QString&, const QString&)> copyDir;
        copyDir = [&](const QString &src, const QString &dst) -> bool {
            QDir srcDir(src);
            if (!srcDir.mkpath(dst)) return false;
            QFileInfoList entries = srcDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
            for (const QFileInfo &entry : entries) {
                QString dstEntry = dst + "/" + entry.fileName();
                if (entry.isDir()) {
                    if (!copyDir(entry.filePath(), dstEntry)) return false;
                } else {
                    if (!QFile::copy(entry.filePath(), dstEntry)) return false;
                }
            }
            return true;
        };
        if (copyDir(srcLocal, destLocal)) {
            QDir(srcLocal).removeRecursively();
            success = true;
        }
    }

    if (!success) {
        sendResponse(socket, 500, "text/plain", "Move failed");
        return;
    }

    sendResponse(socket, 201, "text/plain", "Created");
    emit appendLog(QString("MOVE %1 -> %2").arg(srcLocal, destLocal));
}

QByteArray WebDAVWorker::getMimeType(const QString &fileName) const
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fileName);
    if (mime.isValid())
        return mime.name().toUtf8();
    return "application/octet-stream";
}

void WebDAVWorker::sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType,
                                const QByteArray &body, const QMap<QByteArray, QByteArray> &extraHeaders)
{
    QByteArray statusText;
    switch (statusCode) {
    case 200: statusText = "OK"; break;
    case 201: statusText = "Created"; break;
    case 207: statusText = "Multi-Status"; break;
    case 400: statusText = "Bad Request"; break;
    case 403: statusText = "Forbidden"; break;
    case 404: statusText = "Not Found"; break;
    case 405: statusText = "Method Not Allowed"; break;
    case 409: statusText = "Conflict"; break;
    case 413: statusText = "Payload Too Large"; break;
    case 500: statusText = "Internal Server Error"; break;
    default: statusText = "Unknown"; break;
    }

    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";

    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it) {
        response += it.key() + ": " + it.value() + "\r\n";
    }
    response += "\r\n";

    socket->write(response);
    socket->write(body);
    socket->flush();
    emit appendLog(QString("Response sent: %1 %2, Content-Length: %3").arg(statusCode).arg(QString::fromLatin1(statusText)).arg(body.size()));
}

void WebDAVWorker::sendStreamResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType,
                                      QFile *file, const QMap<QByteArray, QByteArray> &extraHeaders)
{
    QByteArray statusText = (statusCode == 200) ? "OK" : "Unknown";
    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(file->size()) + "\r\n";
    response += "Connection: close\r\n";
    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it) {
        response += it.key() + ": " + it.value() + "\r\n";
    }
    response += "\r\n";
    socket->write(response);

    const qint64 chunkSize = 256 * 1024;
    QByteArray buffer(chunkSize, Qt::Uninitialized);
    while (!file->atEnd()) {
        qint64 bytesRead = file->read(buffer.data(), chunkSize);
        if (bytesRead <= 0) break;
        socket->write(buffer.constData(), bytesRead);
        if (socket->state() != QAbstractSocket::ConnectedState) break;
    }
    socket->flush();
    file->close();
    emit appendLog(QString("Stream response sent: %1, size %2").arg(statusCode).arg(file->size()));
}

void WebDAVWorker::sendHeadersOnly(QTcpSocket *socket, int statusCode, const QByteArray &contentType,
                                   qint64 contentLength, const QMap<QByteArray, QByteArray> &extraHeaders)
{
    QByteArray statusText;
    switch (statusCode) {
    case 200: statusText = "OK"; break;
    case 404: statusText = "Not Found"; break;
    case 403: statusText = "Forbidden"; break;
    case 500: statusText = "Internal Server Error"; break;
    default: statusText = "Unknown"; break;
    }

    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    response += "Connection: close\r\n";
    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it) {
        response += it.key() + ": " + it.value() + "\r\n";
    }
    response += "\r\n";
    socket->write(response);
    socket->flush();
    emit appendLog(QString("HEAD response sent: %1 %2").arg(statusCode).arg(QString::fromLatin1(statusText)));
}
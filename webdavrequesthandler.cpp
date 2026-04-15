#include "webdavrequesthandler.h"
#include "webdavxmlbuilder.h"
#include "fileutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

WebDAVRequestHandler::WebDAVRequestHandler(const QString &rootPath, QObject *parent)
    : QObject(parent), ROOT_PATH(rootPath)
{
}

void WebDAVRequestHandler::handleRequest(QTcpSocket *socket, ClientState &state)
{
    QString decodedPath = FileUtils::urlDecode(state.path);

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
        WebDAVXmlBuilder::sendResponse(socket, 405, "text/plain", "Method Not Allowed");
    }
    state.requestHandled = true;
}

void WebDAVRequestHandler::processChunkedBody(QTcpSocket *socket, ClientState &state)
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
            WebDAVXmlBuilder::sendResponse(socket, 400, "text/plain", "Bad chunk size");
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
                WebDAVXmlBuilder::sendResponse(socket, 201, "text/plain", "Created");
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
            WebDAVXmlBuilder::sendResponse(socket, 400, "text/plain", "Invalid chunk format");
            socket->disconnectFromHost();
            return;
        }

        QByteArray chunkData = state.chunkBuffer.mid(dataStart, chunkSize);
        state.chunkBuffer.remove(0, dataEnd + 2);

        if (state.method == "PUT") {
            if (!state.uploadFile) {
                QString decodedPath = FileUtils::urlDecode(state.path);
                QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
                if (localPath.isEmpty()) {
                    WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
                    socket->disconnectFromHost();
                    return;
                }
                QFileInfo fileInfo(localPath);
                QDir dir = fileInfo.dir();
                if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
                    WebDAVXmlBuilder::sendResponse(socket, 409, "text/plain", "Conflict");
                    socket->disconnectFromHost();
                    return;
                }
                state.uploadFile = new QFile(localPath);
                if (!state.uploadFile->open(QIODevice::WriteOnly)) {
                    WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
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
                WebDAVXmlBuilder::sendResponse(socket, 500, "text/plain", "Write error");
                socket->disconnectFromHost();
                emit appendLog(QString("Chunk write error: %1").arg(filePath));
                return;
            }
            state.totalBodyWritten += written;
        } else {
            if (state.buffer.size() + chunkData.size() > MAX_MEMORY_BUFFER) {
                WebDAVXmlBuilder::sendResponse(socket, 413, "text/plain", "Payload Too Large");
                socket->disconnectFromHost();
                return;
            }
            state.buffer.append(chunkData);
        }
    }
}

void WebDAVRequestHandler::processContentLengthPut(QTcpSocket *socket, ClientState &state)
{
    if (!state.putFile) {
        QString decodedPath = FileUtils::urlDecode(state.path);
        QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
        if (localPath.isEmpty()) {
            WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
            socket->disconnectFromHost();
            return;
        }
        QFileInfo fileInfo(localPath);
        QDir dir = fileInfo.dir();
        if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
            WebDAVXmlBuilder::sendResponse(socket, 409, "text/plain", "Conflict");
            socket->disconnectFromHost();
            return;
        }
        state.putFile = new QFile(localPath);
        if (!state.putFile->open(QIODevice::WriteOnly)) {
            WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
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
            WebDAVXmlBuilder::sendResponse(socket, 500, "text/plain", "Write error");
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
        WebDAVXmlBuilder::sendResponse(socket, 201, "text/plain", "Created");
        emit appendLog(QString("PUT completed: %1 bytes").arg(state.contentLength));
        if (socket->state() == QAbstractSocket::ConnectedState)
            socket->disconnectFromHost();
    }
}

void WebDAVRequestHandler::handlePropfind(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    if (state.depth == -1) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Depth: infinity not supported");
        emit appendLog("PROPFIND with Depth: infinity rejected");
        return;
    }

    QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
    if (localPath.isEmpty()) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }

    QByteArray xmlBody = WebDAVXmlBuilder::buildPropfindResponse(state.path, localPath, state.depth);
    if (xmlBody.isEmpty()) {
        WebDAVXmlBuilder::sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }

    QMap<QByteArray, QByteArray> headers;
    headers["DAV"] = "1,2";
    WebDAVXmlBuilder::sendResponse(socket, 207, "application/xml; charset=utf-8", xmlBody, headers);
}

void WebDAVRequestHandler::handleMkcol(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
    if (localPath.isEmpty()) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QDir dir;
    if (dir.mkpath(localPath)) {
        WebDAVXmlBuilder::sendResponse(socket, 201, "text/plain", "Created");
        emit appendLog(QString("MKCOL created: %1").arg(localPath));
    } else {
        WebDAVXmlBuilder::sendResponse(socket, 409, "text/plain", "Conflict");
        emit appendLog(QString("MKCOL failed: %1").arg(localPath));
    }
}

void WebDAVRequestHandler::handleOptions(QTcpSocket *socket, ClientState &state)
{
    Q_UNUSED(state)
    QMap<QByteArray, QByteArray> headers;
    headers["Allow"] = "OPTIONS, PROPFIND, MKCOL, GET, HEAD, PUT, DELETE, MOVE";
    headers["DAV"] = "1,2";
    WebDAVXmlBuilder::sendResponse(socket, 200, "text/plain", "", headers);
}

void WebDAVRequestHandler::handleGet(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
    if (localPath.isEmpty()) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists() || fileInfo.isDir()) {
        WebDAVXmlBuilder::sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }
    QFile *file = new QFile(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        delete file;
        return;
    }
    QByteArray mimeType = FileUtils::getMimeType(fileInfo.fileName());
    WebDAVXmlBuilder::sendStreamResponse(socket, 200, mimeType, file);
    file->deleteLater();
}

void WebDAVRequestHandler::handleHead(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
    if (localPath.isEmpty()) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists() || fileInfo.isDir()) {
        WebDAVXmlBuilder::sendHeadersOnly(socket, 404, "text/plain", 0);
        return;
    }
    QByteArray mimeType = FileUtils::getMimeType(fileInfo.fileName());
    WebDAVXmlBuilder::sendHeadersOnly(socket, 200, mimeType, fileInfo.size());
}

void WebDAVRequestHandler::handlePut(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(socket)
    if (!state.chunked && state.contentLength == 0) {
        QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
        if (localPath.isEmpty()) {
            WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
            return;
        }
        QFile file(localPath);
        if (!file.open(QIODevice::WriteOnly)) {
            WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
            return;
        }
        file.close();
        WebDAVXmlBuilder::sendResponse(socket, 201, "text/plain", "Created");
        emit appendLog(QString("PUT created empty file: %1").arg(localPath));
    }
}

void WebDAVRequestHandler::handleDelete(QTcpSocket *socket, ClientState &state, const QString &decodedPath)
{
    Q_UNUSED(state)
    QString localPath = FileUtils::safeLocalPath(decodedPath, ROOT_PATH);
    if (localPath.isEmpty()) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists()) {
        WebDAVXmlBuilder::sendResponse(socket, 404, "text/plain", "Not Found");
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
        WebDAVXmlBuilder::sendResponse(socket, 200, "text/plain", "OK");
        emit appendLog(QString("DELETE removed: %1").arg(localPath));
    } else {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        emit appendLog(QString("DELETE failed: %1").arg(localPath));
    }
}

void WebDAVRequestHandler::handleMove(QTcpSocket *socket, ClientState &state)
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
        WebDAVXmlBuilder::sendResponse(socket, 400, "text/plain", "Destination header missing");
        return;
    }

    QUrl destUrl(destPath);
    QString destDecoded = FileUtils::urlDecode(destUrl.path());
    QString srcDecoded = FileUtils::urlDecode(state.path);

    QString srcLocal = FileUtils::safeLocalPath(srcDecoded, ROOT_PATH);
    QString destLocal = FileUtils::safeLocalPath(destDecoded, ROOT_PATH);
    if (srcLocal.isEmpty() || destLocal.isEmpty()) {
        WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Forbidden");
        return;
    }

    QFileInfo srcInfo(srcLocal);
    if (!srcInfo.exists()) {
        WebDAVXmlBuilder::sendResponse(socket, 404, "text/plain", "Not Found");
        return;
    }

    QFileInfo destInfo(destLocal);
    QDir destDir = destInfo.dir();
    if (!destDir.exists() && !destDir.mkpath(".")) {
        WebDAVXmlBuilder::sendResponse(socket, 409, "text/plain", "Conflict: cannot create destination directory");
        return;
    }

    if (destInfo.exists()) {
        if (destInfo.isDir()) {
            QDir dir(destLocal);
            if (!dir.removeRecursively()) {
                WebDAVXmlBuilder::sendResponse(socket, 403, "text/plain", "Cannot overwrite destination");
                return;
            }
        } else {
            QFile::remove(destLocal);
        }
    }

    bool success = QFile::rename(srcLocal, destLocal);
    if (!success && srcInfo.isDir()) {
        success = FileUtils::copyDirectoryRecursively(srcLocal, destLocal);
        if (success) {
            QDir(srcLocal).removeRecursively();
        }
    }

    if (!success) {
        WebDAVXmlBuilder::sendResponse(socket, 500, "text/plain", "Move failed");
        return;
    }

    WebDAVXmlBuilder::sendResponse(socket, 201, "text/plain", "Created");
    emit appendLog(QString("MOVE %1 -> %2").arg(srcLocal, destLocal));
}
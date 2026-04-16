#include "webdavclienthandler.h"
#include "filemanager.h"
#include "webdavxmlbuilder.h"

#include <QThread>
#include <QUrl>
#include <QRegularExpression>
#include <QUuid>
#include <QTimer>
#include <QMetaObject>

WebDavClientHandler::WebDavClientHandler(qintptr socketDescriptor, const QString &rootPath, QObject *parent)
    : QObject(parent)
    , m_socketDescriptor(socketDescriptor)
    , m_rootPath(rootPath)
    , m_fileManager(new FileManager(rootPath))
    , m_headersParsed(false)
    , m_contentLength(0)
    , m_keepAlive(true)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &WebDavClientHandler::onTimeout);
}

void WebDavClientHandler::run()
{
    m_socket = new QTcpSocket(this);
    if (!m_socket->setSocketDescriptor(m_socketDescriptor)) {
        emit logMessage("Ошибка создания сокета для клиента.");
        emit finished();
        return;
    }

    connect(m_socket, &QTcpSocket::readyRead, this, &WebDavClientHandler::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &WebDavClientHandler::onDisconnected);

    emit logMessage("Клиент подключился.");

    // Запускаем таймер бездействия
    m_timeoutTimer->start(KEEP_ALIVE_TIMEOUT);
}

void WebDavClientHandler::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    // Пытаемся разобрать запросы, пока буфер содержит полные HTTP-сообщения
    while (true) {
        if (!m_headersParsed) {
            int headerEnd = m_buffer.indexOf("\r\n\r\n");
            if (headerEnd == -1)
                return; // ждём остаток заголовков

            // Разбираем первую строку
            int firstLineEnd = m_buffer.indexOf("\r\n");
            QByteArray firstLine = m_buffer.left(firstLineEnd);
            QList<QByteArray> parts = firstLine.split(' ');
            if (parts.size() < 3) {
                sendResponse(400, "Bad Request");
                return;
            }

            m_method = parts[0];
            QString rawPath = QUrl::fromPercentEncoding(parts[1]);
            m_path = rawPath;
            if (m_path.startsWith('/'))
                m_path = m_path.mid(1);
            if (m_path.isEmpty())
                m_path = "";

            m_requestHeaders = m_buffer.left(headerEnd);

            // Проверяем заголовок Connection
            if (m_requestHeaders.toLower().contains("connection: close")) {
                m_keepAlive = false;
            } else {
                m_keepAlive = true;
            }

            // Ищем Content-Length
            int clPos = m_requestHeaders.indexOf("Content-Length:");
            if (clPos != -1) {
                int clEnd = m_requestHeaders.indexOf("\r\n", clPos);
                QByteArray clLine = m_requestHeaders.mid(clPos, clEnd - clPos);
                m_contentLength = clLine.mid(15).trimmed().toInt();
            }

            m_headersParsed = true;
        }

        // Проверяем, всё ли тело запроса получено
        int headerEnd = m_buffer.indexOf("\r\n\r\n");
        int totalExpected = headerEnd + 4 + m_contentLength;
        if (m_buffer.size() < totalExpected)
            return; // ждём остаток тела

        // Полный запрос получен, останавливаем таймер на время обработки
        m_timeoutTimer->stop();

        // Обрабатываем текущий запрос
        parseRequest();

        // После обработки удаляем обработанный запрос из буфера
        if (m_keepAlive) {
            // Сбрасываем состояние, но сохраняем оставшиеся данные
            resetForNextRequest();
            // Перезапускаем таймер
            m_timeoutTimer->start(KEEP_ALIVE_TIMEOUT);
        } else {
            // Если Keep-Alive не поддерживается, закрываем соединение
            m_socket->disconnectFromHost();
            return;
        }
    }
}

void WebDavClientHandler::parseRequest()
{
    emit logMessage(QString("Запрос: %1 %2").arg(QString(m_method), m_path));

    // Извлекаем тело запроса (если есть)
    QByteArray body;
    if (hasRequestBody()) {
        body = getRequestBody();
    }

    // Извлекаем заголовок Destination для COPY/MOVE
    QString destination;
    int destPos = m_requestHeaders.indexOf("Destination:");
    if (destPos != -1) {
        int destEnd = m_requestHeaders.indexOf("\r\n", destPos);
        QByteArray destLine = m_requestHeaders.mid(destPos, destEnd - destPos);
        destination = QUrl::fromPercentEncoding(destLine.mid(12).trimmed());
    }

    // Маршрутизация по методам
    if (m_method == "OPTIONS") {
        handleOptions(m_path);
    } else if (m_method == "PROPFIND") {
        handlePropfind(m_path);
    } else if (m_method == "GET") {
        handleGet(m_path);
    } else if (m_method == "PUT") {
        handlePut(m_path, body);
    } else if (m_method == "DELETE") {
        handleDelete(m_path);
    } else if (m_method == "MKCOL") {
        handleMkcol(m_path);
    } else if (m_method == "COPY") {
        handleCopy(m_path, destination);
    } else if (m_method == "MOVE") {
        handleMove(m_path, destination);
    } else if (m_method == "LOCK") {
        handleLock(m_path);
    } else if (m_method == "UNLOCK") {
        handleUnlock(m_path);
    } else {
        sendResponse(501, "Not Implemented");
    }
}

void WebDavClientHandler::sendResponse(int statusCode, const QByteArray &body, const QByteArray &extraHeaders)
{
    QByteArray response;
    QTextStream stream(&response);
    stream << "HTTP/1.1 " << statusCode << " \r\n";
    stream << "Content-Type: application/xml; charset=\"utf-8\"\r\n";
    stream << "Content-Length: " << body.size() << "\r\n";
    stream << "Connection: " << (m_keepAlive ? "Keep-Alive" : "close") << "\r\n";
    if (!extraHeaders.isEmpty()) {
        stream << extraHeaders;
        if (!extraHeaders.endsWith("\r\n"))
            stream << "\r\n";
    }
    stream << "\r\n";
    stream.flush();

    m_socket->write(response);
    if (!body.isEmpty())
        m_socket->write(body);
    m_socket->flush();
}

void WebDavClientHandler::resetForNextRequest()
{
    // Вычисляем, сколько байт было потреблено текущим запросом
    int headerEnd = m_buffer.indexOf("\r\n\r\n");
    if (headerEnd != -1) {
        int totalConsumed = headerEnd + 4 + m_contentLength;
        if (totalConsumed > 0 && totalConsumed <= m_buffer.size()) {
            m_buffer.remove(0, totalConsumed);
        }
    }

    // Сбрасываем состояние парсинга
    m_headersParsed = false;
    m_contentLength = 0;
    m_method.clear();
    m_path.clear();
    m_requestHeaders.clear();
    // m_keepAlive остаётся как было установлено из текущего запроса
}

bool WebDavClientHandler::hasRequestBody() const
{
    return m_contentLength > 0;
}

QByteArray WebDavClientHandler::getRequestBody() const
{
    int headerEnd = m_buffer.indexOf("\r\n\r\n");
    if (headerEnd == -1)
        return QByteArray();
    return m_buffer.mid(headerEnd + 4, m_contentLength);
}

// ----------------------- Обработчики методов -----------------------

void WebDavClientHandler::handleOptions(const QString &path)
{
    Q_UNUSED(path)
    QByteArray body = WebDavXmlBuilder::buildOptionsResponse();
    QByteArray extraHeaders = "DAV: 1, 2, 3\r\n";
    sendResponse(200, body, extraHeaders);
}

void WebDavClientHandler::handlePropfind(const QString &path)
{
    QFileInfo info = m_fileManager->getFileInfo(path);
    if (!info.exists() && !path.isEmpty()) {
        QByteArray errorBody = WebDavXmlBuilder::buildErrorResponse(path, 404, "Resource not found");
        sendResponse(404, errorBody);
        return;
    }

    // Для коллекции получаем список дочерних элементов
    QList<QFileInfo> children;
    if (info.isDir()) {
        children = m_fileManager->getDirectoryContents(path);
    }

    QString mime = info.isDir() ? "httpd/unix-directory" : m_fileManager->mimeType(path);
    QByteArray responseBody = WebDavXmlBuilder::buildPropfindResponse(
        path, info, children, mime);
    sendResponse(207, responseBody);
}

void WebDavClientHandler::handleGet(const QString &path)
{
    QFileInfo info = m_fileManager->getFileInfo(path);
    if (!info.exists()) {
        sendResponse(404);
        return;
    }

    if (info.isDir()) {
        // Для коллекции возвращаем ошибку, GET для папок не поддерживаем
        sendResponse(403);
        return;
    }

    // Читаем весь файл
    QByteArray content = m_fileManager->readFile(path);
    qint64 totalSize = content.size();

    // Проверяем заголовок Range
    QByteArray rangeHeader;
    int rangePos = m_requestHeaders.indexOf("Range:");
    if (rangePos != -1) {
        int rangeEnd = m_requestHeaders.indexOf("\r\n", rangePos);
        rangeHeader = m_requestHeaders.mid(rangePos, rangeEnd - rangePos).mid(6).trimmed();
    }

    if (rangeHeader.isEmpty()) {
        // Обычный GET
        QString mime = m_fileManager->mimeType(path);
        if (mime.isEmpty()) mime = "application/octet-stream";

        QByteArray response;
        QTextStream stream(&response);
        stream << "HTTP/1.1 200 OK\r\n";
        stream << "Content-Type: " << mime << "\r\n";
        stream << "Content-Length: " << totalSize << "\r\n";
        stream << "Accept-Ranges: bytes\r\n";
        stream << "Connection: " << (m_keepAlive ? "Keep-Alive" : "close") << "\r\n";
        stream << "\r\n";
        stream.flush();

        m_socket->write(response);
        m_socket->write(content);
        m_socket->flush();
    } else {
        // Обработка Range
        if (rangeHeader.startsWith("bytes=")) {
            QByteArray rangeSpec = rangeHeader.mid(6);
            QList<QByteArray> ranges = rangeSpec.split(',');
            // Берём только первый диапазон (мультидиапазонные запросы не поддерживаем)
            if (!ranges.isEmpty()) {
                QByteArray firstRange = ranges[0].trimmed();
                qint64 start = -1, end = -1;
                int dashPos = firstRange.indexOf('-');
                if (dashPos != -1) {
                    QByteArray startBytes = firstRange.left(dashPos);
                    QByteArray endBytes = firstRange.mid(dashPos + 1);
                    if (!startBytes.isEmpty()) {
                        start = startBytes.toLongLong();
                    }
                    if (!endBytes.isEmpty()) {
                        end = endBytes.toLongLong();
                    }
                }

                // Корректировка границ
                if (start < 0) start = 0;
                if (end < 0 || end >= totalSize) end = totalSize - 1;
                if (start > end) {
                    // Некорректный диапазон — отправляем 416 с заголовком Content-Range
                    QByteArray response;
                    QTextStream stream(&response);
                    stream << "HTTP/1.1 416 Requested Range Not Satisfiable\r\n";
                    stream << "Content-Range: bytes */" << totalSize << "\r\n";
                    stream << "Content-Length: 0\r\n";
                    stream << "Connection: " << (m_keepAlive ? "Keep-Alive" : "close") << "\r\n";
                    stream << "\r\n";
                    stream.flush();

                    m_socket->write(response);
                    m_socket->flush();
                    return;
                }

                qint64 length = end - start + 1;
                QByteArray partialContent = content.mid(static_cast<int>(start), static_cast<int>(length));

                QString mime = m_fileManager->mimeType(path);
                if (mime.isEmpty()) mime = "application/octet-stream";

                QByteArray response;
                QTextStream stream(&response);
                stream << "HTTP/1.1 206 Partial Content\r\n";
                stream << "Content-Type: " << mime << "\r\n";
                stream << "Content-Length: " << length << "\r\n";
                stream << "Content-Range: bytes " << start << "-" << end << "/" << totalSize << "\r\n";
                stream << "Accept-Ranges: bytes\r\n";
                stream << "Connection: " << (m_keepAlive ? "Keep-Alive" : "close") << "\r\n";
                stream << "\r\n";
                stream.flush();

                m_socket->write(response);
                m_socket->write(partialContent);
                m_socket->flush();
            } else {
                // Пустой список диапазонов — 416
                QByteArray response;
                QTextStream stream(&response);
                stream << "HTTP/1.1 416 Requested Range Not Satisfiable\r\n";
                stream << "Content-Range: bytes */" << totalSize << "\r\n";
                stream << "Content-Length: 0\r\n";
                stream << "Connection: " << (m_keepAlive ? "Keep-Alive" : "close") << "\r\n";
                stream << "\r\n";
                stream.flush();

                m_socket->write(response);
                m_socket->flush();
                return;
            }
        } else {
            // Неподдерживаемый формат Range
            sendResponse(400, "Invalid Range header");
            return;
        }
    }

    // После отправки ответа в режиме Keep-Alive состояние сбросится в resetForNextRequest
    // (вызывается в onReadyRead после parseRequest)
}

void WebDavClientHandler::handlePut(const QString &path, const QByteArray &body)
{
    if (m_fileManager->writeFile(path, body)) {
        sendResponse(201); // Created
    } else {
        sendResponse(403); // Forbidden
    }
}

void WebDavClientHandler::handleDelete(const QString &path)
{
    if (m_fileManager->deleteResource(path)) {
        sendResponse(204); // No Content
    } else {
        sendResponse(404);
    }
}

void WebDavClientHandler::handleMkcol(const QString &path)
{
    QFileInfo info = m_fileManager->getFileInfo(path);
    if (info.exists()) {
        sendResponse(405); // Method Not Allowed
    } else {
        if (m_fileManager->createDirectory(path)) {
            sendResponse(201);
        } else {
            sendResponse(409); // Conflict
        }
    }
}

void WebDavClientHandler::handleCopy(const QString &sourcePath, const QString &destination)
{
    if (destination.isEmpty()) {
        sendResponse(400, "Destination header required");
    } else {
        if (m_fileManager->copyResource(sourcePath, destination)) {
            sendResponse(201); // Created
        } else {
            sendResponse(403);
        }
    }
}

void WebDavClientHandler::handleMove(const QString &sourcePath, const QString &destination)
{
    if (destination.isEmpty()) {
        sendResponse(400, "Destination header required");
    } else {
        if (m_fileManager->moveResource(sourcePath, destination)) {
            sendResponse(201);
        } else {
            sendResponse(403);
        }
    }
}

void WebDavClientHandler::handleLock(const QString &path)
{
    // Простейшая реализация: всегда разрешаем блокировку и генерируем фиктивный токен
    QFileInfo info = m_fileManager->getFileInfo(path);
    if (!info.exists() && !path.isEmpty()) {
        QByteArray errorBody = WebDavXmlBuilder::buildErrorResponse(path, 404, "Resource not found");
        sendResponse(404, errorBody);
        return;
    }

    QString lockToken = "urn:uuid:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray body = WebDavXmlBuilder::buildLockResponse(path.isEmpty() ? "/" : path, lockToken);
    QByteArray extraHeaders = "Lock-Token: <" + lockToken.toUtf8() + ">\r\n";
    sendResponse(200, body, extraHeaders);
}

void WebDavClientHandler::handleUnlock(const QString &path)
{
    Q_UNUSED(path)
    // В упрощённой реализации просто подтверждаем снятие блокировки
    sendResponse(204);
}

void WebDavClientHandler::onDisconnected()
{
    emit logMessage("Клиент отключился.");
    m_timeoutTimer->stop();
    emit finished();
}

void WebDavClientHandler::onTimeout()
{
    emit logMessage("Таймаут Keep-Alive, закрываем соединение.");
    m_socket->disconnectFromHost();
}
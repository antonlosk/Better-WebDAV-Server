#include "webdavworker.h"
#include "davhandlers.h"
#include "httputils.h"
#include "filestreamer.h"

#include <QTcpSocket>
#include <QDir>
#include <QUrl>

namespace {
constexpr qint64 MAX_HEADER_BYTES      = 64 * 1024;
constexpr qint64 MAX_REQUEST_BODY_BYTES = 256 * 1024 * 1024;
constexpr int    MAX_QUEUED_REQUESTS   = 256;
}

// ─────────────────────────────────────────────────────────────────────────────
WebDavWorker::WebDavWorker(QObject *parent)
    : QObject(parent)
{}

WebDavWorker::~WebDavWorker()
{
    stopServer();
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::startServer(const QString &rootPath, quint16 port)
{
    if (m_running) {
        emit logMessage("Server is already running.", "WARN");
        return;
    }
    if (!QDir(rootPath).exists()) {
        const QString reason = "Directory does not exist: " + rootPath;
        emit logMessage(reason, "ERROR");
        emit serverStartFailed(reason);
        return;
    }

    QString rp = QDir::toNativeSeparators(QDir(rootPath).absolutePath());
    if (!rp.endsWith(QDir::separator())) rp += QDir::separator();
    m_rootPath = rp;
    m_port     = port;

    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection,
            this, &WebDavWorker::onNewConnection);

    if (!m_tcpServer->listen(QHostAddress::Any, m_port)) {
        const QString reason = "Failed to start server: " +
                               m_tcpServer->errorString();
        emit logMessage(reason, "ERROR");
        emit serverStartFailed(reason);
        delete m_tcpServer;
        m_tcpServer = nullptr;
        return;
    }

    m_running = true;
    emit logMessage(QString("Server started. Root: %1  Port: %2")
                        .arg(m_rootPath).arg(m_port), "INFO");
    emit serverStarted(m_port);
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::stopServer()
{
    if (!m_running) return;

    if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        it.key()->blockSignals(true);
        it.key()->abort();
        it.key()->deleteLater();
        delete it.value();
    }
    m_clients.clear();
    m_streamerToSocket.clear();

    m_running = false;
    emit logMessage("Server stopped.", "INFO");
    emit serverStopped();
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::onNewConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket *socket = m_tcpServer->nextPendingConnection();
        m_clients[socket]  = new ClientState();

        emit clientConnected(socket->peerAddress().toString());
        emit logMessage(QString("[+] Client: %1:%2")
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()), "INFO");

        connect(socket, &QTcpSocket::readyRead,
                this, &WebDavWorker::onClientReadyRead);
        connect(socket, &QTcpSocket::disconnected,
                this, &WebDavWorker::onClientDisconnected);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::onClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    emit logMessage(QString("[-] Client: %1:%2")
                        .arg(socket->peerAddress().toString())
                        .arg(socket->peerPort()), "INFO");
    emit clientDisconnected(socket->peerAddress().toString());

    for (auto it = m_streamerToSocket.begin();
         it != m_streamerToSocket.end(); )
    {
        if (it.value() == socket)
            it = m_streamerToSocket.erase(it);
        else
            ++it;
    }

    if (m_clients.contains(socket))
        delete m_clients.take(socket);

    socket->deleteLater();
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::onClientReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !m_clients.contains(socket)) return;

    m_clients[socket]->buffer += socket->readAll();

    parseIncoming(socket);

    if (m_clients.contains(socket))
        dispatchNext(socket);
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::onStreamFinished()
{
    QObject *streamer = sender();
    if (!streamer) return;

    QTcpSocket *socket = m_streamerToSocket.take(streamer);
    if (!socket || !m_clients.contains(socket)) return;

    m_clients[socket]->streaming = false;

    dispatchNext(socket);
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::parseIncoming(QTcpSocket *socket)
{
    if (!m_clients.contains(socket)) return;
    ClientState *st = m_clients[socket];
    auto rejectRequest = [&](int code, const QString &text) {
        HttpUtils::sendError(socket, code, text, false);
        socket->disconnectFromHost();
    };

    while (true) {

        // ── Phase 1: Headers ──────────────────────────────────────────────────
        if (st->state == WaitingHeaders) {
            int sepIdx = st->buffer.indexOf("\r\n\r\n"), sepLen = 4;
            if (sepIdx < 0) {
                sepIdx = st->buffer.indexOf("\n\n");
                sepLen = 2;
            }
            if (sepIdx < 0) {
                if ((qint64)st->buffer.size() > MAX_HEADER_BYTES) {
                    rejectRequest(431, "Request Header Fields Too Large");
                }
                return;
            }

            if (sepIdx > MAX_HEADER_BYTES) {
                rejectRequest(431, "Request Header Fields Too Large");
                return;
            }

            QByteArray headerBlock = st->buffer.left(sepIdx);
            st->buffer = st->buffer.mid(sepIdx + sepLen);

            int firstLF = headerBlock.indexOf('\n');
            QByteArray reqLine = firstLF >= 0
                                     ? headerBlock.left(firstLF).trimmed()
                                     : headerBlock.trimmed();

            QList<QByteArray> partsRaw = reqLine.split(' ');
            QList<QByteArray> parts;
            parts.reserve(partsRaw.size());
            for (const QByteArray &p : partsRaw) {
                if (!p.trimmed().isEmpty()) parts.append(p.trimmed());
            }

            if (parts.size() != 3) {
                rejectRequest(400, "Bad Request");
                return;
            }

            st->method  = QString::fromUtf8(parts[0]).toUpper().trimmed();
            QString target = QUrl::fromPercentEncoding(parts[1].trimmed());
            if (target == "*") {
                st->path = "*";
            } else {
                QUrl targetUrl(target);
                QString parsedPath;

                if (targetUrl.isValid() && !targetUrl.scheme().isEmpty()) {
                    parsedPath = targetUrl.path(QUrl::FullyDecoded);
                } else {
                    int cut = target.size();
                    int qPos = target.indexOf('?');
                    int hPos = target.indexOf('#');
                    if (qPos >= 0) cut = qMin(cut, qPos);
                    if (hPos >= 0) cut = qMin(cut, hPos);
                    parsedPath = target.left(cut);
                }

                if (parsedPath.isEmpty()) parsedPath = "/";
                if (!parsedPath.startsWith('/')) parsedPath.prepend('/');
                st->path = parsedPath;
            }
            st->version = QString::fromUtf8(parts[2]).trimmed().toUpper();
            if (st->version != "HTTP/1.1" && st->version != "HTTP/1.0") {
                rejectRequest(505, "HTTP Version Not Supported");
                return;
            }

            st->headers.clear();
            st->expectContinue  = false;
            st->chunked         = false;
            st->chunkedComplete = false;
            st->chunkedParseError = false;
            st->chunkedTooLarge = false;

            for (const QByteArray &line : headerBlock.split('\n')) {
                QByteArray l = line.trimmed();
                if (l.isEmpty()) continue;
                int col = l.indexOf(':');
                if (col <= 0) continue;
                QString key = QString::fromUtf8(l.left(col)).trimmed().toLower();
                QString val = QString::fromUtf8(l.mid(col + 1)).trimmed();
                st->headers[key] = val;
            }

            QString te = st->headers.value("transfer-encoding","").toLower();
            const QString clHeader = st->headers.value("content-length", "").trimmed();
            if (te.contains("chunked")) {
                if (!clHeader.isEmpty()) {
                    rejectRequest(400, "Bad Request");
                    return;
                }
                st->chunked       = true;
                st->contentLength = -1;
            } else {
                st->chunked       = false;
                st->contentLength = 0;
                if (!clHeader.isEmpty()) {
                    bool ok = false;
                    qint64 cl = clHeader.toLongLong(&ok);
                    if (!ok || cl < 0) {
                        rejectRequest(400, "Bad Request");
                        return;
                    }
                    if (cl > MAX_REQUEST_BODY_BYTES) {
                        rejectRequest(413, "Payload Too Large");
                        return;
                    }
                    st->contentLength = cl;
                }
            }

            st->body.clear();

            const QString expectHeader =
                st->headers.value("expect", "").trimmed().toLower();
            if (!expectHeader.isEmpty() && expectHeader != "100-continue") {
                rejectRequest(417, "Expectation Failed");
                return;
            }
            if (expectHeader == "100-continue") {
                socket->write("HTTP/1.1 100 Continue\r\n\r\n");
                socket->flush();
            }

            st->state = WaitingBody;

            emit logMessage(
                QString(">> %1 %2  [%3]")
                    .arg(st->method, st->path,
                         st->chunked
                             ? "chunked"
                             : QString("%1 B").arg(st->contentLength)),
                "REQ");
        }

        // ── Phase 2: Body ─────────────────────────────────────────────────────
        if (st->state == WaitingBody) {

            if (st->chunked) {
                if (!decodeChunked(st)) {
                    if (st->chunkedTooLarge) {
                        rejectRequest(413, "Payload Too Large");
                    } else if (st->chunkedParseError) {
                        rejectRequest(400, "Bad Request");
                    }
                    return;
                }
                emit logMessage(
                    QString("   Chunked complete: %1 bytes")
                        .arg(st->body.size()), "INFO");
            } else {
                qint64 need = st->contentLength - (qint64)st->body.size();
                if (need > 0 && !st->buffer.isEmpty()) {
                    qint64 take = qMin(need, (qint64)st->buffer.size());
                    st->body   += st->buffer.left((int)take);
                    st->buffer  = st->buffer.mid((int)take);
                }
                if ((qint64)st->body.size() < st->contentLength) return;
            }

            if (st->requestQueue.size() >= MAX_QUEUED_REQUESTS) {
                rejectRequest(429, "Too Many Requests");
                return;
            }

            HttpRequest req;
            req.method  = st->method;
            req.path    = st->path;
            req.version = st->version;
            req.headers = st->headers;
            req.body    = st->body;

            st->requestQueue.enqueue(req);

            st->state           = WaitingHeaders;
            st->method.clear();
            st->path.clear();
            st->version.clear();
            st->headers.clear();
            st->contentLength   = 0;
            st->body.clear();
            st->expectContinue  = false;
            st->chunked         = false;
            st->chunkedComplete = false;
            st->chunkedParseError = false;
            st->chunkedTooLarge = false;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::dispatchNext(QTcpSocket *socket)
{
    if (!m_clients.contains(socket)) return;
    ClientState *st = m_clients[socket];

    while (!st->requestQueue.isEmpty() && !st->streaming) {
        HttpRequest req = st->requestQueue.dequeue();
        executeRequest(socket, req);
        if (!m_clients.contains(socket)) return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WebDavWorker::executeRequest(QTcpSocket *socket, const HttpRequest &req)
{
    using namespace DavHandlers;
    const QString &rp = m_rootPath;

    if (req.method == "GET") {
        FileStreamer *streamer = handleGet(socket, req, rp);
        if (streamer && m_clients.contains(socket)) {
            m_clients[socket]->streaming = true;
            m_streamerToSocket[streamer] = socket;

            connect(streamer, &FileStreamer::finished,
                    this,     &WebDavWorker::onStreamFinished,
                    Qt::UniqueConnection);
            connect(streamer, &FileStreamer::failed,
                    this,     &WebDavWorker::onStreamFinished,
                    Qt::UniqueConnection);
        }
        return;
    }

    if      (req.method == "OPTIONS" ) handleOptions (socket, req, rp);
    else if (req.method == "HEAD"    ) handleHead    (socket, req, rp);
    else if (req.method == "PUT"     ) handlePut     (socket, req, rp);
    else if (req.method == "DELETE"  ) handleDelete  (socket, req, rp);
    else if (req.method == "MKCOL"   ) handleMkcol   (socket, req, rp);
    else if (req.method == "PROPFIND") handlePropfind(socket, req, rp);
    else if (req.method == "MOVE"    ) handleMove    (socket, req, rp);
    else if (req.method == "COPY"    ) handleCopy    (socket, req, rp);
    else
        HttpUtils::sendError(socket, 501, "Not Implemented",
                             isKeepAlive(req));
}

// ─────────────────────────────────────────────────────────────────────────────
bool WebDavWorker::decodeChunked(ClientState *st)
{
    st->chunkedParseError = false;
    st->chunkedTooLarge = false;

    auto findLineEnd = [](const QByteArray &buf, int from, int &eolLen) -> int {
        int crlf = buf.indexOf("\r\n", from);
        int lf   = buf.indexOf('\n', from);
        if (crlf >= 0 && (lf < 0 || crlf <= lf)) {
            eolLen = 2;
            return crlf;
        }
        if (lf >= 0) {
            eolLen = 1;
            return lf;
        }
        eolLen = 0;
        return -1;
    };

    while (true) {
        int sizeLineEol = 0;
        int crlfIdx = findLineEnd(st->buffer, 0, sizeLineEol);
        if (crlfIdx < 0) return false;

        QByteArray sizeLine = st->buffer.left(crlfIdx);
        int semiIdx = sizeLine.indexOf(';');
        if (semiIdx >= 0) sizeLine = sizeLine.left(semiIdx);
        sizeLine = sizeLine.trimmed();

        if (sizeLine.isEmpty()) {
            st->chunkedParseError = true;
            return false;
        }

        bool ok = false;
        qint64 chunkSize = sizeLine.toLongLong(&ok, 16);
        if (!ok) {
            emit logMessage(
                QString("Chunked: invalid size '%1'")
                    .arg(QString::fromUtf8(sizeLine)), "WARN");
            st->chunkedParseError = true;
            return false;
        }

        if (chunkSize == 0) {
            st->buffer = st->buffer.mid(crlfIdx + sizeLineEol);
            while (true) {
                int tailEol = 0;
                int end = findLineEnd(st->buffer, 0, tailEol);
                if (end < 0) return false;
                if (end == 0) { st->buffer = st->buffer.mid(tailEol); break; }
                st->buffer = st->buffer.mid(end + tailEol);
            }
            st->chunkedComplete = true;
            return true;
        }

        qint64 dataStart = crlfIdx + sizeLineEol;
        if ((qint64)st->buffer.size() < dataStart + chunkSize) return false;

        int chunkEol = 0;
        int chunkEolPos = findLineEnd(st->buffer, (int)(dataStart + chunkSize), chunkEol);
        if (chunkEolPos != dataStart + chunkSize) {
            st->chunkedParseError = true;
            return false;
        }

        qint64 needed = dataStart + chunkSize + chunkEol;
        if ((qint64)st->buffer.size() < needed) return false;

        st->body += st->buffer.mid((int)dataStart, (int)chunkSize);
        if ((qint64)st->body.size() > MAX_REQUEST_BODY_BYTES) {
            st->chunkedTooLarge = true;
            return false;
        }
        st->buffer = st->buffer.mid((int)needed);
    }
}
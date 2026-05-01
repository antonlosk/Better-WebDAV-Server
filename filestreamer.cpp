#include "filestreamer.h"
#include "httputils.h"

#include <QDateTime>

// ─────────────────────────────────────────────────────────────────────────────
FileStreamer *FileStreamer::create(
    QTcpSocket                  *socket,
    int                          statusCode,
    const QString               &statusText,
    const QMap<QString,QString> &headers,
    const QString               &filePath,
    qint64                       offset,
    qint64                       length,
    bool                         keepAlive)
{
    if (!socket || !socket->isOpen()) return nullptr;

    // Open file first, then send headers.
    auto *streamer = new FileStreamer(socket, filePath, offset, length,
                                      keepAlive);
    if (!streamer->m_file.isOpen()) {
        delete streamer;
        return nullptr;
    }

    // ── Send HTTP headers ─────────────────────────────────────────────────────
    QByteArray headerData;
    headerData.reserve(512);
    headerData += QString("HTTP/1.1 %1 %2\r\n")
                      .arg(statusCode).arg(statusText).toUtf8();
    headerData += "Server: BetterWebDAV/1.0\r\n";
    headerData += QString("Date: %1\r\n")
                      .arg(HttpUtils::formatDate(
                          QDateTime::currentDateTimeUtc())).toUtf8();
    headerData += keepAlive
                      ? "Connection: keep-alive\r\n"
                      : "Connection: close\r\n";

    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        headerData += QString("%1: %2\r\n")
                          .arg(it.key(), it.value()).toUtf8();
    headerData += "\r\n";

    socket->write(headerData);

    connect(socket,   &QTcpSocket::bytesWritten,
            streamer, &FileStreamer::onBytesWritten,
            Qt::UniqueConnection);

    connect(socket,   &QTcpSocket::disconnected,
            streamer, &FileStreamer::onSocketDisconnected,
            Qt::UniqueConnection);

    // Send first chunk
    streamer->sendNextChunk();

    return streamer;
}

// ─────────────────────────────────────────────────────────────────────────────
FileStreamer::FileStreamer(
    QTcpSocket    *socket,
    const QString &filePath,
    qint64         offset,
    qint64         length,
    bool           keepAlive,
    QObject       *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_file(filePath)
    , m_remaining(length)
    , m_keepAlive(keepAlive)
{
    if (!m_file.open(QIODevice::ReadOnly)) return;
    if (offset > 0) m_file.seek(offset);
}

// ─────────────────────────────────────────────────────────────────────────────
void FileStreamer::sendNextChunk()
{
    if (m_done || m_fileDone) return;
    if (!m_socket || !m_socket->isOpen()) { finish(false); return; }

    // If socket buffer is full, wait for bytesWritten
    if (m_socket->bytesToWrite() > SOCKET_BUFFER_LIMIT) return;

    while (m_remaining > 0) {
        if (!m_socket->isOpen()) { finish(false); return; }

        qint64     toRead = qMin(m_remaining, CHUNK_SIZE);
        QByteArray chunk  = m_file.read(toRead);

        if (chunk.isEmpty()) {
            // File ended earlier than expected - likely truncated
            finish(false);
            return;
        }

        qint64 written = m_socket->write(chunk);
        if (written < 0) {
            finish(false);
            return;
        }

        m_remaining -= chunk.size();

        // Stop reading when buffer is full
        if (m_socket->bytesToWrite() > SOCKET_BUFFER_LIMIT) break;
    }

    // Entire file has been read and queued in Qt buffer
    if (m_remaining <= 0) {
        m_file.close();
        m_fileDone = true;

        // ── Key behavior ──────────────────────────────────────────────────────
        // Do NOT close socket here.
        // Data may still be pending in Qt/OS buffers.
        // Wait for onBytesWritten where bytesToWrite() reaches 0.
        if (m_socket->bytesToWrite() == 0) {
            // Buffer already empty - all data sent
            finish(true);
        }
        // otherwise finish() will be called from onBytesWritten
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void FileStreamer::onBytesWritten(qint64 /*bytes*/)
{
    if (m_done) return;

    if (!m_fileDone) {
        // File still has data to send
        sendNextChunk();
    } else {
        // File fully read, wait for socket buffer to drain
        if (m_socket && m_socket->bytesToWrite() == 0) {
            finish(true);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void FileStreamer::onSocketDisconnected()
{
    if (!m_done) finish(false);
}

// ─────────────────────────────────────────────────────────────────────────────
void FileStreamer::finish(bool ok)
{
    if (m_done) return;
    m_done = true;

    // Close file if still open
    if (m_file.isOpen()) m_file.close();

    if (m_socket) {
        // Disconnect our slots to avoid affecting other socket users
        disconnect(m_socket, &QTcpSocket::bytesWritten,
                   this,     &FileStreamer::onBytesWritten);
        disconnect(m_socket, &QTcpSocket::disconnected,
                   this,     &FileStreamer::onSocketDisconnected);

        if (ok && !m_keepAlive) {
            // ── Graceful TCP close ────────────────────────────────────────────
            // waitForBytesWritten waits until OS accepts queued Qt data.
            // Then disconnectFromHost sends FIN after all bytes.
            while (m_socket->bytesToWrite() > 0) {
                if (!m_socket->waitForBytesWritten(5000)) break;
            }
            m_socket->disconnectFromHost();
        }
    }

    if (ok) emit finished();
    else    emit failed();

    deleteLater();
}
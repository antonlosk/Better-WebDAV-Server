#include "filestreamer.h"
#include "httputils.h"
#include "webdavworker.h"

#include <QDateTime>

FileStreamer *FileStreamer::create(
    QTcpSocket                  *socket,
    int                          statusCode,
    const QString               &statusText,
    const QMap<QString,QString> &headers,
    const QString               &filePath,
    qint64                       offset,
    qint64                       length,
    bool                         keepAlive,
    WebDavWorker                *worker)
{
    if (!socket || !socket->isOpen()) return nullptr;

    auto *streamer = new FileStreamer(socket, filePath, offset, length,
                                      keepAlive, worker);
    if (!streamer->m_file.isOpen()) {
        delete streamer;
        return nullptr;
    }

    // Send HTTP headers
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
    if (worker)
        worker->addBytesSent(headerData.size());

    // Подключаем сигналы: bytesWritten, disconnected и destroyed для безопасности
    QObject::connect(socket, &QTcpSocket::bytesWritten,
                     streamer, &FileStreamer::onBytesWritten,
                     Qt::UniqueConnection);
    QObject::connect(socket, &QTcpSocket::disconnected,
                     streamer, &FileStreamer::onSocketDisconnected,
                     Qt::UniqueConnection);
    QObject::connect(socket, &QObject::destroyed,
                     streamer, &FileStreamer::onSocketDestroyed,
                     Qt::UniqueConnection);

    streamer->sendNextChunk();
    return streamer;
}

FileStreamer::FileStreamer(
    QTcpSocket    *socket,
    const QString &filePath,
    qint64         offset,
    qint64         length,
    bool           keepAlive,
    WebDavWorker  *worker,
    QObject       *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_file(filePath)
    , m_remaining(length)
    , m_keepAlive(keepAlive)
    , m_worker(worker)
{
    if (!m_file.open(QIODevice::ReadOnly)) return;
    if (offset > 0) m_file.seek(offset);
}

void FileStreamer::sendNextChunk()
{
    if (m_done || m_fileDone) return;
    if (m_socket.isNull() || !m_socket->isOpen()) { finish(false); return; }

    if (m_socket->bytesToWrite() > SOCKET_BUFFER_LIMIT) return;

    while (m_remaining > 0) {
        if (m_socket.isNull() || !m_socket->isOpen()) { finish(false); return; }

        qint64     toRead = qMin(m_remaining, CHUNK_SIZE);
        QByteArray chunk  = m_file.read(toRead);

        if (chunk.isEmpty()) {
            finish(false);
            return;
        }

        qint64 written = m_socket->write(chunk);
        if (written < 0) {
            finish(false);
            return;
        }

        m_remaining -= chunk.size();
        if (m_worker)
            m_worker->addBytesSent(chunk.size());

        if (m_socket->bytesToWrite() > SOCKET_BUFFER_LIMIT) break;
    }

    if (m_remaining <= 0) {
        m_file.close();
        m_fileDone = true;
        if (!m_socket.isNull() && m_socket->bytesToWrite() == 0) {
            finish(true);
        }
    }
}

void FileStreamer::onBytesWritten(qint64 /*bytes*/)
{
    if (m_done) return;
    if (m_socket.isNull()) { finish(false); return; }

    if (!m_fileDone) {
        sendNextChunk();
    } else {
        if (m_socket->bytesToWrite() == 0) {
            finish(true);
        }
    }
}

void FileStreamer::onSocketDisconnected()
{
    if (!m_done) finish(false);
}

void FileStreamer::onSocketDestroyed()
{
    // Сокет уничтожен вне нашего контроля – немедленно завершаем
    if (!m_done) finish(false);
}

void FileStreamer::finish(bool ok)
{
    if (m_done) return;
    m_done = true;

    if (m_file.isOpen()) m_file.close();

    if (!m_socket.isNull()) {
        // Отключаем все сигналы от сокета
        QObject::disconnect(m_socket.data(), nullptr, this, nullptr);

        if (ok && !m_keepAlive) {
            while (m_socket->bytesToWrite() > 0) {
                if (!m_socket->waitForBytesWritten(5000)) break;
            }
            m_socket->disconnectFromHost();
        }
    }

    if (ok) emit finished();
    else    emit failed();

    // больше нет deleteLater – удалением занимается вызывающий код
}
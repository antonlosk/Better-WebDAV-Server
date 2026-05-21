#pragma once

#include <QObject>
#include <QFile>
#include <QTcpSocket>
#include <QMap>
#include <QString>
#include <QByteArray>

class WebDavWorker;

class FileStreamer : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 CHUNK_SIZE          = 256 * 1024;  // 256 KB
    static constexpr qint64 SOCKET_BUFFER_LIMIT = 4 * CHUNK_SIZE; // 1 MB

    static FileStreamer *create(
        QTcpSocket                  *socket,
        int                          statusCode,
        const QString               &statusText,
        const QMap<QString,QString> &headers,
        const QString               &filePath,
        qint64                       offset,
        qint64                       length,
        bool                         keepAlive,
        WebDavWorker                *worker);

signals:
    void finished();  // file fully sent and socket buffer is empty
    void failed();    // I/O error or socket disconnected

private slots:
    void onBytesWritten(qint64 bytes);
    void onSocketDisconnected();

private:
    explicit FileStreamer(
        QTcpSocket    *socket,
        const QString &filePath,
        qint64         offset,
        qint64         length,
        bool           keepAlive,
        WebDavWorker  *worker,
        QObject       *parent = nullptr);

    void sendNextChunk();
    void finish(bool ok);

    QTcpSocket   *m_socket;
    QFile         m_file;
    qint64        m_remaining;      // bytes left to send from file
    bool          m_keepAlive;
    WebDavWorker *m_worker;
    bool          m_fileDone = false; // whole file read and queued in buffer
    bool          m_done     = false; // finish() already called
};
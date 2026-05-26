#pragma once

#include <QObject>
#include <QFile>
#include <QTcpSocket>
#include <QMap>
#include <QString>
#include <QByteArray>
#include <QPointer>

class WebDavWorker;

class FileStreamer : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 CHUNK_SIZE          = 256 * 1024;
    static constexpr qint64 SOCKET_BUFFER_LIMIT = 4 * CHUNK_SIZE;

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

    void finish(bool ok);   // больше НЕ делает deleteLater

signals:
    void finished();
    void failed();

private slots:
    void onBytesWritten(qint64 bytes);
    void onSocketDisconnected();
    void onSocketDestroyed();

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

    QPointer<QTcpSocket> m_socket;
    QFile                 m_file;
    qint64                m_remaining;
    bool                  m_keepAlive;
    WebDavWorker         *m_worker;
    bool                  m_fileDone = false;
    bool                  m_done     = false;
};
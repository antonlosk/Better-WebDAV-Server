#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QQueue>
#include <QFile>
#include <QTemporaryFile>
#include <QAtomicInteger>
#include <QElapsedTimer>
#include <QTimer>

#include "davhandlers.h"
#include "davutils.h"

class FileStreamer;

class WebDavWorker : public QObject
{
    Q_OBJECT

public:
    explicit WebDavWorker(QObject *parent = nullptr);
    ~WebDavWorker();

    qint64 bytesSent()     const { return m_bytesSent.loadRelaxed(); }
    qint64 bytesReceived() const { return m_bytesReceived.loadRelaxed(); }

    void addBytesSent(qint64 bytes);
    void addBytesReceived(qint64 bytes);

    void setIdleSettings(int timeoutMs, int intervalMs);

public slots:
    void startServer(const QString &rootPath, quint16 port);
    void stopServer();
    void checkIdleConnections();

signals:
    void logMessage(const QString &message, const QString &level);
    void serverStarted(quint16 port);
    void serverStartFailed(const QString &reason);
    void serverStopped();
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void onStreamFinished();

private:
    enum ParseState { WaitingHeaders, WaitingBody };

    struct ClientState {
        QByteArray            buffer;
        ParseState            state           = WaitingHeaders;
        QString               method;
        QString               path;
        QString               version;
        QMap<QString,QString> headers;
        qint64                contentLength   = 0;
        QByteArray            body;
        bool                  expectContinue  = false;
        bool                  chunked         = false;
        bool                  chunkedComplete = false;
        bool                  chunkedParseError = false;
        bool                  chunkedTooLarge = false;
        QTemporaryFile       *uploadFile      = nullptr;
        QString               uploadPath;
        bool                  uploadFailed    = false;
        QQueue<HttpRequest>   requestQueue;
        bool                  streaming       = false;
        QElapsedTimer         lastActivity;
    };

    QTcpServer *m_tcpServer = nullptr;
    QString     m_rootPath;
    quint16     m_port      = 80;
    bool        m_running   = false;

    QMap<QTcpSocket*, ClientState*> m_clients;
    QMap<QObject*,    QTcpSocket*>  m_streamerToSocket;

    QAtomicInteger<qint64> m_bytesSent     = 0;
    QAtomicInteger<qint64> m_bytesReceived = 0;

    QTimer *m_idleTimer = nullptr;

    int m_idleTimeoutMs  = 60000;   // 60 секунд по умолчанию
    int m_idleIntervalMs = 10000;   // 10 секунд по умолчанию

    void loadIdleSettings();         // загрузка из QSettings
    void parseIncoming   (QTcpSocket *socket);
    void dispatchNext    (QTcpSocket *socket);
    bool decodeChunked   (ClientState *st);
    bool decodeChunkedToFile(ClientState *st);
    void executeRequest  (QTcpSocket *socket, const HttpRequest &req);
};
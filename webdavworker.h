#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QQueue>

#include "davhandlers.h"

class FileStreamer;

class WebDavWorker : public QObject
{
    Q_OBJECT

public:
    explicit WebDavWorker(QObject *parent = nullptr);
    ~WebDavWorker();

public slots:
    void startServer(const QString &rootPath, quint16 port);
    void stopServer();

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

        QQueue<HttpRequest>   requestQueue;
        bool                  streaming       = false;
    };

    QTcpServer *m_tcpServer = nullptr;
    QString     m_rootPath;
    quint16     m_port      = 80;
    bool        m_running   = false;

    QMap<QTcpSocket*, ClientState*> m_clients;
    QMap<QObject*,    QTcpSocket*>  m_streamerToSocket;

    void parseIncoming (QTcpSocket *socket);
    void dispatchNext  (QTcpSocket *socket);
    bool decodeChunked (ClientState *st);
    void executeRequest(QTcpSocket *socket, const HttpRequest &req);
};
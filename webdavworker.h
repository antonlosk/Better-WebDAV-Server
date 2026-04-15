#ifndef WEBDAVWORKER_H
#define WEBDAVWORKER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <QHash>
#include <QFile>
#include <QTimer>

class MainWindow;

class WebDAVWorker : public QObject
{
    Q_OBJECT
public:
    explicit WebDAVWorker(MainWindow *mainWindow, quint16 port, QObject *parent = nullptr);
    ~WebDAVWorker();

public slots:
    void start();
    void stop();

signals:
    void appendLog(const QString &message);
    void finished();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onSocketTimeout();

private:
    struct ClientState {
        QByteArray buffer;
        bool headerParsed = false;
        bool chunked = false;
        int contentLength = 0;
        bool expectContinue = false;
        bool sentContinue = false;
        QByteArray chunkBuffer;
        qint64 totalBodyWritten = 0;
        QFile *uploadFile = nullptr;
        QString uploadPath;
        QString method;
        QString path;
        QString version;
        bool requestHandled = false;
        QFile *putFile = nullptr;
        qint64 putBytesWritten = 0;
        QByteArray requestHeaders;
        int depth = 1;
        bool uploadCompleted = false;
    };

    void handleRequest(QTcpSocket *socket, ClientState &state);

    void handlePropfind(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleMkcol(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleOptions(QTcpSocket *socket, ClientState &state);
    void handleGet(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handlePut(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleDelete(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleHead(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleMove(QTcpSocket *socket, ClientState &state);

    void processChunkedBody(QTcpSocket *socket, ClientState &state);
    void processContentLengthPut(QTcpSocket *socket, ClientState &state);

    QTcpServer *tcpServer;
    MainWindow *mainWindow;
    QHash<QTcpSocket*, ClientState> clients;
    quint16 port;
    bool isRunning;
    const qint64 MAX_MEMORY_BUFFER = 1 * 1024 * 1024;
    const QString ROOT_PATH = "C:/";
};

#endif // WEBDAVWORKER_H
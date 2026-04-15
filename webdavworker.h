#ifndef WEBDAVWORKER_H
#define WEBDAVWORKER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <QHash>
#include <QTimer>
#include "clientstate.h"

class MainWindow;
class WebDAVRequestHandler;

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
    void started(bool success);   // новый сигнал

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onSocketTimeout();

private:
    QTcpServer *tcpServer;
    MainWindow *mainWindow;
    QHash<QTcpSocket*, ClientState> clients;
    WebDAVRequestHandler *requestHandler;
    quint16 port;
    bool isRunning;
    const QString ROOT_PATH = "C:/";
};

#endif // WEBDAVWORKER_H
#ifndef WEBDAVSERVER_H
#define WEBDAVSERVER_H

#include <QObject>
#include <QThread>

class MainWindow;
class WebDAVWorker;

class WebDAVServer : public QObject
{
    Q_OBJECT
public:
    explicit WebDAVServer(MainWindow *mainWindow, const QString &rootPath, QObject *parent = nullptr);
    ~WebDAVServer();

    bool startServer(quint16 port);
    void stopServer();

signals:
    void appendLog(const QString &message);
    void serverStarted(bool success);

private:
    QThread workerThread;
    WebDAVWorker *worker;
    MainWindow *mainWindow;
    QString rootPath;
};

#endif // WEBDAVSERVER_H
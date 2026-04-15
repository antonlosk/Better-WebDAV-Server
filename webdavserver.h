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
    explicit WebDAVServer(MainWindow *mainWindow, QObject *parent = nullptr);
    ~WebDAVServer();

    bool startServer(quint16 port);
    void stopServer();

signals:
    // Проксируем сигнал логирования из воркера
    void appendLog(const QString &message);

private:
    QThread workerThread;
    WebDAVWorker *worker;
    MainWindow *mainWindow;
};

#endif // WEBDAVSERVER_H
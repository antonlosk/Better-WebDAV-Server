#pragma once

#include <QObject>
#include <QThread>
#include <QDir>

class WebDavWorker;

class WebDavServer : public QObject
{
    Q_OBJECT

public:
    explicit WebDavServer(QObject *parent = nullptr);
    ~WebDavServer();

    bool    start(const QString &rootPath, quint16 port = 80);
    void    stop();

    bool    isRunning() const;
    quint16 port()      const;
    QString rootPath()  const;

signals:
    void logMessage(const QString &message, const QString &level);
    void serverStarted(quint16 port);
    void serverStartFailed(const QString &reason);
    void serverStopped();
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);

    void _startRequested(const QString &rootPath, quint16 port);
    void _stopRequested();

private slots:
    void onServerStarted(quint16 port);
    void onServerStartFailed(const QString &reason);
    void onServerStopped();

private:
    QThread      *m_thread  = nullptr;
    WebDavWorker *m_worker  = nullptr;

    bool    m_running  = false;
    bool    m_startPending = false;
    quint16 m_port     = 80;
    QString m_rootPath;
};
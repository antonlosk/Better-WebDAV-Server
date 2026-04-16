#ifndef WEBDAVSERVER_H
#define WEBDAVSERVER_H

#include <QTcpServer>
#include <QThread>

class WebDavServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit WebDavServer(QObject *parent = nullptr);

    void start(const QString &rootPath);
    void stop();
    bool isRunning() const;

signals:
    void stateChanged(bool isRunning);
    void logMessage(const QString &message);

protected:
    void incomingConnection(qintptr handle) override;

private:
    QString m_rootPath;
    bool m_isRunning;
};

#endif // WEBDAVSERVER_H
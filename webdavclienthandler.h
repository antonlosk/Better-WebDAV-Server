#ifndef WEBDAVCLIENTHANDLER_H
#define WEBDAVCLIENTHANDLER_H

#include <QObject>
#include <QTcpSocket>
#include <QUrl>
#include <QTimer>

class FileManager;

class WebDavClientHandler : public QObject
{
    Q_OBJECT
public:
    explicit WebDavClientHandler(qintptr socketDescriptor, const QString &rootPath, QObject *parent = nullptr);

public slots:
    void run();

signals:
    void finished();
    void logMessage(const QString &message);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onTimeout();

private:
    void resetForNextRequest();
    void parseRequest();
    void sendResponse(int statusCode, const QByteArray &body = QByteArray(), const QByteArray &extraHeaders = QByteArray());

    // Обработчики методов WebDAV
    void handleOptions(const QString &path);
    void handlePropfind(const QString &path);
    void handleGet(const QString &path);
    void handlePut(const QString &path, const QByteArray &body);
    void handleDelete(const QString &path);
    void handleMkcol(const QString &path);
    void handleCopy(const QString &sourcePath, const QString &destination);
    void handleMove(const QString &sourcePath, const QString &destination);
    void handleLock(const QString &path);
    void handleUnlock(const QString &path);

    // Вспомогательные методы
    bool hasRequestBody() const;
    QByteArray getRequestBody() const;

    QTcpSocket *m_socket;
    qintptr m_socketDescriptor;
    QString m_rootPath;
    FileManager *m_fileManager;

    // Буфер для сбора запроса
    QByteArray m_buffer;
    bool m_headersParsed;
    int m_contentLength;
    QByteArray m_method;
    QString m_path;
    QByteArray m_requestHeaders;

    // Для Keep-Alive
    bool m_keepAlive;
    QTimer *m_timeoutTimer;
    static const int KEEP_ALIVE_TIMEOUT = 10000; // 10 секунд
};

#endif // WEBDAVCLIENTHANDLER_H
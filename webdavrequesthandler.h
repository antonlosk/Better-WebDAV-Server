#ifndef WEBDAVREQUESTHANDLER_H
#define WEBDAVREQUESTHANDLER_H

#include <QObject>
#include <QTcpSocket>
#include <QString>
#include "clientstate.h"

class WebDAVRequestHandler : public QObject
{
    Q_OBJECT
public:
    explicit WebDAVRequestHandler(const QString &rootPath, QObject *parent = nullptr);

    void handleRequest(QTcpSocket *socket, ClientState &state);
    void processChunkedBody(QTcpSocket *socket, ClientState &state);
    void processContentLengthPut(QTcpSocket *socket, ClientState &state);

    // Проверка превышения лимита памяти для не-PUT тел
    bool isBodyTooLarge(int contentLength) const { return contentLength > MAX_MEMORY_BUFFER; }

signals:
    void appendLog(const QString &message);

private:
    void handlePropfind(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleMkcol(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleOptions(QTcpSocket *socket, ClientState &state);
    void handleGet(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handlePut(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleDelete(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleHead(QTcpSocket *socket, ClientState &state, const QString &decodedPath);
    void handleMove(QTcpSocket *socket, ClientState &state);

    const QString ROOT_PATH;
    const qint64 MAX_MEMORY_BUFFER = 1 * 1024 * 1024;
};

#endif // WEBDAVREQUESTHANDLER_H
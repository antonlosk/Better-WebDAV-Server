#pragma once

#include <QTcpSocket>
#include <QMap>
#include <QString>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

// HTTP request context
struct HttpRequest {
    QString               method;
    QString               path;
    QString               version;
    QMap<QString,QString> headers;
    QByteArray            body;
};

// Forward declaration
class FileStreamer;

namespace DavHandlers
{
// handleGet returns FileStreamer* when streaming starts,
// nullptr in all other cases (directory, 304, 416, error)
FileStreamer *handleGet(QTcpSocket *s, const HttpRequest &req,
                        const QString &rootPath);

void handleOptions (QTcpSocket *s, const HttpRequest &req,
                   const QString &rootPath);
void handleHead    (QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath);
void handlePut     (QTcpSocket *s, const HttpRequest &req,
               const QString &rootPath);
void handleDelete  (QTcpSocket *s, const HttpRequest &req,
                  const QString &rootPath);
void handleMkcol   (QTcpSocket *s, const HttpRequest &req,
                 const QString &rootPath);
void handlePropfind(QTcpSocket *s, const HttpRequest &req,
                    const QString &rootPath);
void handleMove    (QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath);
void handleCopy    (QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath);

bool isKeepAlive   (const HttpRequest &req);
void sendDirListing(QTcpSocket *s, const HttpRequest &req,
                    const QString &localDir);
}
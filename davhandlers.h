#pragma once

#include <QTcpSocket>
#include <QMap>
#include <QString>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

class WebDavWorker;

struct HttpRequest {
    QString               method;
    QString               path;
    QString               version;
    QMap<QString,QString> headers;
    QByteArray            body;
    QString               tempFilePath;
};

class FileStreamer;

namespace DavHandlers
{
FileStreamer *handleGet(QTcpSocket *s, const HttpRequest &req,
                        const QString &rootPath,
                        WebDavWorker *worker);

void handleOptions (QTcpSocket *s, const HttpRequest &req,
                   const QString &rootPath,
                   WebDavWorker *worker);
void handleHead    (QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath,
                WebDavWorker *worker);
void handlePut     (QTcpSocket *s, const HttpRequest &req,
               const QString &rootPath,
               WebDavWorker *worker);
void handleDelete  (QTcpSocket *s, const HttpRequest &req,
                  const QString &rootPath,
                  WebDavWorker *worker);
void handleMkcol   (QTcpSocket *s, const HttpRequest &req,
                 const QString &rootPath,
                 WebDavWorker *worker);
void handlePropfind(QTcpSocket *s, const HttpRequest &req,
                    const QString &rootPath,
                    WebDavWorker *worker);
void handleMove    (QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath,
                WebDavWorker *worker);
void handleCopy    (QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath,
                WebDavWorker *worker);

bool isKeepAlive   (const HttpRequest &req);
void sendDirListing(QTcpSocket *s, const HttpRequest &req,
                    const QString &localDir,
                    WebDavWorker *worker);
}
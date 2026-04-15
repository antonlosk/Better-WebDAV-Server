#ifndef WEBDAVXMLBUILDER_H
#define WEBDAVXMLBUILDER_H

#include <QByteArray>
#include <QString>
#include <QFile>
#include <QTcpSocket>
#include <QMap>

class WebDAVXmlBuilder
{
public:
    static QByteArray buildPropfindResponse(const QString &path,
                                            const QString &localPath,
                                            int depth);

    static void sendResponse(QTcpSocket *socket,
                             int statusCode,
                             const QByteArray &contentType,
                             const QByteArray &body,
                             const QMap<QByteArray, QByteArray> &extraHeaders = {});

    static void sendStreamResponse(QTcpSocket *socket,
                                   int statusCode,
                                   const QByteArray &contentType,
                                   QFile *file,
                                   qint64 startByte,
                                   qint64 endByte,
                                   qint64 totalSize,
                                   const QMap<QByteArray, QByteArray> &extraHeaders = {});

    static void sendHeadersOnly(QTcpSocket *socket,
                                int statusCode,
                                const QByteArray &contentType,
                                qint64 contentLength,
                                const QMap<QByteArray, QByteArray> &extraHeaders = {});

private:
    static QByteArray statusText(int statusCode);
};

#endif // WEBDAVXMLBUILDER_H
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
    // Генерация тела ответа PROPFIND (Multi-Status)
    static QByteArray buildPropfindResponse(const QString &path,
                                            const QString &localPath,
                                            int depth);

    // Отправка простого ответа (без тела или с телом)
    static void sendResponse(QTcpSocket *socket,
                             int statusCode,
                             const QByteArray &contentType,
                             const QByteArray &body,
                             const QMap<QByteArray, QByteArray> &extraHeaders = {});

    // Отправка потокового ответа (файл)
    static void sendStreamResponse(QTcpSocket *socket,
                                   int statusCode,
                                   const QByteArray &contentType,
                                   QFile *file,
                                   const QMap<QByteArray, QByteArray> &extraHeaders = {});

    // Отправка только заголовков (для HEAD)
    static void sendHeadersOnly(QTcpSocket *socket,
                                int statusCode,
                                const QByteArray &contentType,
                                qint64 contentLength,
                                const QMap<QByteArray, QByteArray> &extraHeaders = {});

private:
    // Вспомогательная функция для статусных текстов
    static QByteArray statusText(int statusCode);
};

#endif // WEBDAVXMLBUILDER_H
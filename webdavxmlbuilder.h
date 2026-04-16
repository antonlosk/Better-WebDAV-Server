#ifndef WEBDAVXMLBUILDER_H
#define WEBDAVXMLBUILDER_H

#include <QByteArray>
#include <QString>
#include <QFileInfo>
#include <QXmlStreamWriter>

class WebDavXmlBuilder
{
public:
    // Генерация ответа на PROPFIND для одного ресурса или коллекции
    static QByteArray buildPropfindResponse(const QString &path,
                                            const QFileInfo &fileInfo,
                                            const QList<QFileInfo> &children,
                                            const QString &mimeType);

    // Генерация ответа на OPTIONS (объявление возможностей сервера)
    static QByteArray buildOptionsResponse();

    // Генерация ответа с ошибкой в формате Multi-Status
    static QByteArray buildErrorResponse(const QString &path, int statusCode, const QString &message = {});

    // Генерация тела для успешного ответа LOCK (пока упрощённо)
    static QByteArray buildLockResponse(const QString &path, const QString &lockToken);

private:
    static void writePropertyElement(QXmlStreamWriter &xml, const QFileInfo &fileInfo, const QString &mimeType);
};

#endif // WEBDAVXMLBUILDER_H
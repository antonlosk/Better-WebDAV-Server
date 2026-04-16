#include "webdavxmlbuilder.h"
#include "mimeutils.h"
#include <QXmlStreamWriter>
#include <QDateTime>
#include <QUrl>

// Вспомогательная функция для кодирования пути с сохранением слешей
static QString encodeHref(const QString &path) {
    // percent-encoding всех символов, кроме разрешённых (включая '/')
    return QString::fromUtf8(QUrl::toPercentEncoding(path, "/"));
}

QByteArray WebDavXmlBuilder::buildPropfindResponse(const QString &path,
                                                   const QFileInfo &fileInfo,
                                                   const QList<QFileInfo> &children,
                                                   const QString &mimeType)
{
    QByteArray result;
    QXmlStreamWriter xml(&result);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("DAV:", "multistatus");
    xml.writeDefaultNamespace("DAV:");

    // Нормализация пути для корневого ресурса
    QString displayPath = path.isEmpty() ? "/" : "/" + path;
    // Для коллекций добавляем завершающий слеш
    if (fileInfo.isDir() && !displayPath.endsWith('/'))
        displayPath += '/';

    QString encodedPath = encodeHref(displayPath);

    // Ответ для самого ресурса
    xml.writeStartElement("DAV:", "response");
    xml.writeTextElement("DAV:", "href", encodedPath);
    xml.writeStartElement("DAV:", "propstat");
    xml.writeStartElement("DAV:", "prop");

    writePropertyElement(xml, fileInfo, mimeType);

    xml.writeEndElement(); // prop
    xml.writeTextElement("DAV:", "status", fileInfo.exists() ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found");
    xml.writeEndElement(); // propstat
    xml.writeEndElement(); // response

    // Ответ для дочерних ресурсов (если коллекция)
    for (const QFileInfo &child : children) {
        // Построение абсолютного пути с ведущим слешем
        QString childPath = displayPath;
        if (!childPath.endsWith('/'))
            childPath += '/';
        childPath += child.fileName();
        if (child.isDir())
            childPath += '/';

        QString encodedChildPath = encodeHref(childPath);

        // Определяем MIME-тип для дочернего элемента
        QString childMime;
        if (child.isDir())
            childMime = "httpd/unix-directory";
        else
            childMime = MimeUtils::fromFileName(child.absoluteFilePath());

        xml.writeStartElement("DAV:", "response");
        xml.writeTextElement("DAV:", "href", encodedChildPath);
        xml.writeStartElement("DAV:", "propstat");
        xml.writeStartElement("DAV:", "prop");

        writePropertyElement(xml, child, childMime);

        xml.writeEndElement(); // prop
        xml.writeTextElement("DAV:", "status", "HTTP/1.1 200 OK");
        xml.writeEndElement(); // propstat
        xml.writeEndElement(); // response
    }

    xml.writeEndElement(); // multistatus
    xml.writeEndDocument();
    return result;
}

QByteArray WebDavXmlBuilder::buildOptionsResponse()
{
    QByteArray result;
    QXmlStreamWriter xml(&result);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("DAV:", "options");
    xml.writeDefaultNamespace("DAV:");
    xml.writeTextElement("DAV:", "activity-collection-set", "");
    xml.writeEndElement(); // options
    xml.writeEndDocument();
    return result;
}

QByteArray WebDavXmlBuilder::buildErrorResponse(const QString &path, int statusCode, const QString &message)
{
    QByteArray result;
    QXmlStreamWriter xml(&result);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("DAV:", "multistatus");
    xml.writeDefaultNamespace("DAV:");

    xml.writeStartElement("DAV:", "response");
    // Используем кодирование пути для консистентности
    QString encodedPath = encodeHref(path.isEmpty() ? "/" : path);
    xml.writeTextElement("DAV:", "href", encodedPath);
    xml.writeTextElement("DAV:", "status", QString("HTTP/1.1 %1").arg(statusCode));
    if (!message.isEmpty()) {
        xml.writeTextElement("DAV:", "responsedescription", message);
    }
    xml.writeEndElement(); // response

    xml.writeEndElement(); // multistatus
    xml.writeEndDocument();
    return result;
}

QByteArray WebDavXmlBuilder::buildLockResponse(const QString &path, const QString &lockToken)
{
    QByteArray result;
    QXmlStreamWriter xml(&result);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("DAV:", "prop");
    xml.writeDefaultNamespace("DAV:");

    xml.writeStartElement("DAV:", "lockdiscovery");
    xml.writeStartElement("DAV:", "activelock");

    xml.writeStartElement("DAV:", "locktype");
    xml.writeEmptyElement("DAV:", "write");
    xml.writeEndElement(); // locktype

    xml.writeStartElement("DAV:", "lockscope");
    xml.writeEmptyElement("DAV:", "exclusive");
    xml.writeEndElement(); // lockscope

    xml.writeTextElement("DAV:", "depth", "infinity");

    xml.writeStartElement("DAV:", "locktoken");
    xml.writeTextElement("DAV:", "href", lockToken);
    xml.writeEndElement(); // locktoken

    xml.writeStartElement("DAV:", "lockroot");
    // Кодируем путь для lockroot
    QString encodedPath = encodeHref(path.isEmpty() ? "/" : path);
    xml.writeTextElement("DAV:", "href", encodedPath);
    xml.writeEndElement(); // lockroot

    xml.writeEndElement(); // activelock
    xml.writeEndElement(); // lockdiscovery

    xml.writeEndElement(); // prop
    xml.writeEndDocument();
    return result;
}

void WebDavXmlBuilder::writePropertyElement(QXmlStreamWriter &xml, const QFileInfo &fileInfo, const QString &mimeType)
{
    if (!fileInfo.exists())
        return;

    xml.writeTextElement("DAV:", "displayname", fileInfo.fileName());
    xml.writeTextElement("DAV:", "getcontentlength", QString::number(fileInfo.size()));
    xml.writeTextElement("DAV:", "getlastmodified", fileInfo.lastModified().toString(Qt::RFC2822Date));
    xml.writeTextElement("DAV:", "creationdate", fileInfo.birthTime().toString(Qt::ISODate));

    xml.writeStartElement("DAV:", "resourcetype");
    if (fileInfo.isDir()) {
        xml.writeEmptyElement("DAV:", "collection");
    }
    xml.writeEndElement(); // resourcetype

    xml.writeTextElement("DAV:", "getcontenttype", mimeType);
}
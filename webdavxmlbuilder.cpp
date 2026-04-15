#include "webdavxmlbuilder.h"

#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QXmlStreamWriter>
#include <QUrl>

QByteArray WebDAVXmlBuilder::buildPropfindResponse(const QString &path,
                                                   const QString &localPath,
                                                   int depth)
{
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists())
        return QByteArray();

    QByteArray xmlBody;
    QXmlStreamWriter xml(&xmlBody);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("D:multistatus");
    xml.writeAttribute("xmlns:D", "DAV:");

    xml.writeStartElement("D:response");
    xml.writeTextElement("D:href", path.toUtf8());
    xml.writeStartElement("D:propstat");
    xml.writeStartElement("D:prop");
    xml.writeTextElement("D:displayname", fileInfo.fileName().isEmpty() ? "/" : fileInfo.fileName());

    if (fileInfo.isDir()) {
        xml.writeStartElement("D:resourcetype");
        xml.writeEmptyElement("D:collection");
        xml.writeEndElement();
    } else {
        xml.writeStartElement("D:resourcetype");
        xml.writeEndElement();
        xml.writeTextElement("D:getcontentlength", QString::number(fileInfo.size()));
        xml.writeTextElement("D:getlastmodified", fileInfo.lastModified().toString(Qt::RFC2822Date));
        if (fileInfo.birthTime().isValid())
            xml.writeTextElement("D:creationdate", fileInfo.birthTime().toString(Qt::ISODate));
    }
    xml.writeEndElement(); // prop
    xml.writeTextElement("D:status", "HTTP/1.1 200 OK");
    xml.writeEndElement(); // propstat
    xml.writeEndElement(); // response

    if (depth == 1 && fileInfo.isDir()) {
        QDir dir(localPath);
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            xml.writeStartElement("D:response");
            QString entryPath = path;
            if (!entryPath.endsWith('/')) entryPath += '/';
            entryPath += QString::fromUtf8(QUrl::toPercentEncoding(entry.fileName()));
            xml.writeTextElement("D:href", entryPath.toUtf8());

            xml.writeStartElement("D:propstat");
            xml.writeStartElement("D:prop");
            xml.writeTextElement("D:displayname", entry.fileName());
            if (entry.isDir()) {
                xml.writeStartElement("D:resourcetype");
                xml.writeEmptyElement("D:collection");
                xml.writeEndElement();
            } else {
                xml.writeStartElement("D:resourcetype");
                xml.writeEndElement();
                xml.writeTextElement("D:getcontentlength", QString::number(entry.size()));
                xml.writeTextElement("D:getlastmodified", entry.lastModified().toString(Qt::RFC2822Date));
            }
            xml.writeEndElement(); // prop
            xml.writeTextElement("D:status", "HTTP/1.1 200 OK");
            xml.writeEndElement(); // propstat
            xml.writeEndElement(); // response
        }
    }

    xml.writeEndElement(); // multistatus
    xml.writeEndDocument();

    return xmlBody;
}

QByteArray WebDAVXmlBuilder::statusText(int statusCode)
{
    switch (statusCode) {
    case 200: return "OK";
    case 201: return "Created";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 416: return "Range Not Satisfiable";
    case 500: return "Internal Server Error";
    default:  return "Unknown";
    }
}

void WebDAVXmlBuilder::sendResponse(QTcpSocket *socket,
                                    int statusCode,
                                    const QByteArray &contentType,
                                    const QByteArray &body,
                                    const QMap<QByteArray, QByteArray> &extraHeaders)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText(statusCode) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";

    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it) {
        response += it.key() + ": " + it.value() + "\r\n";
    }
    response += "\r\n";

    socket->write(response);
    socket->write(body);
    socket->flush();
}

void WebDAVXmlBuilder::sendStreamResponse(QTcpSocket *socket,
                                          int statusCode,
                                          const QByteArray &contentType,
                                          QFile *file,
                                          qint64 startByte,
                                          qint64 endByte,
                                          qint64 totalSize,
                                          const QMap<QByteArray, QByteArray> &extraHeaders)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText(statusCode) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";

    qint64 contentLength = endByte - startByte + 1;
    response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";

    if (statusCode == 206) {
        response += "Content-Range: bytes " + QByteArray::number(startByte) + "-" +
                    QByteArray::number(endByte) + "/" + QByteArray::number(totalSize) + "\r\n";
    }

    response += "Accept-Ranges: bytes\r\n";
    response += "Connection: close\r\n";
    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it) {
        response += it.key() + ": " + it.value() + "\r\n";
    }
    response += "\r\n";
    socket->write(response);

    if (!file->seek(startByte)) {
        file->close();
        return;
    }

    const qint64 chunkSize = 256 * 1024;
    QByteArray buffer(chunkSize, Qt::Uninitialized);
    qint64 bytesRemaining = contentLength;
    while (bytesRemaining > 0 && !file->atEnd()) {
        qint64 toRead = qMin(chunkSize, bytesRemaining);
        qint64 bytesRead = file->read(buffer.data(), toRead);
        if (bytesRead <= 0) break;
        qint64 written = 0;
        while (written < bytesRead) {
            qint64 ret = socket->write(buffer.constData() + written, bytesRead - written);
            if (ret < 0) {
                file->close();
                return;
            }
            written += ret;
            if (!socket->waitForBytesWritten(10000)) { // ждём до 5 секунд
                file->close();
                return;
            }
        }
        bytesRemaining -= bytesRead;
        if (socket->state() != QAbstractSocket::ConnectedState) break;
    }
    socket->flush();
    file->close();
}

void WebDAVXmlBuilder::sendHeadersOnly(QTcpSocket *socket,
                                       int statusCode,
                                       const QByteArray &contentType,
                                       qint64 contentLength,
                                       const QMap<QByteArray, QByteArray> &extraHeaders)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText(statusCode) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    response += "Connection: close\r\n";

    for (auto it = extraHeaders.begin(); it != extraHeaders.end(); ++it) {
        response += it.key() + ": " + it.value() + "\r\n";
    }
    response += "\r\n";
    socket->write(response);
    socket->flush();
}
#include "httputils.h"

#include <QMimeDatabase>
#include <QMimeType>
#include <QTcpSocket>

namespace HttpUtils
{

// ─────────────────────────────────────────────────────────────────────────────
QString formatDate(const QDateTime &dt)
{
    static const char *wd[] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    static const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    QDateTime u = dt.toUTC();
    return QString("%1, %2 %3 %4 %5 GMT")
        .arg(wd[u.date().dayOfWeek() - 1])
        .arg(u.date().day(), 2, 10, QChar('0'))
        .arg(mo[u.date().month() - 1])
        .arg(u.date().year())
        .arg(u.time().toString("hh:mm:ss"));
}

// ─────────────────────────────────────────────────────────────────────────────
QString formatSize(qint64 size)
{
    if (size < 1024)           return QString("%1 B").arg(size);
    if (size < 1024*1024)      return QString("%1 KB").arg(size / 1024);
    if (size < 1024*1024*1024) return QString("%1 MB").arg(size / (1024*1024));
    return QString("%1 GB").arg(size / (1024LL*1024*1024));
}

// ─────────────────────────────────────────────────────────────────────────────
QString mimeType(const QString &filePath)
{
    static QMimeDatabase db;
    QMimeType mt = db.mimeTypeForFile(filePath, QMimeDatabase::MatchExtension);

    if (mt.name() == "application/octet-stream") {
        const QString ext = QFileInfo(filePath).suffix().toLower();
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "png")                  return "image/png";
        if (ext == "gif")                  return "image/gif";
        if (ext == "webp")                 return "image/webp";
        if (ext == "mp4")                  return "video/mp4";
        if (ext == "mkv")                  return "video/x-matroska";
        if (ext == "mp3")                  return "audio/mpeg";
        if (ext == "pdf")                  return "application/pdf";
    }
    return mt.name();
}

// ─────────────────────────────────────────────────────────────────────────────
void sendResponse(QTcpSocket                  *socket,
                  int                          statusCode,
                  const QString               &statusText,
                  const QMap<QString,QString> &headers,
                  const QByteArray            &body,
                  bool                         keepAlive)
{
    if (!socket || !socket->isOpen()) return;

    QByteArray resp;
    resp.reserve(512 + body.size());

    resp += QString("HTTP/1.1 %1 %2\r\n")
                .arg(statusCode).arg(statusText).toUtf8();
    resp += "Server: BetterWebDAV/1.0\r\n";
    resp += QString("Date: %1\r\n")
                .arg(formatDate(QDateTime::currentDateTimeUtc())).toUtf8();
    resp += keepAlive ? "Connection: keep-alive\r\n"
                      : "Connection: close\r\n";

    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        resp += QString("%1: %2\r\n").arg(it.key(), it.value()).toUtf8();

    resp += "\r\n";
    resp += body;

    socket->write(resp);
    socket->flush();

    if (!keepAlive)
        socket->disconnectFromHost();
}

// ─────────────────────────────────────────────────────────────────────────────
void sendError(QTcpSocket    *socket,
               int            code,
               const QString &text,
               bool           keepAlive)
{
    if (!socket || !socket->isOpen()) return;

    QByteArray body =
        QString("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                "<D:error xmlns:D=\"DAV:\">"
                "<D:status>HTTP/1.1 %1 %2</D:status>"
                "</D:error>")
            .arg(code).arg(text)
            .toUtf8();

    QMap<QString,QString> h;
    h["Content-Type"]   = "application/xml; charset=utf-8";
    h["Content-Length"] = QString::number(body.size());

    sendResponse(socket, code, text, h, body, keepAlive);
}

} // namespace HttpUtils
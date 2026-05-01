#include "davutils.h"
#include "httputils.h"

#include <QDir>
#include <QUrl>
#include <QMimeDatabase>

namespace DavUtils
{

// ─────────────────────────────────────────────────────────────────────────────
QString localPath(const QString &urlPath, const QString &rootPath)
{
    // rootPath always has trailing separator: "C:\" or "/srv/dav/"

    // Remove leading slashes
    QString rel = urlPath;
    while (rel.startsWith('/')) rel = rel.mid(1);

    // Protect against path traversal using a fake root anchor.
    // If cleanPath escapes above "__ROOT__", reject the request.
    QString anchored = QDir::cleanPath("/__ROOT__/" + rel);
    if (!(anchored == "/__ROOT__" || anchored.startsWith("/__ROOT__/")))
        return {};

    QString cleaned = anchored.mid(9); // len("/__ROOT__") == 9
    while (cleaned.startsWith('/'))
        cleaned = cleaned.mid(1);

    // Join paths: rootPath already has trailing separator
    QString full = QDir::toNativeSeparators(rootPath + cleaned);

    // Final validation
#ifdef Q_OS_WIN
    if (!full.startsWith(rootPath, Qt::CaseInsensitive)) return {};
#else
    if (!full.startsWith(rootPath)) return {};
#endif

    return full;
}

// ─────────────────────────────────────────────────────────────────────────────
QString makeEtag(const QFileInfo &fi)
{
    return QString("\"%1-%2\"")
    .arg(fi.size())
        .arg(fi.lastModified().toUTC().toSecsSinceEpoch());
}

// ─────────────────────────────────────────────────────────────────────────────
QString xmlEscape(const QString &s)
{
    QString r = s;
    r.replace('&',  "&amp;");
    r.replace('<',  "&lt;");
    r.replace('>',  "&gt;");
    r.replace('"',  "&quot;");
    r.replace('\'', "&apos;");
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
QString propfindEntry(const QFileInfo &fi, const QString &href)
{
    QString creationDate = fi.birthTime().isValid()
    ? fi.birthTime().toUTC().toString(Qt::ISODate)
    : fi.lastModified().toUTC().toString(Qt::ISODate);

    QString resourceType = fi.isDir() ? "<D:collection/>" : "";

    QString extra;
    if (!fi.isDir()) {
        extra = QString(
                    "<D:getcontenttype>%1</D:getcontenttype>"
                    "<D:getcontentlength>%2</D:getcontentlength>"
                    "<D:getetag>%3</D:getetag>")
                    .arg(xmlEscape(HttpUtils::mimeType(fi.filePath())))
                    .arg(fi.size())
                    .arg(xmlEscape(makeEtag(fi)));
    }

    QString encodedHref =
        QString::fromLatin1(QUrl::toPercentEncoding(href, "/"));

    return QString(
               "<D:response>"
               "<D:href>%1</D:href>"
               "<D:propstat><D:prop>"
               "<D:displayname>%2</D:displayname>"
               "<D:creationdate>%3</D:creationdate>"
               "<D:getlastmodified>%4</D:getlastmodified>"
               "<D:resourcetype>%5</D:resourcetype>"
               "%6"
               "</D:prop>"
               "<D:status>HTTP/1.1 200 OK</D:status>"
               "</D:propstat>"
               "</D:response>")
        .arg(xmlEscape(encodedHref),
             xmlEscape(fi.fileName()),
             xmlEscape(creationDate),
             xmlEscape(HttpUtils::formatDate(fi.lastModified())),
             resourceType,
             extra);
}

} // namespace DavUtils
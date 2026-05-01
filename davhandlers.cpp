#include "davhandlers.h"
#include "httputils.h"
#include "davutils.h"
#include "filestreamer.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <functional>

namespace DavHandlers
{

// ─────────────────────────────────────────────────────────────────────────────
bool isKeepAlive(const HttpRequest &req)
{
    QString conn = req.headers.value("connection", "").toLower();
    if (req.version == "HTTP/1.1") return !conn.contains("close");
    else                           return  conn.contains("keep-alive");
}

// ─────────────────────────────────────────────────────────────────────────────
// Parses Range header.
// Returns true if Range is valid and response should be 206.
// outIs416 = true when from is beyond file size (respond with 416).
// Clamps to to fileSize-1 when client requests a larger range.
// ─────────────────────────────────────────────────────────────────────────────
static bool parseRange(const QString &rangeHeader,
                       qint64  fileSize,
                       qint64 &outFrom,
                       qint64 &outTo,
                       bool   &outIs416)
{
    outFrom  = 0;
    outTo    = fileSize - 1;
    outIs416 = false;

    if (rangeHeader.isEmpty() ||
        !rangeHeader.startsWith("bytes=", Qt::CaseInsensitive))
        return false;

    QString spec = rangeHeader.mid(6).trimmed();

    // Use only the first range from the list
    int commaIdx = spec.indexOf(',');
    if (commaIdx > 0) spec = spec.left(commaIdx).trimmed();

    QStringList bounds = spec.split('-');
    if (bounds.size() != 2) return false;

    QString fromStr = bounds[0].trimmed();
    QString toStr   = bounds[1].trimmed();

    // suffix-range: bytes=-N -> last N bytes
    if (fromStr.isEmpty()) {
        bool ok = false;
        qint64 suffix = toStr.toLongLong(&ok);
        if (!ok || suffix <= 0) return false;
        outFrom = qMax(0LL, fileSize - suffix);
        outTo   = fileSize - 1;
        return true;
    }

    bool ok1 = false, ok2 = true;
    qint64 from = fromStr.toLongLong(&ok1);
    if (!ok1 || from < 0) return false;

    // from is beyond file size -> 416
    if (from >= fileSize) {
        outIs416 = true;
        return false;
    }

    qint64 to = toStr.isEmpty()
                    ? fileSize - 1
                    : toStr.toLongLong(&ok2);
    if (!ok2) to = fileSize - 1;

    // Client may request range beyond file size.
    // Clamp to EOF and still return 206.
    if (to >= fileSize) to = fileSize - 1;
    if (to < from) {
        outIs416 = true;
        return false;
    }

    outFrom = from;
    outTo   = to;
    return true;
}

static Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

static QString normalizedPathForCompare(const QString &path)
{
    QString p = QDir::cleanPath(QDir::toNativeSeparators(path));
    while (p.endsWith(QDir::separator()) && p.length() > 1) {
#ifdef Q_OS_WIN
        if (p.length() == 3 && p[1] == ':') break; // "C:\"
#endif
        p.chop(1);
    }
    return p;
}

static bool samePath(const QString &a, const QString &b)
{
    return normalizedPathForCompare(a).compare(normalizedPathForCompare(b),
                                               pathCaseSensitivity()) == 0;
}

static bool isInsidePath(const QString &child, const QString &parent)
{
    QString c = normalizedPathForCompare(child);
    QString p = normalizedPathForCompare(parent);
    if (c.compare(p, pathCaseSensitivity()) == 0) return true;
    QString prefix = p;
    if (!prefix.endsWith(QDir::separator())) prefix += QDir::separator();
    return c.startsWith(prefix, pathCaseSensitivity());
}

static QString destinationToLocalPath(const HttpRequest &req,
                                      const QString &rootPath)
{
    auto parseHostPort = [](const QString &hostHeader,
                            QString &hostOut,
                            int &portOut) -> bool {
        QUrl u("http://" + hostHeader);
        if (!u.isValid() || u.host().isEmpty()) return false;
        hostOut = u.host();
        portOut = u.port(-1);
        return true;
    };

    auto destinationHostMatches = [&](const QUrl &destinationUrl) -> bool {
        if (destinationUrl.host().isEmpty()) return true;

        QString reqHost;
        int reqPort = -1;
        if (!parseHostPort(req.headers.value("host").trimmed(), reqHost, reqPort))
            return false;

        const QString dstHost = destinationUrl.host();
        const int dstPort = destinationUrl.port(-1);

        if (reqHost.compare(dstHost, Qt::CaseInsensitive) != 0) return false;
        if (reqPort >= 0 && dstPort >= 0 && reqPort != dstPort) return false;
        return true;
    };

    const QString rawDestination = req.headers.value("destination").trimmed();
    if (rawDestination.isEmpty()) return {};

    QUrl destinationUrl(rawDestination);
    QString destinationPath;
    if (destinationUrl.isValid() &&
        (!destinationUrl.scheme().isEmpty() || !destinationUrl.host().isEmpty()))
    {
        if (!destinationHostMatches(destinationUrl)) return {};

        destinationPath = destinationUrl.path(QUrl::FullyDecoded);
        if (destinationPath.isEmpty()) return {};
    } else {
        destinationPath = QUrl::fromPercentEncoding(rawDestination.toUtf8());
    }
    if (destinationPath.isEmpty()) return {};

    return DavUtils::localPath(destinationPath, rootPath);
}

// ─────────────────────────────────────────────────────────────────────────────
void handleOptions(QTcpSocket *s, const HttpRequest &req,
                   const QString & /*rootPath*/)
{
    QMap<QString,QString> h;
    h["Allow"]          = "OPTIONS, GET, HEAD, PUT, DELETE,"
                 " MKCOL, PROPFIND, MOVE, COPY";
    h["DAV"]            = "1, 2";
    h["MS-Author-Via"]  = "DAV";
    h["Content-Length"] = "0";
    h["Accept-Ranges"]  = "bytes";
    HttpUtils::sendResponse(s, 200, "OK", h, {}, isKeepAlive(req));
}

// ─────────────────────────────────────────────────────────────────────────────
void sendDirListing(QTcpSocket *s, const HttpRequest &req,
                    const QString &localDir)
{
    bool ka = isKeepAlive(req);
    QDir dir(localDir);

    QString html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>Index of " + req.path.toHtmlEscaped() + "</title>"
                                     "<style>"
                                     "body{font-family:monospace;background:#1e1e1e;color:#ddd;margin:20px}"
                                     "h2{color:#4fc3f7}"
                                     "a{color:#4fc3f7;text-decoration:none}"
                                     "a:hover{text-decoration:underline}"
                                     "table{width:100%;border-collapse:collapse;margin-top:12px}"
                                     "td,th{padding:5px 14px;border-bottom:1px solid #333;text-align:left}"
                                     "th{color:#888;font-weight:normal}"
                                     "</style></head><body>"
                                     "<h2>Index of " + req.path.toHtmlEscaped() + "</h2>"
                                     "<table><tr><th>Name</th><th>Size</th><th>Modified</th></tr>";

    if (req.path != "/")
        html += "<tr><td><a href='../'>../</a></td>"
                "<td>-</td><td>-</td></tr>";

    for (const QFileInfo &e :
         dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                           QDir::DirsFirst | QDir::Name))
    {
        QString hrefRaw = req.path;
        if (!hrefRaw.endsWith('/')) hrefRaw += '/';
        hrefRaw += e.fileName();
        if (e.isDir()) hrefRaw += '/';
        QString href = QString::fromLatin1(
            QUrl::toPercentEncoding(hrefRaw, "/"));

        html += QString(
                    "<tr>"
                    "<td><a href='%1'>%2</a></td>"
                    "<td>%3</td>"
                    "<td>%4</td>"
                    "</tr>")
                    .arg(href.toHtmlEscaped(),
                         (e.fileName() + (e.isDir() ? "/" : "")).toHtmlEscaped(),
                         e.isDir() ? "-" : HttpUtils::formatSize(e.size()),
                         e.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
    }
    html += "</table></body></html>";

    QByteArray body = html.toUtf8();
    QMap<QString,QString> h;
    h["Content-Type"]   = "text/html; charset=utf-8";
    h["Content-Length"] = QString::number(body.size());
    HttpUtils::sendResponse(s, 200, "OK", h, body, ka);
}

// ─────────────────────────────────────────────────────────────────────────────
void handleHead(QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath)
{
    bool    ka = isKeepAlive(req);
    QString p  = DavUtils::localPath(req.path, rootPath);
    if (p.isEmpty()) { HttpUtils::sendError(s, 403, "Forbidden", ka); return; }

    QFileInfo fi(p);
    if (!fi.exists()) { HttpUtils::sendError(s, 404, "Not Found", ka); return; }

    if (fi.isDir()) {
        QMap<QString,QString> h;
        h["Content-Type"]  = "text/html; charset=utf-8";
        h["Accept-Ranges"] = "bytes";
        HttpUtils::sendResponse(s, 200, "OK", h, {}, ka);
        return;
    }

    qint64 fileSize = fi.size();
    qint64 from = 0, to = fileSize - 1;
    bool   is416 = false;
    bool   isRange = parseRange(req.headers.value("range").trimmed(),
                              fileSize, from, to, is416);
    if (is416) {
        QMap<QString,QString> h;
        h["Content-Range"]  = QString("bytes */%1").arg(fileSize);
        h["Content-Length"] = "0";
        h["Accept-Ranges"]  = "bytes";
        HttpUtils::sendResponse(s, 416, "Range Not Satisfiable", h, {}, ka);
        return;
    }

    int     statusCode = isRange ? 206 : 200;
    QString statusText = isRange ? "Partial Content" : "OK";

    QMap<QString,QString> h;
    h["Content-Type"]   = HttpUtils::mimeType(p);
    h["Content-Length"] = QString::number(to - from + 1);
    h["Accept-Ranges"]  = "bytes";
    h["Last-Modified"]  = HttpUtils::formatDate(fi.lastModified());
    h["ETag"]           = DavUtils::makeEtag(fi);
    h["Cache-Control"]  = "public, max-age=3600";
    if (isRange)
        h["Content-Range"] = QString("bytes %1-%2/%3")
                                 .arg(from).arg(to).arg(fileSize);

    // HEAD: body is always empty
    HttpUtils::sendResponse(s, statusCode, statusText, h, {}, ka);
}

// ─────────────────────────────────────────────────────────────────────────────
FileStreamer *handleGet(QTcpSocket *s, const HttpRequest &req,
                        const QString &rootPath)
{
    bool    ka = isKeepAlive(req);
    QString p  = DavUtils::localPath(req.path, rootPath);
    if (p.isEmpty()) {
        HttpUtils::sendError(s, 403, "Forbidden", ka);
        return nullptr;
    }

    QFileInfo fi(p);
    if (!fi.exists()) {
        HttpUtils::sendError(s, 404, "Not Found", ka);
        return nullptr;
    }

    // ── Directory -> HTML listing ─────────────────────────────────────────────
    if (fi.isDir()) {
        sendDirListing(s, req, p);
        return nullptr;
    }

    qint64  fileSize = fi.size();
    QString etag     = DavUtils::makeEtag(fi);

    // ── Zero-size file ────────────────────────────────────────────────────────
    if (fileSize == 0) {
        QMap<QString,QString> h;
        h["Content-Type"]   = HttpUtils::mimeType(p);
        h["Content-Length"] = "0";
        h["Accept-Ranges"]  = "bytes";
        h["Last-Modified"]  = HttpUtils::formatDate(fi.lastModified());
        h["ETag"]           = etag;
        h["Cache-Control"]  = "public, max-age=3600";
        HttpUtils::sendResponse(s, 200, "OK", h, {}, ka);
        return nullptr;
    }

    // ── Conditional GET (If-None-Match) ───────────────────────────────────────
    QString clientEtag = req.headers.value("if-none-match").trimmed();
    if (!clientEtag.isEmpty() && clientEtag == etag) {
        QMap<QString,QString> h;
        h["ETag"]           = etag;
        h["Content-Length"] = "0";
        h["Accept-Ranges"]  = "bytes";
        HttpUtils::sendResponse(s, 304, "Not Modified", h, {}, ka);
        return nullptr;
    }

    // ── Range ─────────────────────────────────────────────────────────────────
    qint64 rangeFrom = 0, rangeTo = fileSize - 1;
    bool   is416   = false;
    bool   isRange = parseRange(req.headers.value("range").trimmed(),
                              fileSize, rangeFrom, rangeTo, is416);
    if (is416) {
        QMap<QString,QString> h;
        h["Content-Range"]  = QString("bytes */%1").arg(fileSize);
        h["Content-Length"] = "0";
        h["Accept-Ranges"]  = "bytes";
        HttpUtils::sendResponse(s, 416, "Range Not Satisfiable", h, {}, ka);
        return nullptr;
    }

    qint64  sendLength = rangeTo - rangeFrom + 1;
    int     statusCode = isRange ? 206 : 200;
    QString statusText = isRange ? "Partial Content" : "OK";

    // ── Response headers ──────────────────────────────────────────────────────
    QMap<QString,QString> h;
    h["Content-Type"]   = HttpUtils::mimeType(p);
    h["Content-Length"] = QString::number(sendLength);
    h["Accept-Ranges"]  = "bytes";
    h["Last-Modified"]  = HttpUtils::formatDate(fi.lastModified());
    h["ETag"]           = etag;
    h["Cache-Control"]  = "public, max-age=3600";
    if (isRange)
        h["Content-Range"] = QString("bytes %1-%2/%3")
                                 .arg(rangeFrom).arg(rangeTo).arg(fileSize);

    // ── Stream file by 256 KB chunks ─────────────────────────────────────────
    // FileStreamer sends headers + body.
    // Return pointer so Worker can connect finished() signal.
    return FileStreamer::create(s, statusCode, statusText, h,
                                p, rangeFrom, sendLength, ka);
}

// ─────────────────────────────────────────────────────────────────────────────
void handlePut(QTcpSocket *s, const HttpRequest &req,
               const QString &rootPath)
{
    bool    ka = isKeepAlive(req);
    QString p  = DavUtils::localPath(req.path, rootPath);
    if (p.isEmpty()) { HttpUtils::sendError(s, 403, "Forbidden", ka); return; }
    const bool existedBefore = QFileInfo::exists(p);

    // Create parent directories if needed
    QDir().mkpath(QFileInfo(p).absolutePath());

    QFile file(p);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        HttpUtils::sendError(s, 403, "Forbidden", ka);
        return;
    }

    if (!req.body.isEmpty()) {
        qint64 written = file.write(req.body);
        if (written != (qint64)req.body.size()) {
            file.close();
            HttpUtils::sendError(s, 500, "Internal Server Error", ka);
            return;
        }
    }
    file.close();

    QMap<QString,QString> h;
    h["Content-Length"] = "0";
    h["ETag"]           = DavUtils::makeEtag(QFileInfo(p));
    HttpUtils::sendResponse(s,
                            existedBefore ? 204 : 201,
                            existedBefore ? "No Content" : "Created",
                            h, {}, ka);
}

// ─────────────────────────────────────────────────────────────────────────────
void handleDelete(QTcpSocket *s, const HttpRequest &req,
                  const QString &rootPath)
{
    bool    ka = isKeepAlive(req);
    QString p  = DavUtils::localPath(req.path, rootPath);
    if (p.isEmpty()) { HttpUtils::sendError(s, 403, "Forbidden", ka); return; }

    QFileInfo fi(p);
    if (!fi.exists()) { HttpUtils::sendError(s, 404, "Not Found", ka); return; }

    bool ok = fi.isDir()
                  ? QDir(p).removeRecursively()
                  : QFile::remove(p);

    if (ok) {
        QMap<QString,QString> h;
        h["Content-Length"] = "0";
        HttpUtils::sendResponse(s, 204, "No Content", h, {}, ka);
    } else {
        HttpUtils::sendError(s, 500, "Internal Server Error", ka);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void handleMkcol(QTcpSocket *s, const HttpRequest &req,
                 const QString &rootPath)
{
    bool    ka = isKeepAlive(req);
    QString p  = DavUtils::localPath(req.path, rootPath);
    if (p.isEmpty()) { HttpUtils::sendError(s, 403, "Forbidden", ka); return; }

    if (QDir(p).exists()) {
        HttpUtils::sendError(s, 405, "Method Not Allowed", ka);
        return;
    }

    if (QDir().mkpath(p)) {
        QMap<QString,QString> h;
        h["Content-Length"] = "0";
        HttpUtils::sendResponse(s, 201, "Created", h, {}, ka);
    } else {
        HttpUtils::sendError(s, 409, "Conflict", ka);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void handlePropfind(QTcpSocket *s, const HttpRequest &req,
                    const QString &rootPath)
{
    bool    ka = isKeepAlive(req);
    QString p  = DavUtils::localPath(req.path, rootPath);
    if (p.isEmpty()) { HttpUtils::sendError(s, 403, "Forbidden", ka); return; }

    QFileInfo fi(p);
    if (!fi.exists()) { HttpUtils::sendError(s, 404, "Not Found", ka); return; }

    // Parse Depth
    int     depth = 1;
    QString dh    = req.headers.value("depth", "1").toLower().trimmed();
    if      (dh == "0")        depth = 0;
    else if (dh == "infinity") depth = 1; // cap at 1
    else {
        bool okDepth = false;
        int parsed = dh.toInt(&okDepth);
        if (!okDepth || (parsed != 0 && parsed != 1)) {
            HttpUtils::sendError(s, 400, "Bad Request", ka);
            return;
        }
        depth = parsed;
    }

    QString baseHref = req.path;
    if (fi.isDir() && !baseHref.endsWith('/')) baseHref += '/';

    QString xml;
    xml.reserve(8192);
    xml += "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
           "<D:multistatus xmlns:D=\"DAV:\">";
    xml += DavUtils::propfindEntry(fi, baseHref);

    if (fi.isDir() && depth > 0) {
        const auto entries =
            QDir(p).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &e : entries) {
            QString childHref = baseHref + e.fileName();
            if (e.isDir()) childHref += '/';
            xml += DavUtils::propfindEntry(e, childHref);
        }
    }
    xml += "</D:multistatus>";

    QByteArray body = xml.toUtf8();
    QMap<QString,QString> h;
    h["Content-Type"]   = "application/xml; charset=utf-8";
    h["Content-Length"] = QString::number(body.size());
    h["DAV"]            = "1, 2";
    HttpUtils::sendResponse(s, 207, "Multi-Status", h, body, ka);
}

// ─────────────────────────────────────────────────────────────────────────────
void handleMove(QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath)
{
    bool    ka  = isKeepAlive(req);
    QString src = DavUtils::localPath(req.path, rootPath);
    QString dst = destinationToLocalPath(req, rootPath);

    if (src.isEmpty() || dst.isEmpty()) {
        HttpUtils::sendError(s, 400, "Bad Request", ka); return;
    }
    if (!QFileInfo::exists(src)) {
        HttpUtils::sendError(s, 404, "Not Found", ka); return;
    }
    if (samePath(src, rootPath) || samePath(dst, rootPath) || samePath(src, dst)) {
        HttpUtils::sendError(s, 403, "Forbidden", ka); return;
    }

    bool overwrite = req.headers.value("overwrite", "T").toUpper() != "F";
    bool dstExisted = QFileInfo::exists(dst);
    if (QFileInfo::exists(dst)) {
        if (!overwrite) {
            HttpUtils::sendError(s, 412, "Precondition Failed", ka); return;
        }
        QFileInfo dfi(dst);
        bool removed = dfi.isDir()
                           ? QDir(dst).removeRecursively()
                           : QFile::remove(dst);
        if (!removed) {
            HttpUtils::sendError(s, 500, "Internal Server Error", ka); return;
        }
    }

    if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
        HttpUtils::sendError(s, 409, "Conflict", ka); return;
    }

    QFileInfo srcInfo(src);
    bool moved = srcInfo.isDir()
                     ? QDir().rename(src, dst)
                     : QFile::rename(src, dst);

    if (moved) {
        QMap<QString,QString> h;
        h["Content-Length"] = "0";
        HttpUtils::sendResponse(s,
                                dstExisted ? 204 : 201,
                                dstExisted ? "No Content" : "Created",
                                h, {}, ka);
    } else {
        HttpUtils::sendError(s, 500, "Internal Server Error", ka);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void handleCopy(QTcpSocket *s, const HttpRequest &req,
                const QString &rootPath)
{
    bool    ka  = isKeepAlive(req);
    QString src = DavUtils::localPath(req.path, rootPath);
    QString dst = destinationToLocalPath(req, rootPath);

    if (src.isEmpty() || dst.isEmpty()) {
        HttpUtils::sendError(s, 400, "Bad Request", ka); return;
    }
    if (!QFileInfo::exists(src)) {
        HttpUtils::sendError(s, 404, "Not Found", ka); return;
    }
    if (samePath(src, rootPath) || samePath(dst, rootPath) || samePath(src, dst)) {
        HttpUtils::sendError(s, 403, "Forbidden", ka); return;
    }
    QFileInfo srcInfo(src);
    if (srcInfo.isDir() && isInsidePath(dst, src)) {
        HttpUtils::sendError(s, 409, "Conflict", ka); return;
    }

    bool overwrite = req.headers.value("overwrite", "T").toUpper() != "F";
    bool dstExisted = QFileInfo::exists(dst);
    if (QFileInfo::exists(dst)) {
        if (!overwrite) {
            HttpUtils::sendError(s, 412, "Precondition Failed", ka); return;
        }
        QFileInfo dfi(dst);
        bool removed = dfi.isDir()
                           ? QDir(dst).removeRecursively()
                           : QFile::remove(dst);
        if (!removed) {
            HttpUtils::sendError(s, 500, "Internal Server Error", ka); return;
        }
    }

    std::function<bool(const QString&, const QString&)> copyDir;
    copyDir = [&](const QString &from, const QString &to) -> bool {
        QDir().mkpath(to);
        for (const QString &f :
             QDir(from).entryList(QDir::Files))
            if (!QFile::copy(from + "/" + f, to + "/" + f)) return false;
        for (const QString &d :
             QDir(from).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (!copyDir(from + "/" + d, to + "/" + d)) return false;
        return true;
    };

    bool ok = srcInfo.isDir()
                  ? copyDir(src, dst)
                  : (QDir().mkpath(QFileInfo(dst).absolutePath()) &&
                     QFile::copy(src, dst));

    if (ok) {
        QMap<QString,QString> h;
        h["Content-Length"] = "0";
        HttpUtils::sendResponse(s,
                                dstExisted ? 204 : 201,
                                dstExisted ? "No Content" : "Created",
                                h, {}, ka);
    } else {
        HttpUtils::sendError(s, 500, "Internal Server Error", ka);
    }
}

} // namespace DavHandlers
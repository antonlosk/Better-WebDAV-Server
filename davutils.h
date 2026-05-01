#pragma once

#include <QString>
#include <QFileInfo>
#include <QDateTime>

namespace DavUtils
{
// Converts URL path to local filesystem path.
// rootPath must end with native separator.
// Returns empty string on path traversal attempts.
QString localPath(const QString &urlPath,
                  const QString &rootPath);

// ETag based on size and mtime
QString makeEtag(const QFileInfo &fi);

// XML-safe escaping
QString xmlEscape(const QString &s);

// Builds one <D:response> element for PROPFIND
QString propfindEntry(const QFileInfo &fi,
                      const QString   &href);
}
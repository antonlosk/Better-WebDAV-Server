#include "fileutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QMimeDatabase>
#include <QMimeType>

QString FileUtils::urlDecode(const QString &path)
{
    QByteArray encoded = path.toUtf8();
    return QUrl::fromPercentEncoding(encoded);
}

QString FileUtils::safeLocalPath(const QString &decodedPath, const QString &rootPath)
{
    QString cleanPath = QDir::cleanPath(rootPath + decodedPath);
    if (!cleanPath.startsWith(rootPath, Qt::CaseInsensitive))
        return QString();
    return cleanPath;
}

QByteArray FileUtils::getMimeType(const QString &fileName)
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fileName);
    if (mime.isValid())
        return mime.name().toUtf8();
    return "application/octet-stream";
}

bool FileUtils::copyDirectoryRecursively(const QString &srcPath, const QString &dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.mkpath(dstPath))
        return false;

    QFileInfoList entries = srcDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        QString dstEntry = dstPath + "/" + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.filePath(), dstEntry))
                return false;
        } else {
            if (!QFile::copy(entry.filePath(), dstEntry))
                return false;
        }
    }
    return true;
}
#include "mimeutils.h"
#include <QMimeDatabase>
#include <QFileInfo>

QString MimeUtils::fromFileName(const QString &fileName)
{
    static QMimeDatabase db;
    QFileInfo info(fileName);
    QMimeType mime = db.mimeTypeForFile(info);
    return mime.name();
}
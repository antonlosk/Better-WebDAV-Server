#pragma once

#include <QString>
#include <QByteArray>
#include <QMap>
#include <QDateTime>
#include <QFileInfo>
#include <QTcpSocket>

namespace HttpUtils
{
// Formatting
QString formatDate(const QDateTime &dt);
QString formatSize(qint64 size);

// MIME
QString mimeType(const QString &filePath);

// Sending
void sendResponse(QTcpSocket        *socket,
                  int                statusCode,
                  const QString     &statusText,
                  const QMap<QString,QString> &headers,
                  const QByteArray  &body,
                  bool               keepAlive);

void sendError(QTcpSocket    *socket,
               int            code,
               const QString &text,
               bool           keepAlive);
}
#pragma once

#include <QString>
#include <QByteArray>
#include <QMap>
#include <QDateTime>
#include <QFileInfo>
#include <QTcpSocket>

class WebDavWorker;   // forward declaration

namespace HttpUtils
{
QString formatDate(const QDateTime &dt);
QString formatSize(qint64 size);
QString mimeType(const QString &filePath);

void sendResponse(QTcpSocket        *socket,
                  int                statusCode,
                  const QString     &statusText,
                  const QMap<QString,QString> &headers,
                  const QByteArray  &body,
                  bool               keepAlive,
                  WebDavWorker      *worker = nullptr);

void sendError(QTcpSocket    *socket,
               int            code,
               const QString &text,
               bool           keepAlive,
               WebDavWorker  *worker = nullptr);
}
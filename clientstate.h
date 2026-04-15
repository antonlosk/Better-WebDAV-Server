#ifndef CLIENTSTATE_H
#define CLIENTSTATE_H

#include <QByteArray>
#include <QString>
#include <QFile>

struct ClientState {
    QByteArray buffer;
    bool headerParsed = false;
    bool chunked = false;
    int contentLength = 0;
    bool expectContinue = false;
    bool sentContinue = false;
    QByteArray chunkBuffer;
    qint64 totalBodyWritten = 0;
    QFile *uploadFile = nullptr;
    QString uploadPath;
    QString method;
    QString path;
    QString version;
    bool requestHandled = false;
    QFile *putFile = nullptr;
    qint64 putBytesWritten = 0;
    QByteArray requestHeaders;
    int depth = 1;
    bool uploadCompleted = false;
};

#endif // CLIENTSTATE_H
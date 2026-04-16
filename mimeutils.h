#ifndef MIMEUTILS_H
#define MIMEUTILS_H

#include <QString>

class MimeUtils
{
public:
    static QString fromFileName(const QString &fileName);
};

#endif // MIMEUTILS_H
#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <QString>
#include <QByteArray>

class FileUtils
{
public:
    // Декодирование URL (percent-encoding)
    static QString urlDecode(const QString &path);

    // Безопасное получение локального пути в пределах ROOT_PATH
    static QString safeLocalPath(const QString &decodedPath, const QString &rootPath);

    // Получение MIME-типа файла по имени
    static QByteArray getMimeType(const QString &fileName);

    // Рекурсивное копирование папки (возвращает true при успехе)
    static bool copyDirectoryRecursively(const QString &srcPath, const QString &dstPath);
};

#endif // FILEUTILS_H
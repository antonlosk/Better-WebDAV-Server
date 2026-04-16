#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QString>
#include <QFileInfo>
#include <QDir>

class FileManager
{
public:
    explicit FileManager(const QString &rootPath);

    // Возвращает абсолютный путь, проверенный на выход за пределы root
    QString absolutePath(const QString &relativePath) const;
    bool isValidPath(const QString &absolutePath) const;

    QFileInfo getFileInfo(const QString &relativePath) const;
    QList<QFileInfo> getDirectoryContents(const QString &relativePath) const;

    bool createDirectory(const QString &relativePath);
    bool deleteResource(const QString &relativePath);
    bool copyResource(const QString &sourceRelPath, const QString &destRelPath);
    bool moveResource(const QString &sourceRelPath, const QString &destRelPath);

    // Для PUT
    bool writeFile(const QString &relativePath, const QByteArray &data);
    // Для GET
    QByteArray readFile(const QString &relativePath) const;

    // Получить MIME-тип файла
    QString mimeType(const QString &relativePath) const;

private:
    QString m_rootPath;

    // Вспомогательные функции копирования директорий
    bool copyDirectoryRecursively(const QString &srcAbsPath, const QString &dstAbsPath);
    bool deleteDirectoryRecursively(const QString &absPath);
};

#endif // FILEMANAGER_H
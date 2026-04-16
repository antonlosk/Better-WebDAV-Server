#include "filemanager.h"
#include "mimeutils.h"
#include <QDir>
#include <QFile>
#include <QDebug>

FileManager::FileManager(const QString &rootPath)
    : m_rootPath(QDir::cleanPath(rootPath))
{
    if (!m_rootPath.endsWith('/'))
        m_rootPath += '/';
}

QString FileManager::absolutePath(const QString &relativePath) const
{
    QString cleanRelPath = QDir::cleanPath(relativePath);
    if (cleanRelPath.startsWith('/'))
        cleanRelPath = cleanRelPath.mid(1);
    return m_rootPath + cleanRelPath;
}

bool FileManager::isValidPath(const QString &absolutePath) const
{
    // Проверяем, что путь не выходит за пределы корневой папки
    QString cleanAbs = QDir::cleanPath(absolutePath);
    if (!cleanAbs.endsWith('/') && QFileInfo(cleanAbs).isDir())
        cleanAbs += '/';
    return cleanAbs.startsWith(m_rootPath);
}

QFileInfo FileManager::getFileInfo(const QString &relativePath) const
{
    QString absPath = absolutePath(relativePath);
    if (!isValidPath(absPath))
        return QFileInfo();
    return QFileInfo(absPath);
}

QList<QFileInfo> FileManager::getDirectoryContents(const QString &relativePath) const
{
    QList<QFileInfo> result;
    QString absPath = absolutePath(relativePath);
    if (!isValidPath(absPath))
        return result;

    QDir dir(absPath);
    if (!dir.exists())
        return result;

    return dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
}

bool FileManager::createDirectory(const QString &relativePath)
{
    QString absPath = absolutePath(relativePath);
    if (!isValidPath(absPath))
        return false;

    QDir dir;
    return dir.mkpath(absPath);
}

bool FileManager::deleteResource(const QString &relativePath)
{
    QString absPath = absolutePath(relativePath);
    if (!isValidPath(absPath))
        return false;

    QFileInfo info(absPath);
    if (!info.exists())
        return true; // Уже удалено

    if (info.isDir()) {
        return deleteDirectoryRecursively(absPath);
    } else {
        return QFile::remove(absPath);
    }
}

bool FileManager::copyResource(const QString &sourceRelPath, const QString &destRelPath)
{
    QString srcAbs = absolutePath(sourceRelPath);
    QString dstAbs = absolutePath(destRelPath);
    if (!isValidPath(srcAbs) || !isValidPath(dstAbs))
        return false;

    QFileInfo srcInfo(srcAbs);
    if (!srcInfo.exists())
        return false;

    if (srcInfo.isDir()) {
        return copyDirectoryRecursively(srcAbs, dstAbs);
    } else {
        // Убедимся, что родительская директория существует
        QDir().mkpath(QFileInfo(dstAbs).absolutePath());
        return QFile::copy(srcAbs, dstAbs);
    }
}

bool FileManager::moveResource(const QString &sourceRelPath, const QString &destRelPath)
{
    QString srcAbs = absolutePath(sourceRelPath);
    QString dstAbs = absolutePath(destRelPath);
    if (!isValidPath(srcAbs) || !isValidPath(dstAbs))
        return false;

    // Простой случай: переименование в пределах одной файловой системы
    QDir().mkpath(QFileInfo(dstAbs).absolutePath());
    return QFile::rename(srcAbs, dstAbs);
}

bool FileManager::writeFile(const QString &relativePath, const QByteArray &data)
{
    QString absPath = absolutePath(relativePath);
    if (!isValidPath(absPath))
        return false;

    // Создаём родительские директории
    QDir().mkpath(QFileInfo(absPath).absolutePath());

    QFile file(absPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    qint64 written = file.write(data);
    file.close();
    return (written == data.size());
}

QByteArray FileManager::readFile(const QString &relativePath) const
{
    QString absPath = absolutePath(relativePath);
    if (!isValidPath(absPath))
        return QByteArray();

    QFile file(absPath);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();

    return file.readAll();
}

QString FileManager::mimeType(const QString &relativePath) const
{
    QString absPath = absolutePath(relativePath);
    if (!isValidPath(absPath))
        return QString();
    return MimeUtils::fromFileName(absPath);
}

// Приватные вспомогательные методы

bool FileManager::copyDirectoryRecursively(const QString &srcAbsPath, const QString &dstAbsPath)
{
    QDir srcDir(srcAbsPath);
    if (!srcDir.exists())
        return false;

    // Создаём целевую директорию
    QDir().mkpath(dstAbsPath);

    for (const QFileInfo &entry : srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
        QString dstEntryPath = dstAbsPath + '/' + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), dstEntryPath))
                return false;
        } else {
            if (!QFile::copy(entry.absoluteFilePath(), dstEntryPath))
                return false;
        }
    }
    return true;
}

bool FileManager::deleteDirectoryRecursively(const QString &absPath)
{
    QDir dir(absPath);
    if (!dir.exists())
        return true;

    bool success = true;
    for (const QFileInfo &entry : dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
        if (entry.isDir()) {
            success &= deleteDirectoryRecursively(entry.absoluteFilePath());
        } else {
            success &= QFile::remove(entry.absoluteFilePath());
        }
    }
    success &= dir.rmdir(absPath);
    return success;
}
#include "RecentFiles.h"
#include <QSettings>
#include <QFileInfo>

RecentFiles::RecentFiles(QObject* parent, int maxFiles)
    : QObject(parent)
    , m_maxFiles(maxFiles)
{
    load();
}

QStringList RecentFiles::files() const
{
    return m_files;
}

void RecentFiles::addFile(const QString& filePath)
{
    QString canonical = QFileInfo(filePath).absoluteFilePath();
    m_files.removeAll(canonical);
    m_files.prepend(canonical);
    while (m_files.size() > m_maxFiles)
        m_files.removeLast();
    save();
    emit changed();
}

void RecentFiles::clear()
{
    m_files.clear();
    save();
    emit changed();
}

void RecentFiles::load()
{
    QSettings settings;
    m_files = settings.value("RecentFiles/files").toStringList();
    // Remove entries for files that no longer exist
    m_files.erase(
        std::remove_if(m_files.begin(), m_files.end(),
                       [](const QString& f) { return !QFileInfo::exists(f); }),
        m_files.end());
}

void RecentFiles::save()
{
    QSettings settings;
    settings.setValue("RecentFiles/files", m_files);
}

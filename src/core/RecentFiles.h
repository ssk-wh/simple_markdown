#pragma once
#include <QObject>
#include <QStringList>

class RecentFiles : public QObject {
    Q_OBJECT
public:
    explicit RecentFiles(QObject* parent = nullptr, int maxFiles = 10);

    QStringList files() const;
    void addFile(const QString& filePath);
    void clear();

signals:
    void changed();

private:
    int m_maxFiles;
    QStringList m_files;

    void load();
    void save();
};

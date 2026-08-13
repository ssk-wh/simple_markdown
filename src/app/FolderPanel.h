// src/app/FolderPanel.h
//
// 左侧文件夹面板：单 QTreeView，多文件夹作为顶层节点
#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include "Theme.h"

class QTreeView;
class QStandardItemModel;
class QStandardItem;
class QLabel;
class QFileSystemWatcher;

class FolderPanel : public QWidget {
    Q_OBJECT
public:
    explicit FolderPanel(QWidget* parent = nullptr);

    void setRootPath(const QString& path);   // 兼容：清除已有，设置单个
    void addFolder(const QString& path);     // 追加文件夹节点
    void removeFolder(const QString& path);
    QString rootPath() const;
    QStringList rootPaths() const;
    void setTheme(const Theme& theme);
    void retranslateUi();
    void clearRoot();
    void selectFile(const QString& filePath);

signals:
    void fileClicked(const QString& filePath);
    void fileDoubleClicked(const QString& filePath);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void populateFolder(QStandardItem* folderItem, const QString& dirPath);
    void markFolderUnloaded(QStandardItem* folderItem);
    void watchDirectory(const QString& path);
    void removeWatchSubtree(const QString& path, bool keepRoot);
    void restoreExpandedDirectories(QStandardItem* parent, const QStringList& expandedPaths);
    QStringList expandedDirectoryPaths(QStandardItem* parent) const;
    QStandardItem* findDirectoryItem(const QString& path) const;
    QStandardItem* findDirectChild(QStandardItem* parent, const QString& path) const;
    void onItemClicked(const QModelIndex& index);
    void onItemDoubleClicked(const QModelIndex& index);
    void onItemExpanded(const QModelIndex& index);
    void onItemCollapsed(const QModelIndex& index);
    void onDirectoryChanged(const QString& path);
    void applyThemeStyles();

    // 右键菜单操作
    void newFile(const QString& parentDir);
    void newFolder(const QString& parentDir);
    void renameItem(const QString& path);
    void deleteItem(const QString& path);
    void revealInExplorer(const QString& path);

    QLabel* m_titleLabel = nullptr;
    QTreeView* m_treeView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    QStringList m_rootPaths;
    Theme m_theme;
};

// src/app/FolderPanel.cpp
//
// 单 QTreeView + QStandardItemModel，多文件夹作为顶层节点
#include "FolderPanel.h"

#include <QVBoxLayout>
#include <QTreeView>
#include <QStandardItemModel>
#include <QLabel>
#include <QFileSystemWatcher>
#include <QMenu>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QHeaderView>
#include <QCoreApplication>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyle>
#include <QFileIconProvider>

// 去掉 item 选中时的虚线焦点框
class NoFocusDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        opt.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

// 自定义角色：存储文件/文件夹完整路径
static constexpr int PathRole = Qt::UserRole + 1;
static constexpr int IsFolderRole = Qt::UserRole + 2;
static constexpr int IsRootRole = Qt::UserRole + 3;

FolderPanel::FolderPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 固定标题栏
    m_titleLabel = new QLabel(tr("Explorer"), this);
    m_titleLabel->setObjectName("folderTitle");
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setContentsMargins(8, 4, 8, 4);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 0.85);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    layout->addWidget(m_titleLabel);

    // 单一 TreeView
    m_model = new QStandardItemModel(this);
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setItemDelegate(new NoFocusDelegate(m_treeView));
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    m_treeView->setIndentation(16);
    m_treeView->setEditTriggers(QTreeView::NoEditTriggers);
    m_treeView->setContextMenuPolicy(Qt::DefaultContextMenu);

    connect(m_treeView, &QTreeView::clicked, this, &FolderPanel::onItemClicked);
    connect(m_treeView, &QTreeView::doubleClicked, this, &FolderPanel::onItemDoubleClicked);

    layout->addWidget(m_treeView, 1);

    // 文件系统监听
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &FolderPanel::onDirectoryChanged);

    setMinimumWidth(120);
    hide();
}

void FolderPanel::setRootPath(const QString& path)
{
    if (path.isEmpty()) { clearRoot(); return; }
    clearRoot();
    addFolder(path);
}

void FolderPanel::addFolder(const QString& path)
{
    if (path.isEmpty()) return;
    QString absPath = QFileInfo(path).absoluteFilePath();
    if (m_rootPaths.contains(absPath)) return;

    m_rootPaths.append(absPath);

    // 创建顶层节点
    auto* folderItem = new QStandardItem();
    folderItem->setText(QDir(absPath).dirName());
    folderItem->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    folderItem->setData(absPath, PathRole);
    folderItem->setData(true, IsFolderRole);
    folderItem->setData(true, IsRootRole);
    QFont f = folderItem->font();
    f.setBold(true);
    folderItem->setFont(f);
    folderItem->setEditable(false);

    m_model->appendRow(folderItem);
    populateFolder(folderItem, absPath);

    // 展开顶层节点
    m_treeView->expand(folderItem->index());

    // 监听目录变化
    m_watcher->addPath(absPath);

    show();
    applyThemeStyles();
}

void FolderPanel::removeFolder(const QString& path)
{
    QString absPath = QFileInfo(path).absoluteFilePath();
    int idx = m_rootPaths.indexOf(absPath);
    if (idx < 0) return;

    m_rootPaths.removeAt(idx);
    m_watcher->removePath(absPath);

    // 从 model 中删除对应顶层节点
    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto* item = m_model->item(i);
        if (item && item->data(PathRole).toString() == absPath) {
            m_model->removeRow(i);
            break;
        }
    }

    if (m_rootPaths.isEmpty()) hide();
}

QString FolderPanel::rootPath() const
{
    return m_rootPaths.isEmpty() ? QString() : m_rootPaths.first();
}

QStringList FolderPanel::rootPaths() const
{
    return m_rootPaths;
}

void FolderPanel::clearRoot()
{
    m_model->clear();
    if (!m_rootPaths.isEmpty())
        m_watcher->removePaths(m_rootPaths);
    m_rootPaths.clear();
    hide();
}

void FolderPanel::selectFile(const QString& filePath)
{
    if (m_rootPaths.isEmpty() || !isVisible()) return;
    QString absPath = QFileInfo(filePath).absoluteFilePath();

    // 递归搜索 model 中匹配的 item
    std::function<QModelIndex(QStandardItem*)> findItem = [&](QStandardItem* parent) -> QModelIndex {
        for (int i = 0; i < parent->rowCount(); ++i) {
            auto* child = parent->child(i);
            if (child->data(PathRole).toString() == absPath)
                return child->index();
            QModelIndex found = findItem(child);
            if (found.isValid()) return found;
        }
        return {};
    };

    for (int i = 0; i < m_model->rowCount(); ++i) {
        QModelIndex found = findItem(m_model->item(i));
        if (found.isValid()) {
            m_treeView->setCurrentIndex(found);
            m_treeView->scrollTo(found);
            return;
        }
    }
}

void FolderPanel::populateFolder(QStandardItem* folderItem, const QString& dirPath)
{
    folderItem->removeRows(0, folderItem->rowCount());

    QDir dir(dirPath);
    dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    QStringList nameFilters = {"*.md", "*.markdown", "*.txt"};

    QFileInfoList entries = dir.entryInfoList();
    for (const auto& fi : entries) {
        if (fi.isDir()) {
            auto* dirItem = new QStandardItem();
            dirItem->setText(fi.fileName());
            QFileIconProvider iconProvider;
            dirItem->setIcon(iconProvider.icon(fi));
            dirItem->setData(fi.absoluteFilePath(), PathRole);
            dirItem->setData(true, IsFolderRole);
            dirItem->setData(false, IsRootRole);
            dirItem->setEditable(false);
            folderItem->appendRow(dirItem);
            // 递归填充子目录
            populateFolder(dirItem, fi.absoluteFilePath());
            // 监听子目录
            m_watcher->addPath(fi.absoluteFilePath());
        } else {
            // 只显示匹配的文件
            bool match = false;
            for (const auto& filter : nameFilters) {
                if (QDir::match(filter, fi.fileName())) { match = true; break; }
            }
            if (!match) continue;

            auto* fileItem = new QStandardItem();
            fileItem->setText(fi.fileName());
            QFileIconProvider iconProvider;
            fileItem->setIcon(iconProvider.icon(fi));
            fileItem->setData(fi.absoluteFilePath(), PathRole);
            fileItem->setData(false, IsFolderRole);
            fileItem->setData(false, IsRootRole);
            fileItem->setEditable(false);
            folderItem->appendRow(fileItem);
        }
    }
}

void FolderPanel::onItemClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    auto* item = m_model->itemFromIndex(index);
    if (!item) return;
    bool isFolder = item->data(IsFolderRole).toBool();
    if (!isFolder) {
        emit fileClicked(item->data(PathRole).toString());
    }
}

void FolderPanel::onItemDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    auto* item = m_model->itemFromIndex(index);
    if (!item) return;
    bool isFolder = item->data(IsFolderRole).toBool();
    if (!isFolder) {
        emit fileDoubleClicked(item->data(PathRole).toString());
    }
}

void FolderPanel::onDirectoryChanged(const QString& path)
{
    // 找到对应的 model item 并重新填充
    std::function<QStandardItem*(QStandardItem*)> findDirItem = [&](QStandardItem* parent) -> QStandardItem* {
        if (parent->data(PathRole).toString() == path)
            return parent;
        for (int i = 0; i < parent->rowCount(); ++i) {
            auto* child = parent->child(i);
            if (child->data(IsFolderRole).toBool()) {
                auto* found = findDirItem(child);
                if (found) return found;
            }
        }
        return nullptr;
    };

    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto* found = findDirItem(m_model->item(i));
        if (found) {
            bool wasExpanded = m_treeView->isExpanded(found->index());
            populateFolder(found, path);
            if (wasExpanded)
                m_treeView->expand(found->index());
            return;
        }
    }
}

void FolderPanel::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(tr("Explorer"));
}

void FolderPanel::setTheme(const Theme& theme)
{
    m_theme = theme;
    applyThemeStyles();
}

void FolderPanel::applyThemeStyles()
{
    QString bg = m_theme.previewBg.name();
    QString fg = m_theme.previewFg.name();
    // 选中背景：基于链接色降低不透明度，亮暗主题均柔和
    QString selBg = QString("rgba(%1,%2,%3,%4)")
        .arg(m_theme.previewLink.red())
        .arg(m_theme.previewLink.green())
        .arg(m_theme.previewLink.blue())
        .arg(m_theme.isDark ? "0.30" : "0.15");
    QString selFg = fg;  // 选中时保持与正常文本相同的颜色
    QString hoverCss = m_theme.hoverBgCss();

    setStyleSheet(QString(
        "FolderPanel { background: %1; }"
        "FolderPanel QLabel { color: %2; }"
    ).arg(bg, fg));

    m_treeView->setStyleSheet(QString(
        "QTreeView { background: %1; color: %2; border: none; }"
        "QTreeView::item { padding: 2px 0; }"
        "QTreeView::item:selected { background: %3; color: %5; }"
        "QTreeView::item:selected:hover { background: %3; color: %5; }"
        "QTreeView::item:hover:!selected { background: %4; }"
        "QTreeView::branch { background: %1; }"
    ).arg(bg, fg, selBg, hoverCss, selFg));
}

void FolderPanel::contextMenuEvent(QContextMenuEvent* event)
{
    QModelIndex index = m_treeView->indexAt(m_treeView->viewport()->mapFrom(this, event->pos()));
    QString path;
    QString parentDir;

    if (index.isValid()) {
        auto* item = m_model->itemFromIndex(index);
        if (item) {
            path = item->data(PathRole).toString();
            bool isFolder = item->data(IsFolderRole).toBool();
            parentDir = isFolder ? path : QFileInfo(path).absolutePath();
        }
    }

    if (parentDir.isEmpty() && !m_rootPaths.isEmpty())
        parentDir = m_rootPaths.first();
    if (parentDir.isEmpty()) return;

    QMenu menu(this);

    menu.addAction(tr("New File..."), [this, parentDir]() { newFile(parentDir); });
    menu.addAction(tr("New Folder..."), [this, parentDir]() { newFolder(parentDir); });

    if (index.isValid()) {
        auto* item = m_model->itemFromIndex(index);
        bool isRoot = item && item->data(IsRootRole).toBool();

        menu.addSeparator();
        if (isRoot) {
            menu.addAction(tr("Close Folder"), [this, path]() { removeFolder(path); });
        } else {
            menu.addAction(tr("Rename..."), [this, path]() { renameItem(path); });
            menu.addAction(tr("Delete"), [this, path]() { deleteItem(path); });
        }

        menu.addSeparator();
#ifdef _WIN32
        menu.addAction(tr("Reveal in Explorer"), [this, path]() { revealInExplorer(path); });
#else
        menu.addAction(tr("Reveal in File Manager"), [this, path]() { revealInExplorer(path); });
#endif
    }

    menu.exec(event->globalPos());
}

void FolderPanel::newFile(const QString& parentDir)
{
    bool ok;
    QString name = QInputDialog::getText(this, tr("New File"),
                                          tr("File name:"), QLineEdit::Normal,
                                          QStringLiteral("untitled.md"), &ok);
    if (!ok || name.isEmpty()) return;
    if (!name.endsWith(".md") && !name.endsWith(".markdown"))
        name += ".md";

    QString fullPath = QDir(parentDir).filePath(name);
    QFile file(fullPath);
    if (file.exists()) {
        QMessageBox::warning(this, tr("Error"), tr("File already exists: %1").arg(name));
        return;
    }
    if (file.open(QIODevice::WriteOnly)) {
        file.close();
        emit fileClicked(fullPath);
    }
}

void FolderPanel::newFolder(const QString& parentDir)
{
    bool ok;
    QString name = QInputDialog::getText(this, tr("New Folder"),
                                          tr("Folder name:"), QLineEdit::Normal,
                                          tr("New Folder"), &ok);
    if (!ok || name.isEmpty()) return;

    QDir dir(parentDir);
    if (!dir.mkdir(name)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to create folder: %1").arg(name));
    }
}

void FolderPanel::renameItem(const QString& path)
{
    QFileInfo fi(path);
    bool ok;
    QString newName = QInputDialog::getText(this, tr("Rename"),
                                             tr("New name:"), QLineEdit::Normal,
                                             fi.fileName(), &ok);
    if (!ok || newName.isEmpty() || newName == fi.fileName()) return;

    QString newPath = fi.absolutePath() + "/" + newName;
    if (QFile::exists(newPath)) {
        QMessageBox::warning(this, tr("Error"), tr("A file with that name already exists."));
        return;
    }
    if (!QFile::rename(path, newPath)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to rename."));
    }
}

void FolderPanel::deleteItem(const QString& path)
{
    QFileInfo fi(path);
    QString msg = fi.isDir()
        ? tr("Delete folder \"%1\" and all its contents?").arg(fi.fileName())
        : tr("Delete file \"%1\"?").arg(fi.fileName());

    if (QMessageBox::question(this, tr("Confirm Delete"), msg) != QMessageBox::Yes)
        return;

    if (fi.isDir()) {
        QDir dir(path);
        dir.removeRecursively();
    } else {
        QFile::remove(path);
    }
}

void FolderPanel::revealInExplorer(const QString& path)
{
#ifdef _WIN32
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#elif defined(__APPLE__)
    QProcess::startDetached("open", {"-R", path});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

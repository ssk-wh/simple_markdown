// tests/app/FolderPanelResourceTest.cpp
//
// Spec: specs/模块-app/20-左侧面板.md
// 验收：T-11 ~ T-14（目录按需加载、监控有界、折叠清理、懒加载定位）

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include "FolderPanel.h"

namespace {

constexpr int PathRole = Qt::UserRole + 1;

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override
    {
        if (!QApplication::instance()) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static int argc = 1;
            static char arg0[] = "FolderPanelResourceTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }

private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

QString normalized(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

void createFile(const QString& path)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("# test\n");
}

QStandardItem* childByPath(QStandardItem* parent, const QString& path)
{
    const QString expected = normalized(path);
    for (int i = 0; i < parent->rowCount(); ++i) {
        auto* child = parent->child(i);
        if (child && normalized(child->data(PathRole).toString()) == expected)
            return child;
    }
    return nullptr;
}

class FolderPanelResourceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(temp_.isValid());
        root_ = temp_.path() + "/root";
        child_ = root_ + "/child";
        grandchild_ = child_ + "/grandchild";
        ASSERT_TRUE(QDir().mkpath(grandchild_));
        createFile(root_ + "/root.md");
        createFile(child_ + "/inside.md");
        deepFile_ = grandchild_ + "/deep.md";
        createFile(deepFile_);

        panel_.addFolder(root_);
        QApplication::processEvents();
        tree_ = panel_.findChild<QTreeView*>();
        watcher_ = panel_.findChild<QFileSystemWatcher*>();
        ASSERT_NE(tree_, nullptr);
        ASSERT_NE(watcher_, nullptr);
        model_ = qobject_cast<QStandardItemModel*>(tree_->model());
        ASSERT_NE(model_, nullptr);
        ASSERT_EQ(model_->rowCount(), 1);
    }

    QStandardItem* rootItem() const { return model_->item(0); }

    QTemporaryDir temp_;
    FolderPanel panel_;
    QTreeView* tree_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    QString root_;
    QString child_;
    QString grandchild_;
    QString deepFile_;
};

TEST_F(FolderPanelResourceTest, T11_RootLoadsOneLevelAndWatchesOnlyRoot)
{
    ASSERT_NE(childByPath(rootItem(), child_), nullptr);
    EXPECT_EQ(childByPath(rootItem(), grandchild_), nullptr);
    ASSERT_EQ(watcher_->directories().size(), 1);
    EXPECT_EQ(normalized(watcher_->directories().first()), normalized(root_));
}

TEST_F(FolderPanelResourceTest, T12_ExpandLoadsAndCollapseReleasesWatcher)
{
    auto* childItem = childByPath(rootItem(), child_);
    ASSERT_NE(childItem, nullptr);
    tree_->expand(childItem->index());
    QApplication::processEvents();

    EXPECT_EQ(watcher_->directories().size(), 2);
    ASSERT_NE(childByPath(childItem, grandchild_), nullptr);

    tree_->collapse(childItem->index());
    QApplication::processEvents();
    ASSERT_EQ(watcher_->directories().size(), 1);

    const QString addedWhileCollapsed = child_ + "/after-collapse.md";
    createFile(addedWhileCollapsed);
    tree_->expand(childItem->index());
    QApplication::processEvents();
    EXPECT_NE(childByPath(childItem, addedWhileCollapsed), nullptr);
    EXPECT_EQ(watcher_->directories().size(), 2);
}

TEST_F(FolderPanelResourceTest, T13_RemoveAndClearReleaseWholeWatchSubtree)
{
    auto* childItem = childByPath(rootItem(), child_);
    ASSERT_NE(childItem, nullptr);
    tree_->expand(childItem->index());
    QApplication::processEvents();
    auto* grandchildItem = childByPath(childItem, grandchild_);
    ASSERT_NE(grandchildItem, nullptr);
    tree_->expand(grandchildItem->index());
    QApplication::processEvents();
    ASSERT_EQ(watcher_->directories().size(), 3);

    panel_.removeFolder(root_);
    EXPECT_TRUE(watcher_->directories().isEmpty());

    panel_.addFolder(root_);
    QApplication::processEvents();
    panel_.clearRoot();
    EXPECT_TRUE(watcher_->directories().isEmpty());
}

TEST_F(FolderPanelResourceTest, T14_SelectFileLoadsAncestorChain)
{
    panel_.selectFile(deepFile_);
    QApplication::processEvents();

    ASSERT_TRUE(tree_->currentIndex().isValid());
    EXPECT_EQ(normalized(tree_->currentIndex().data(PathRole).toString()), normalized(deepFile_));
    EXPECT_EQ(watcher_->directories().size(), 3);
}

TEST_F(FolderPanelResourceTest, T15_ExpandedRootReflectsFilesystemChanges)
{
    const QString added = root_ + "/added.md";
    createFile(added);
    QTRY_VERIFY_WITH_TIMEOUT(childByPath(rootItem(), added) != nullptr, 3000);

    const QString renamed = root_ + "/renamed.md";
    ASSERT_TRUE(QFile::rename(added, renamed));
    QTRY_VERIFY_WITH_TIMEOUT(childByPath(rootItem(), added) == nullptr, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(childByPath(rootItem(), renamed) != nullptr, 3000);

    ASSERT_TRUE(QFile::remove(renamed));
    QTRY_VERIFY_WITH_TIMEOUT(childByPath(rootItem(), renamed) == nullptr, 3000);
    EXPECT_EQ(watcher_->directories().size(), 1);
}

TEST_F(FolderPanelResourceTest, T16_LargeSecondRootOnlyWatchesLoadedBranches)
{
    const QString secondRoot = temp_.path() + "/second-root";
    for (int i = 0; i < 100; ++i) {
        const QString branch = QString("/branch-%1/child").arg(i, 3, 10, QLatin1Char('0'));
        ASSERT_TRUE(QDir().mkpath(secondRoot + branch));
    }

    panel_.addFolder(secondRoot);
    QApplication::processEvents();

    ASSERT_EQ(model_->rowCount(), 2);
    EXPECT_EQ(watcher_->directories().size(), 2);
    EXPECT_EQ(model_->item(1)->rowCount(), 100);
    EXPECT_NE(childByPath(model_->item(1), secondRoot + "/branch-000"), nullptr);
    EXPECT_EQ(childByPath(model_->item(1), secondRoot + "/branch-000/child"), nullptr);
}

}  // namespace

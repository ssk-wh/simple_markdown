// tests/preview/TocPanelTest.cpp
//
// Spec: specs/模块-preview/07-TOC面板.md
// 验收：
//   T-COLLAPSE-1  h1..h4 多层嵌套可折叠
//   T-COLLAPSE-5  折叠项不参与宽度公式
//   T-WIDTH-1/T-WIDTH-2  preferredWidth 按标题长度调整
#include <gtest/gtest.h>
#include <QApplication>
#include <QVector>

#include "preview/TocPanel.h"

namespace {

// gtest 主线程 QApplication 守护
class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "TocPanelTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override {}
private:
    QApplication* app_ = nullptr;
};

QVector<TocEntry> makeEntries(std::initializer_list<std::pair<int, QString>> levels)
{
    QVector<TocEntry> out;
    int line = 0;
    for (auto& p : levels) {
        TocEntry e;
        e.level = p.first;
        e.title = p.second;
        e.sourceLine = line++;
        out.append(e);
    }
    return out;
}

}  // namespace

// ---- INV-TOC-COLLAPSE: parent/child 关系 ----

TEST(TocPanelCollapseTest, ParentIndexH1H2H3)
{
    TocPanel panel;
    auto entries = makeEntries({
        {1, "A"},
        {2, "A.1"},
        {3, "A.1.1"},
        {2, "A.2"},
        {1, "B"},
    });
    panel.setEntries(entries);

    // 期望 parentIndex: [-1, 0, 1, 0, -1]
    const auto& p = panel.parentIndexForTest();
    ASSERT_EQ(p.size(), 5);
    EXPECT_EQ(p[0], -1);
    EXPECT_EQ(p[1], 0);
    EXPECT_EQ(p[2], 1);
    EXPECT_EQ(p[3], 0);
    EXPECT_EQ(p[4], -1);

    // 初始全部可见
    for (int i = 0; i < entries.size(); ++i)
        EXPECT_TRUE(panel.isEntryVisibleForTest(i)) << "entry " << i << " should be visible initially";
}

TEST(TocPanelCollapseTest, HasChildren)
{
    TocPanel panel;
    panel.setEntries(makeEntries({
        {1, "A"},
        {2, "A.1"},
        {3, "A.1.1"},
        {2, "A.2"},
        {1, "B"},
    }));

    EXPECT_TRUE(panel.hasChildrenForTest(0));   // A has A.1, A.2
    EXPECT_TRUE(panel.hasChildrenForTest(1));   // A.1 has A.1.1
    EXPECT_FALSE(panel.hasChildrenForTest(2));  // A.1.1 leaf
    EXPECT_FALSE(panel.hasChildrenForTest(3));  // A.2 leaf
    EXPECT_FALSE(panel.hasChildrenForTest(4));  // B leaf
}

// ---- T-COLLAPSE-1: 折叠 parent 隐藏后代 ----

TEST(TocPanelCollapseTest, CollapseHidesDescendants)
{
    TocPanel panel;
    panel.setEntries(makeEntries({
        {1, "A"},
        {2, "A.1"},
        {3, "A.1.1"},
        {2, "A.2"},
        {1, "B"},
    }));

    // 折叠 A（idx=0），其后代 A.1, A.1.1, A.2 应隐藏
    panel.toggleCollapseForTest(0);
    EXPECT_TRUE(panel.isEntryVisibleForTest(0));   // A 自身可见
    EXPECT_FALSE(panel.isEntryVisibleForTest(1));  // A.1
    EXPECT_FALSE(panel.isEntryVisibleForTest(2));  // A.1.1
    EXPECT_FALSE(panel.isEntryVisibleForTest(3));  // A.2
    EXPECT_TRUE(panel.isEntryVisibleForTest(4));   // B 不受影响

    // 再次 toggle 展开
    panel.toggleCollapseForTest(0);
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(panel.isEntryVisibleForTest(i));
}

TEST(TocPanelCollapseTest, CollapseSecondLevelOnly)
{
    TocPanel panel;
    panel.setEntries(makeEntries({
        {1, "A"},
        {2, "A.1"},
        {3, "A.1.1"},
        {3, "A.1.2"},
        {2, "A.2"},
    }));

    // 折叠 A.1（idx=1），其子 A.1.1, A.1.2 应隐藏；A 和 A.2 可见
    panel.toggleCollapseForTest(1);
    EXPECT_TRUE(panel.isEntryVisibleForTest(0));
    EXPECT_TRUE(panel.isEntryVisibleForTest(1));
    EXPECT_FALSE(panel.isEntryVisibleForTest(2));
    EXPECT_FALSE(panel.isEntryVisibleForTest(3));
    EXPECT_TRUE(panel.isEntryVisibleForTest(4));
}

// ---- T-WIDTH-1 / T-WIDTH-2: 宽度自适应 ----

TEST(TocPanelWidthTest, EmptyEntriesReturnsMinWidth)
{
    TocPanel panel;
    panel.setEntries({});
    EXPECT_GE(panel.preferredWidth(), 120);
    EXPECT_LE(panel.preferredWidth(), 400);
}

TEST(TocPanelWidthTest, LongTitleExpandsWidth)
{
    TocPanel panel;
    panel.setEntries(makeEntries({{1, "Short"}}));
    const int shortW = panel.preferredWidth();

    panel.setEntries(makeEntries({
        {1, "This is a very long heading that clearly needs more width than a short one"},
    }));
    const int longW = panel.preferredWidth();

    EXPECT_GT(longW, shortW) << "longW=" << longW << " shortW=" << shortW;
}

// ---- T-COLLAPSE-5: 折叠子节点不参与宽度计算 ----

TEST(TocPanelWidthTest, CollapsedChildrenDoNotAffectWidth)
{
    TocPanel panel;
    panel.setEntries(makeEntries({
        {1, "A"},
        {2, "A superduper extremely long child heading that would otherwise dominate width"},
    }));
    const int expandedW = panel.preferredWidth();

    panel.toggleCollapseForTest(0);  // 折叠 A，隐藏长子标题
    const int collapsedW = panel.preferredWidth();

    EXPECT_LT(collapsedW, expandedW)
        << "collapsedW=" << collapsedW << " expandedW=" << expandedW;
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new QAppFixture);
    return RUN_ALL_TESTS();
}

// tests/app/TabBarAddButtonVisibilityTest.cpp
//
// Spec: specs/模块-app/04-窗口焦点管理.md — INV-6（「+」按钮独立于 Tab 滚动，始终可见可点）
// Bug: plans/2026-06-16-多标签时新建按钮被挤不可见.md
// 验收：
//   T-11：Tab 数量多到溢出 Tab 栏后，「新建」(+) 按钮仍然可见
//   T-12：Tab 溢出时「+」按钮左边界 ≥ Tab 区右边界（按钮与 Tab 区不重叠）
//
// 历史 bug：早期「+」自绘在「最后一个 Tab 右侧」，Tab 溢出触发 QTabBar 横向滚动后，
// 最后一个 Tab（连同「+」）被滚出可视区，用户看不到新建按钮、只能先关 Tab。
// 容器化（QTabBar + QToolButton 水平布局）后按钮与滚动解耦，始终钉在右端。

#include <gtest/gtest.h>

#include <QApplication>
#include <QTabBar>
#include <QTest>
#include <QToolButton>

#include "TabBarWithAdd.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "TabBarAddButtonVisibilityTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override {}

private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

// 加 n 个标题足够长的 tab，使 tab 总宽超过窄 tab 栏宽度，复现溢出场景。
void fillTabs(QTabBar* bar, int n)
{
    for (int i = 0; i < n; ++i)
        bar->addTab(QStringLiteral("文档标题 document-%1.md").arg(i));
}

}  // namespace

// T-11 + T-12：tab 溢出时按钮仍可见，且不与 tab 区重叠。
TEST(TabBarAddButtonVisibilityTest, T11_T12_AddButtonStaysVisibleWhenTabsOverflow)
{
    TabBarWithAdd box;
    box.bar()->setExpanding(false);  // 对齐 MainWindow 配置
    box.resize(300, 36);             // 故意窄，确保 30 个 tab 溢出
    box.show();
    QTest::qWaitForWindowExposed(&box);

    fillTabs(box.bar(), 30);
    box.resize(300, 36);
    QApplication::processEvents();

    QTabBar* bar = box.bar();
    QToolButton* btn = box.addButton();
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(btn, nullptr);

    // 前置：确认 tab 确实溢出了（最后一个 tabRect 右边界远超 tab 区可见宽度，
    // 说明触发了横向滚动/裁剪——正是历史 bug 会把「+」挤走的场景）。
    const QRect lastTab = bar->tabRect(bar->count() - 1);
    EXPECT_GT(lastTab.right(), bar->width())
        << "测试前置失效：tab 未溢出，需加更多/更宽 tab 才能复现 bug 场景。";

    // T-11：按钮仍可见。
    EXPECT_TRUE(btn->isVisible())
        << "INV-6：Tab 溢出时「新建」按钮必须保持可见（不得随 tab 滚动被挤出可视区）。";

    // T-12：按钮左边界 ≥ tab 区右边界（同一父坐标系，spacing=0，允许 1px 容差）。
    EXPECT_GE(btn->geometry().left(), bar->geometry().right() - 1)
        << "INV-6：「+」按钮不得与 Tab 区重叠，应固定在 Tab 区右侧。";

    // 按钮整体在容器可视范围内（钉在右端而非自身溢出容器）。
    EXPECT_LE(btn->geometry().right(), box.width())
        << "INV-6：「+」按钮右边界应落在容器宽度内。";
    EXPECT_GE(btn->geometry().left(), 0);
}

// 补充：tab 很少（不溢出）时按钮同样可见，且紧随 tab 区右侧。
TEST(TabBarAddButtonVisibilityTest, FewTabs_AddButtonVisibleAndAdjacent)
{
    TabBarWithAdd box;
    box.bar()->setExpanding(false);
    box.resize(800, 36);
    box.show();
    QTest::qWaitForWindowExposed(&box);

    fillTabs(box.bar(), 2);
    QApplication::processEvents();

    EXPECT_TRUE(box.addButton()->isVisible());
    EXPECT_GE(box.addButton()->geometry().left(),
              box.bar()->geometry().right() - 1);
}

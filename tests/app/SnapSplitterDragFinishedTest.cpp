// tests/app/SnapSplitterDragFinishedTest.cpp
//
// Spec: specs/模块-app/13-分隔条吸附刻度.md
// Plan: plans/2026-05-07-拖拽分割线两侧全文重排.md
// 验收：T15（INV-SNAP-LAZY-PANE-REBUILD）
//   - mouse press 后 splitter->property("smSnapDragging") == true
//   - mouse press 后 splitter->isDragging() == true
//   - mouse release 后 splitter->property("smSnapDragging") == false
//   - mouse release 后 dragFinished() 信号 emit 一次（仅一次，不会在拖拽过程中连续 emit）
//   - dragFinished slot 内可观察到 property == false（INV-SNAP-LAZY-PANE-REBUILD 实现细节：
//     property 在 emit 前清，让 slot 里的子 pane rebuild 走"非拖拽路径"）

#include <gtest/gtest.h>

#include <QApplication>
#include <QSignalSpy>
#include <QSplitterHandle>
#include <QTest>
#include <QWidget>

#include "SnapSplitter.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "SnapSplitterDragFinishedTest";
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

}  // namespace

TEST(SnapSplitterDragTest, T15_PropertyAndSignalLifecycle)
{
    using namespace app;

    SnapSplitter splitter(Qt::Horizontal);
    auto* left = new QWidget();
    auto* right = new QWidget();
    splitter.addWidget(left);
    splitter.addWidget(right);
    splitter.resize(800, 400);
    splitter.show();
    QTest::qWaitForWindowExposed(&splitter);

    QSignalSpy finishedSpy(&splitter, &SnapSplitter::dragFinished);

    // 初始态：未拖拽
    EXPECT_FALSE(splitter.isDragging());
    EXPECT_FALSE(splitter.property("smSnapDragging").toBool());
    EXPECT_EQ(finishedSpy.count(), 0);

    // 拿到 editor|preview 之间的 handle (index 1；index 0 是首个 pane 左边的虚拟 handle)
    QSplitterHandle* handle = splitter.handle(1);
    ASSERT_NE(handle, nullptr);

    // 模拟 mouse press → 进入拖拽态
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier,
                      QPoint(handle->width() / 2, handle->height() / 2));

    EXPECT_TRUE(splitter.isDragging());
    EXPECT_TRUE(splitter.property("smSnapDragging").toBool());
    EXPECT_EQ(finishedSpy.count(), 0);  // 拖拽中不 emit

    // 模拟 mouse release → 退出拖拽态 + emit dragFinished
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier,
                        QPoint(handle->width() / 2, handle->height() / 2));

    EXPECT_FALSE(splitter.isDragging());
    EXPECT_FALSE(splitter.property("smSnapDragging").toBool());
    EXPECT_EQ(finishedSpy.count(), 1);  // 松手 emit 一次
}

// INV-SNAP-LAZY-PANE-REBUILD 实现细节：endDrag 在 emit dragFinished 之前
// 必须先清 property——否则连接到 dragFinished 的 slot 内 child widget 调
// rebuildLayout / recomputeWrapForCurrentWidth 时，其 resizeEvent 路径会再次
// 看到 property == true 而跳过重排，导致最终态对不齐。
TEST(SnapSplitterDragTest, T15_PropertyClearedBeforeSignalEmit)
{
    using namespace app;

    SnapSplitter splitter(Qt::Horizontal);
    splitter.addWidget(new QWidget());
    splitter.addWidget(new QWidget());
    splitter.resize(800, 400);
    splitter.show();
    QTest::qWaitForWindowExposed(&splitter);

    bool propertyValueObservedInSlot = true;  // 默认假定为 true，slot 跑后改为实际值
    bool slotRan = false;
    QObject::connect(&splitter, &SnapSplitter::dragFinished, [&]() {
        slotRan = true;
        propertyValueObservedInSlot =
            splitter.property("smSnapDragging").toBool();
    });

    QSplitterHandle* handle = splitter.handle(1);
    ASSERT_NE(handle, nullptr);

    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier,
                      QPoint(handle->width() / 2, handle->height() / 2));
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier,
                        QPoint(handle->width() / 2, handle->height() / 2));

    EXPECT_TRUE(slotRan);
    EXPECT_FALSE(propertyValueObservedInSlot)
        << "endDrag 必须先清 smSnapDragging property，再 emit dragFinished——"
           "否则 slot 内子 pane rebuild 会走拖拽态分支再次跳过重排，"
           "导致松手后内容残留旧布局。";
}

// Spec: specs/模块-core/README.md
// Last synced: 2026-06-24
//
// UndoStack 核心契约测试 + 边界场景。
// T-US-1 ~ T-US-11

#include <gtest/gtest.h>
#include "UndoStack.h"

// T-US-1：空栈 undo/redo 安全
TEST(UndoStackTest, T_US1_EmptyStackSafe)
{
    UndoStack s;
    EXPECT_FALSE(s.canUndo());
    EXPECT_FALSE(s.canRedo());
}

// T-US-2：基本 push / undo / redo 往返
TEST(UndoStackTest, T_US2_PushUndoRedo)
{
    UndoStack s;
    s.push(0, "hello", "");
    EXPECT_TRUE(s.canUndo());
    auto op = s.undo();
    EXPECT_EQ(op.offset, 0);
    EXPECT_EQ(op.removedText.toStdString(), "hello");
    EXPECT_TRUE(s.canRedo());

    op = s.redo();
    EXPECT_EQ(op.offset, 0);
    EXPECT_EQ(op.removedText.toStdString(), "hello");
    EXPECT_FALSE(s.canRedo());
}

// T-US-3：多步 undo 栈顺序
TEST(UndoStackTest, T_US3_MultiStepUndoOrder)
{
    UndoStack s;
    s.setMergeInterval(0);
    s.push(0, "", "a");
    s.push(1, "", "b");
    s.push(2, "", "c");

    EXPECT_EQ(s.undo().addedText.toStdString(), "c");
    EXPECT_EQ(s.undo().addedText.toStdString(), "b");
    EXPECT_EQ(s.undo().addedText.toStdString(), "a");
    EXPECT_FALSE(s.canUndo());
}

// T-US-4：新建编辑清空重做栈
TEST(UndoStackTest, T_US4_NewEditClearsRedo)
{
    UndoStack s;
    s.push(0, "", "a");
    s.undo();
    EXPECT_TRUE(s.canRedo());
    s.push(0, "", "b");
    EXPECT_FALSE(s.canRedo());
}

// T-US-5：保存点跟踪
TEST(UndoStackTest, T_US5_SavePointTracking)
{
    UndoStack s;
    EXPECT_TRUE(s.isAtSavePoint());
    s.push(0, "", "a");
    EXPECT_FALSE(s.isAtSavePoint());
    s.setSavePoint();
    EXPECT_TRUE(s.isAtSavePoint());
    s.undo();
    EXPECT_FALSE(s.isAtSavePoint());
    s.redo();
    EXPECT_TRUE(s.isAtSavePoint());
}

// T-US-6：合并连续键入
TEST(UndoStackTest, T_US6_MergeTyping)
{
    UndoStack s;
    s.setMergeInterval(1000000);
    s.push(0, "", "a");
    s.push(1, "", "b");
    s.push(2, "", "c");

    // 三次键入应合并为一次
    auto op = s.undo();
    EXPECT_EQ(op.addedText.toStdString(), "abc");
    EXPECT_FALSE(s.canUndo());
}

// T-US-7：合并连续退格
TEST(UndoStackTest, T_US7_MergeBackspace)
{
    UndoStack s;
    s.setMergeInterval(1000000);
    s.push(3, "c", "");
    s.push(2, "b", "");
    s.push(1, "a", "");

    auto op = s.undo();
    EXPECT_EQ(op.removedText.toStdString(), "abc");
    EXPECT_EQ(op.offset, 1);
}

// T-US-8：换行和空格不合并前续字符
TEST(UndoStackTest, T_US8_NewlineAndSpaceNoMerge)
{
    UndoStack s;
    s.setMergeInterval(1000000);
    s.push(0, "", "a");
    s.push(1, "", " ");
    s.push(2, "", "\n");

    // 空格和换行各自不合并，栈应有 3 个条目
    EXPECT_EQ(s.undo().addedText.toStdString(), "\n");
    EXPECT_EQ(s.undo().addedText.toStdString(), " ");
    EXPECT_EQ(s.undo().addedText.toStdString(), "a");
    EXPECT_FALSE(s.canUndo());
}

// T-US-9：clear 重置全部状态
TEST(UndoStackTest, T_US9_ClearResetsAll)
{
    UndoStack s;
    s.push(0, "", "a");
    s.setSavePoint();
    s.clear();
    EXPECT_FALSE(s.canUndo());
    EXPECT_FALSE(s.canRedo());
    EXPECT_TRUE(s.isAtSavePoint());
}

// T-US-10：空字符串 push 不影响栈
TEST(UndoStackTest, T_US10_EmptyPushNoop)
{
    UndoStack s;
    s.push(0, "", "");
    EXPECT_FALSE(s.canUndo());
    s.push(1, "x", "");
    EXPECT_TRUE(s.canUndo());
    s.push(0, "", "y");
    EXPECT_TRUE(s.canUndo());
    // 只有纯删除或纯插入才生成有效操作
    s.undo();
    s.undo();
    EXPECT_FALSE(s.canUndo());
}

// T-US-11：保存点标记后新编辑不合并跨越保存点
TEST(UndoStackTest, T_US11_NoMergeAcrossSavePoint)
{
    UndoStack s;
    s.setMergeInterval(1000000);
    s.push(0, "", "a");
    s.setSavePoint();
    s.push(1, "", "b");

    EXPECT_EQ(s.undo().addedText.toStdString(), "b");
    EXPECT_EQ(s.undo().addedText.toStdString(), "a");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

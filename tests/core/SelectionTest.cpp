// Spec: specs/模块-core/README.md
// Last synced: 2026-06-24
//
// Selection / TextPosition / SelectionRange 核心契约测试。
// T-SEL-1 ~ T-SEL-8

#include <gtest/gtest.h>
#include "Selection.h"

// T-SEL-1：TextPosition 比较
TEST(SelectionTest, T_SEL1_TextPositionComparison)
{
    TextPosition a{0, 0};
    TextPosition b{0, 5};
    TextPosition c{1, 0};
    TextPosition d{0, 0};

    EXPECT_TRUE(a == d);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b < c);
    EXPECT_TRUE(a <= d);
    EXPECT_TRUE(c > b);
    EXPECT_TRUE(b >= a);
}

// T-SEL-2：空选区
TEST(SelectionTest, T_SEL2_EmptySelection)
{
    Selection sel;
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_TRUE(sel.range().isEmpty());
}

// T-SEL-3：设置选区
TEST(SelectionTest, T_SEL3_SetSelection)
{
    Selection sel;
    sel.setCursorPosition({0, 10});
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_EQ(sel.cursorPosition().line, 0);
    EXPECT_EQ(sel.cursorPosition().column, 10);

    sel.setSelection({0, 5}, {0, 10});
    EXPECT_TRUE(sel.hasSelection());
    EXPECT_EQ(sel.range().anchor.line, 0);
    EXPECT_EQ(sel.range().anchor.column, 5);
    EXPECT_EQ(sel.range().cursor.column, 10);
}

// T-SEL-4：清除选区
TEST(SelectionTest, T_SEL4_ClearSelection)
{
    Selection sel;
    sel.setSelection({0, 0}, {0, 10});
    EXPECT_TRUE(sel.hasSelection());

    sel.clearSelection();
    EXPECT_FALSE(sel.hasSelection());
}

// T-SEL-5：扩展选区至空 → 选区清空
TEST(SelectionTest, T_SEL5_ExtendToEmpty)
{
    Selection sel;
    sel.setSelection({0, 0}, {0, 10});
    sel.extendSelection({0, 0});
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_TRUE(sel.range().isEmpty());
    EXPECT_EQ(sel.range().anchor.line, 0);
    EXPECT_EQ(sel.range().anchor.column, 0);
}

// T-SEL-6：选区方向
TEST(SelectionTest, T_SEL6_SelectionDirection)
{
    Selection sel;
    sel.setSelection({0, 10}, {0, 5});
    EXPECT_TRUE(sel.range().isForward() == false);

    sel.setSelection({0, 5}, {0, 10});
    EXPECT_TRUE(sel.range().isForward());
}

// T-SEL-7：start / end 按方向返回
TEST(SelectionTest, T_SEL7_StartEnd)
{
    SelectionRange r1{{0, 10}, {0, 5}};
    EXPECT_EQ(r1.start().column, 5);
    EXPECT_EQ(r1.end().column, 10);

    SelectionRange r2{{0, 5}, {0, 10}};
    EXPECT_EQ(r2.start().column, 5);
    EXPECT_EQ(r2.end().column, 10);
}

// T-SEL-8：preferredColumn
TEST(SelectionTest, T_SEL8_PreferredColumn)
{
    Selection sel;
    EXPECT_EQ(sel.preferredColumn(), -1);

    sel.setPreferredColumn(15);
    EXPECT_EQ(sel.preferredColumn(), 15);

    sel.resetPreferredColumn();
    EXPECT_EQ(sel.preferredColumn(), -1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

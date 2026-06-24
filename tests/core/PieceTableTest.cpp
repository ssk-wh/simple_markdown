// Spec: specs/模块-core/README.md
// Last synced: 2026-06-24
//
// PieceTable 核心契约测试 + 边界场景。
// T-PT-1 ~ T-PT-15

#include <gtest/gtest.h>
#include "PieceTable.h"

// T-PT-1：空表
TEST(PieceTableTest, T_PT1_Empty)
{
    PieceTable pt;
    EXPECT_TRUE(pt.isEmpty());
    EXPECT_EQ(pt.length(), 0);
    EXPECT_EQ(pt.text().toStdString(), "");
    EXPECT_EQ(pt.lineCount(), 1);
}

// T-PT-2：构造初始化文本
TEST(PieceTableTest, T_PT2_InitialText)
{
    PieceTable pt("hello\nworld\n");
    EXPECT_FALSE(pt.isEmpty());
    EXPECT_EQ(pt.length(), 12);
    EXPECT_EQ(pt.text().toStdString(), "hello\nworld\n");
    EXPECT_EQ(pt.lineCount(), 3);
}

// T-PT-3：在开头插入
TEST(PieceTableTest, T_PT3_InsertAtStart)
{
    PieceTable pt("world\n");
    pt.insert(0, "hello ");
    EXPECT_EQ(pt.text().toStdString(), "hello world\n");
}

// T-PT-4：在末尾插入
TEST(PieceTableTest, T_PT4_InsertAtEnd)
{
    PieceTable pt("hello");
    pt.insert(5, " world");
    EXPECT_EQ(pt.text().toStdString(), "hello world");
}

// T-PT-5：在中间插入
TEST(PieceTableTest, T_PT5_InsertInMiddle)
{
    PieceTable pt("hello world");
    pt.insert(5, " beautiful");
    EXPECT_EQ(pt.text().toStdString(), "hello beautiful world");
}

// T-PT-6：插入空字符串无影响
TEST(PieceTableTest, T_PT6_InsertEmptyNoop)
{
    PieceTable pt("hello");
    pt.insert(3, "");
    EXPECT_EQ(pt.text().toStdString(), "hello");
}

// T-PT-7：从开头删除
TEST(PieceTableTest, T_PT7_RemoveFromStart)
{
    PieceTable pt("hello world");
    pt.remove(0, 6);
    EXPECT_EQ(pt.text().toStdString(), "world");
}

// T-PT-8：从中间删除
TEST(PieceTableTest, T_PT8_RemoveFromMiddle)
{
    PieceTable pt("hello beautiful world");
    pt.remove(6, 10);
    EXPECT_EQ(pt.text().toStdString(), "hello world");
}

// T-PT-9：删除长度为零无影响
TEST(PieceTableTest, T_PT9_RemoveZeroNoop)
{
    PieceTable pt("hello");
    pt.remove(2, 0);
    EXPECT_EQ(pt.text().toStdString(), "hello");
}

// T-PT-10：删除超出末尾截断到末尾
TEST(PieceTableTest, T_PT10_RemoveBeyondEnd)
{
    PieceTable pt("hello");
    pt.remove(3, 100);
    EXPECT_EQ(pt.text().toStdString(), "hel");
}

// T-PT-11：替换操作
TEST(PieceTableTest, T_PT11_Replace)
{
    PieceTable pt("hello world");
    pt.replace(6, 5, "there");
    EXPECT_EQ(pt.text().toStdString(), "hello there");
}

// T-PT-12：多次插入后文本正确
TEST(PieceTableTest, T_PT12_MultipleInserts)
{
    PieceTable pt("world");
    pt.insert(0, "hello ");
    pt.insert(11, "!!!");
    EXPECT_EQ(pt.text().toStdString(), "hello world!!!");
}

// T-PT-13：多次插入删除混合
TEST(PieceTableTest, T_PT13_MixedInsertRemove)
{
    PieceTable pt("abcdef");
    pt.remove(1, 2);   // "adef" (remove bc)
    pt.insert(2, "X"); // "adXef"
    pt.insert(0, "Z"); // "ZadXef"
    EXPECT_EQ(pt.text().toStdString(), "ZadXef");
}

// T-PT-14：textAt 子串
TEST(PieceTableTest, T_PT14_TextAt)
{
    PieceTable pt("hello world");
    // 插入后再查询跨 piece 子串
    pt.insert(5, " AB");
    EXPECT_EQ(pt.textAt(6, 4).toStdString(), "AB w");
    EXPECT_EQ(pt.textAt(0, 5).toStdString(), "hello");
    EXPECT_EQ(pt.textAt(0, 100).toStdString(), "hello AB world");

    // 空查询
    EXPECT_EQ(pt.textAt(2, 0).toStdString(), "");
}

// T-PT-15：行操作
TEST(PieceTableTest, T_PT15_LineOperations)
{
    PieceTable pt("line0\nline1\nline2\nline3\n");
    EXPECT_EQ(pt.lineCount(), 5);
    EXPECT_EQ(pt.lineText(0).toStdString(), "line0");
    EXPECT_EQ(pt.lineText(1).toStdString(), "line1");
    EXPECT_EQ(pt.lineText(3).toStdString(), "line3");

    // offsetToLine / lineToOffset
    EXPECT_EQ(pt.offsetToLine(0), 0);
    EXPECT_EQ(pt.offsetToLine(6), 1);
    EXPECT_EQ(pt.offsetToLine(12), 2);
    EXPECT_EQ(pt.lineToOffset(1), 6);
    EXPECT_EQ(pt.lineToOffset(3), 18);

    // 越界行
    EXPECT_EQ(pt.lineText(10).toStdString(), "");
}

// T-PT-16：跨多 piece 删除
TEST(PieceTableTest, T_PT16_RemoveAcrossPieces)
{
    PieceTable pt("abc");
    pt.insert(1, "123");   // "a123bc": pieces [Original: a, Add: 123, Original: bc]
    pt.insert(4, "XYZ");   // "a123XYZbc"
    pt.remove(1, 6);       // remove "123XYZ"
    EXPECT_EQ(pt.text().toStdString(), "abc");
}

// T-PT-17：空构造 + insert 后不崩溃
TEST(PieceTableTest, T_PT17_EmptyThenInsert)
{
    PieceTable pt;
    pt.insert(0, "hello");
    EXPECT_EQ(pt.text().toStdString(), "hello");
    EXPECT_EQ(pt.length(), 5);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

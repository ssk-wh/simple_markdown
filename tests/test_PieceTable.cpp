#include <gtest/gtest.h>
#include "PieceTable.h"

TEST(PieceTable, EmptyConstruction) {
    PieceTable pt;
    EXPECT_EQ(pt.length(), 0);
    EXPECT_TRUE(pt.isEmpty());
    EXPECT_EQ(pt.text(), "");
    EXPECT_EQ(pt.lineCount(), 1);
}

TEST(PieceTable, ConstructWithText) {
    PieceTable pt("Hello");
    EXPECT_EQ(pt.length(), 5);
    EXPECT_EQ(pt.text(), "Hello");
}

TEST(PieceTable, InsertAtBeginning) {
    PieceTable pt("World");
    pt.insert(0, "Hello ");
    EXPECT_EQ(pt.text(), "Hello World");
}

TEST(PieceTable, InsertAtEnd) {
    PieceTable pt("Hello");
    pt.insert(5, " World");
    EXPECT_EQ(pt.text(), "Hello World");
}

TEST(PieceTable, InsertInMiddle) {
    PieceTable pt("HellWorld");
    pt.insert(4, "o ");
    EXPECT_EQ(pt.text(), "Hello World");
}

TEST(PieceTable, InsertIntoEmpty) {
    PieceTable pt;
    pt.insert(0, "Hello");
    EXPECT_EQ(pt.text(), "Hello");
}

TEST(PieceTable, MultipleInserts) {
    PieceTable pt;
    pt.insert(0, "A");
    pt.insert(1, "C");
    pt.insert(1, "B");
    EXPECT_EQ(pt.text(), "ABC");
}

TEST(PieceTable, RemoveFromBeginning) {
    PieceTable pt("Hello World");
    pt.remove(0, 6);
    EXPECT_EQ(pt.text(), "World");
}

TEST(PieceTable, RemoveFromEnd) {
    PieceTable pt("Hello World");
    pt.remove(5, 6);
    EXPECT_EQ(pt.text(), "Hello");
}

TEST(PieceTable, RemoveFromMiddle) {
    PieceTable pt("Hello World");
    pt.remove(5, 1);
    EXPECT_EQ(pt.text(), "HelloWorld");
}

TEST(PieceTable, RemoveAll) {
    PieceTable pt("Hello");
    pt.remove(0, 5);
    EXPECT_EQ(pt.text(), "");
    EXPECT_TRUE(pt.isEmpty());
}

TEST(PieceTable, RemoveAcrossPieces) {
    PieceTable pt("ABCDE");
    pt.insert(2, "XY");
    pt.remove(1, 4);
    EXPECT_EQ(pt.text(), "ADE");
}

TEST(PieceTable, ReplaceText) {
    PieceTable pt("Hello World");
    pt.replace(6, 5, "Earth");
    EXPECT_EQ(pt.text(), "Hello Earth");
}

TEST(PieceTable, ReplaceWithDifferentLength) {
    PieceTable pt("abc");
    pt.replace(1, 1, "XYZ");
    EXPECT_EQ(pt.text(), "aXYZc");
}

TEST(PieceTable, TextAtSubstring) {
    PieceTable pt("Hello World");
    EXPECT_EQ(pt.textAt(6, 5), "World");
    EXPECT_EQ(pt.textAt(0, 5), "Hello");
}

TEST(PieceTable, LineCountNoNewlines) {
    PieceTable pt("Hello");
    EXPECT_EQ(pt.lineCount(), 1);
}

TEST(PieceTable, LineCountWithNewlines) {
    PieceTable pt("Line1\nLine2\nLine3");
    EXPECT_EQ(pt.lineCount(), 3);
}

TEST(PieceTable, LineCountTrailingNewline) {
    PieceTable pt("Line1\nLine2\n");
    EXPECT_EQ(pt.lineCount(), 3);
}

TEST(PieceTable, LineText) {
    PieceTable pt("Line1\nLine2\nLine3");
    EXPECT_EQ(pt.lineText(0), "Line1");
    EXPECT_EQ(pt.lineText(1), "Line2");
    EXPECT_EQ(pt.lineText(2), "Line3");
}

TEST(PieceTable, LineTextAfterInsert) {
    PieceTable pt("AAA\nBBB");
    pt.insert(4, "CCC\n");
    EXPECT_EQ(pt.lineCount(), 3);
    EXPECT_EQ(pt.lineText(0), "AAA");
    EXPECT_EQ(pt.lineText(1), "CCC");
    EXPECT_EQ(pt.lineText(2), "BBB");
}

TEST(PieceTable, OffsetToLine) {
    PieceTable pt("AB\nCD\nEF");
    EXPECT_EQ(pt.offsetToLine(0), 0);
    EXPECT_EQ(pt.offsetToLine(1), 0);
    EXPECT_EQ(pt.offsetToLine(2), 0);   // '\n' 属于第 0 行
    EXPECT_EQ(pt.offsetToLine(3), 1);
    EXPECT_EQ(pt.offsetToLine(6), 2);
}

TEST(PieceTable, LineToOffset) {
    PieceTable pt("AB\nCD\nEF");
    EXPECT_EQ(pt.lineToOffset(0), 0);
    EXPECT_EQ(pt.lineToOffset(1), 3);
    EXPECT_EQ(pt.lineToOffset(2), 6);
}

TEST(PieceTable, EmptyLineText) {
    PieceTable pt("\n\n");
    EXPECT_EQ(pt.lineCount(), 3);
    EXPECT_EQ(pt.lineText(0), "");
    EXPECT_EQ(pt.lineText(1), "");
    EXPECT_EQ(pt.lineText(2), "");
}

TEST(PieceTable, ChineseText) {
    PieceTable pt(QString::fromUtf8("\u4f60\u597d\u4e16\u754c"));
    EXPECT_EQ(pt.length(), 4);
    pt.insert(2, QString::fromUtf8("\uff0c"));
    EXPECT_EQ(pt.text(), QString::fromUtf8("\u4f60\u597d\uff0c\u4e16\u754c"));
    EXPECT_EQ(pt.length(), 5);
}

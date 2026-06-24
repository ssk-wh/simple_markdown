// Spec: specs/模块-core/README.md
// Last synced: 2026-06-24
//
// Document 核心契约测试（不含文件 I/O）。
// T-DOC-1 ~ T-DOC-8

#include <gtest/gtest.h>
#include "Document.h"
#include <QSignalSpy>

// T-DOC-1：空文档
TEST(DocumentTest, T_DOC1_Empty)
{
    Document doc;
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_EQ(doc.length(), 0);
    EXPECT_EQ(doc.text().toStdString(), "");
    EXPECT_FALSE(doc.isModified());
    EXPECT_FALSE(doc.canUndo());
    EXPECT_FALSE(doc.canRedo());
}

// T-DOC-2：插入文本
TEST(DocumentTest, T_DOC2_Insert)
{
    Document doc;
    doc.insert(0, "hello");
    EXPECT_EQ(doc.text().toStdString(), "hello");
    EXPECT_EQ(doc.length(), 5);
}

// T-DOC-3：删除文本
TEST(DocumentTest, T_DOC3_Remove)
{
    Document doc;
    doc.insert(0, "hello world");
    doc.remove(0, 6);
    EXPECT_EQ(doc.text().toStdString(), "world");
}

// T-DOC-4：replace 操作
TEST(DocumentTest, T_DOC4_Replace)
{
    Document doc;
    doc.insert(0, "hello world");
    doc.replace(6, 5, "there");
    EXPECT_EQ(doc.text().toStdString(), "hello there");
}

// T-DOC-5：撤销/重做
TEST(DocumentTest, T_DOC5_UndoRedo)
{
    Document doc;
    doc.insert(0, "abc");
    doc.insert(3, " def");
    EXPECT_TRUE(doc.canUndo());

    doc.undo();
    EXPECT_EQ(doc.text().toStdString(), "abc");
    EXPECT_TRUE(doc.canRedo());

    doc.redo();
    EXPECT_EQ(doc.text().toStdString(), "abc def");
}

// T-DOC-6：修改状态跟踪
TEST(DocumentTest, T_DOC6_ModifiedState)
{
    Document doc;
    EXPECT_FALSE(doc.isModified());

    doc.insert(0, "hello");
    EXPECT_TRUE(doc.isModified());

    doc.undo();
    EXPECT_FALSE(doc.isModified());

    doc.redo();
    EXPECT_TRUE(doc.isModified());
}

// T-DOC-7：setModified 强制设回未修改
TEST(DocumentTest, T_DOC7_SetModified)
{
    Document doc;
    doc.insert(0, "hello");
    EXPECT_TRUE(doc.isModified());

    doc.setModified(false);
    EXPECT_FALSE(doc.isModified());
}

// T-DOC-8：textChanged 信号发出
TEST(DocumentTest, T_DOC8_TextChangedSignal)
{
    Document doc;
    QSignalSpy spy(&doc, &Document::textChanged);

    doc.insert(0, "hello");
    ASSERT_EQ(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args[0].toInt(), 0);   // offset
    EXPECT_EQ(args[1].toInt(), 0);   // removedLength
    EXPECT_EQ(args[2].toInt(), 5);   // addedLength
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

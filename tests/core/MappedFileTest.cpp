// Spec: specs/模块-core/README.md
// Last synced: 2026-06-24
//
// MappedFile 文件内存映射契约测试。
// T-MF-1 ~ T-MF-7

#include <gtest/gtest.h>
#include "MappedFile.h"
#include <QTemporaryDir>
#include <QFile>

// T-MF-1：未打开状态
TEST(MappedFileTest, T_MF1_NotOpen)
{
    MappedFile mf;
    EXPECT_FALSE(mf.isOpen());
    EXPECT_EQ(mf.size(), 0);
    EXPECT_EQ(mf.data(), nullptr);
}

// T-MF-2：打开不存在的文件
TEST(MappedFileTest, T_MF2_OpenNonExistent)
{
    MappedFile mf;
    EXPECT_FALSE(mf.open("/nonexistent/path/file.txt"));
    EXPECT_FALSE(mf.isOpen());
}

// T-MF-3：打开空文件
TEST(MappedFileTest, T_MF3_OpenEmpty)
{
    QTemporaryDir dir;
    QFile file(dir.path() + "/empty.txt");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();

    MappedFile mf;
    EXPECT_TRUE(mf.open(dir.path() + "/empty.txt"));
    EXPECT_TRUE(mf.isOpen());
    EXPECT_EQ(mf.size(), 0);
    EXPECT_EQ(mf.toQString().toStdString(), "");
}

// T-MF-4：打开文件并读取内容
TEST(MappedFileTest, T_MF4_ReadContent)
{
    QTemporaryDir dir;
    QFile file(dir.path() + "/test.txt");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("hello\nworld\n");
    file.close();

    MappedFile mf;
    EXPECT_TRUE(mf.open(dir.path() + "/test.txt"));
    EXPECT_TRUE(mf.isOpen());
    EXPECT_EQ(mf.size(), 12);
    EXPECT_EQ(mf.toQString().toStdString(), "hello\nworld\n");
}

// T-MF-5：移动语义
TEST(MappedFileTest, T_MF5_MoveSemantics)
{
    QTemporaryDir dir;
    QFile file(dir.path() + "/data.bin");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("test data");
    file.close();

    MappedFile mf1;
    ASSERT_TRUE(mf1.open(dir.path() + "/data.bin"));

    MappedFile mf2(std::move(mf1));
    EXPECT_FALSE(mf1.isOpen());  // mf1 被清空
    EXPECT_TRUE(mf2.isOpen());
    EXPECT_EQ(mf2.size(), 9);
    EXPECT_EQ(mf2.toQString().toStdString(), "test data");

    MappedFile mf3;
    mf3 = std::move(mf2);
    EXPECT_FALSE(mf2.isOpen());
    EXPECT_TRUE(mf3.isOpen());
    EXPECT_EQ(mf3.size(), 9);
}

// T-MF-6：close 后状态重置
TEST(MappedFileTest, T_MF6_CloseResets)
{
    QTemporaryDir dir;
    QFile file(dir.path() + "/temp.bin");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(dir.path() + "/temp.bin"));
    EXPECT_TRUE(mf.isOpen());

    mf.close();
    EXPECT_FALSE(mf.isOpen());
    EXPECT_EQ(mf.size(), 0);
    EXPECT_EQ(mf.data(), nullptr);
}

// T-MF-7：重复 open 切换文件
TEST(MappedFileTest, T_MF7_ReopenDifferentFile)
{
    QTemporaryDir dir;
    QFile f1(dir.path() + "/a.txt");
    ASSERT_TRUE(f1.open(QIODevice::WriteOnly));
    f1.write("AAA");
    f1.close();

    QFile f2(dir.path() + "/b.txt");
    ASSERT_TRUE(f2.open(QIODevice::WriteOnly));
    f2.write("BB");
    f2.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(dir.path() + "/a.txt"));
    ASSERT_TRUE(mf.isOpen());
    EXPECT_EQ(mf.size(), 3);
    EXPECT_EQ(mf.toQString().toStdString(), "AAA");

    ASSERT_TRUE(mf.open(dir.path() + "/b.txt"));
    EXPECT_EQ(mf.size(), 2);
    EXPECT_EQ(mf.toQString().toStdString(), "BB");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

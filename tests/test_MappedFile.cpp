#include <gtest/gtest.h>
#include "MappedFile.h"
#include <QFile>
#include <QTemporaryFile>

TEST(MappedFile, OpenNonExistentFileFails) {
    MappedFile mf;
    EXPECT_FALSE(mf.open("__nonexistent_file_12345.txt"));
    EXPECT_FALSE(mf.isOpen());
}

TEST(MappedFile, OpenAndReadFile) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("Hello, MappedFile!");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(path));
    EXPECT_TRUE(mf.isOpen());
    EXPECT_EQ(mf.size(), 18u);
    EXPECT_EQ(std::string(mf.data(), mf.size()), "Hello, MappedFile!");
    EXPECT_EQ(mf.toQString(), "Hello, MappedFile!");
}

TEST(MappedFile, EmptyFile) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(path));
    EXPECT_TRUE(mf.isOpen());
    EXPECT_EQ(mf.size(), 0u);
}

TEST(MappedFile, MoveSemantics) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("move test");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf1;
    ASSERT_TRUE(mf1.open(path));
    MappedFile mf2(std::move(mf1));
    EXPECT_FALSE(mf1.isOpen());
    EXPECT_TRUE(mf2.isOpen());
    EXPECT_EQ(mf2.toQString(), "move test");
}

TEST(MappedFile, CloseReleasesResources) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("close test");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(path));
    mf.close();
    EXPECT_FALSE(mf.isOpen());
    EXPECT_EQ(mf.data(), nullptr);
    EXPECT_EQ(mf.size(), 0u);
}

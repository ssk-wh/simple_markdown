// Spec: specs/模块-core/README.md
// Last synced: 2026-06-24
//
// LineIndex 骨架实现锁定测试。
// T-LI-1 ~ T-LI-2

#include <gtest/gtest.h>
#include "LineIndex.h"

// T-LI-1：默认行为
TEST(LineIndexTest, T_LI1_Default)
{
    LineIndex li;
    EXPECT_EQ(li.lineCount(), 1);
    EXPECT_EQ(li.offsetToLine(0), 0);
    EXPECT_EQ(li.lineToOffset(0), 0);
    EXPECT_EQ(li.lineLength(0), 0);
}

// T-LI-2：build 后仍为默认
TEST(LineIndexTest, T_LI2_AfterBuild)
{
    LineIndex li;
    QString text = "hello\nworld\nfoo\n";
    li.build(text.constData(), text.length());
    EXPECT_EQ(li.lineCount(), 1);
    EXPECT_EQ(li.offsetToLine(10), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// Spec: specs/模块-editor/README.md
// Last synced: 2026-06-24
//
// SearchWorker 搜索结果正确性测试。
// T-SW-1 ~ T-SW-9

#include <gtest/gtest.h>
#include <QSignalSpy>
#include "SearchWorker.h"

// T-SW-1：空搜索词 → 空结果
TEST(SearchWorkerTest, T_SW1_EmptyTextReturnsEmpty)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.search("", "hello world", 1);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    EXPECT_TRUE(matches.isEmpty());
}

// T-SW-2：空全文 → 空结果
TEST(SearchWorkerTest, T_SW2_EmptyFullTextReturnsEmpty)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.search("hello", "", 1);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    EXPECT_TRUE(matches.isEmpty());
}

// T-SW-3：普通搜索匹配
TEST(SearchWorkerTest, T_SW3_BasicMatch)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.search("world", "hello world", 0);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    ASSERT_EQ(matches.size(), 1);
    EXPECT_EQ(matches[0].first, 6);
    EXPECT_EQ(matches[0].second, 5);
}

// T-SW-4：普通搜索多匹配
TEST(SearchWorkerTest, T_SW4_MultipleMatches)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.search("a", "abcaba", 0);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    ASSERT_EQ(matches.size(), 3);
}

// T-SW-5：普通搜索大小写不敏感
TEST(SearchWorkerTest, T_SW5_CaseInsensitive)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.search("HELLO", "hello world", 0);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    ASSERT_EQ(matches.size(), 1);
}

// T-SW-6：全词匹配
TEST(SearchWorkerTest, T_SW6_WholeWord)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.searchWithOptions("cat", "cat category cat", 0, false, true, false);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    ASSERT_EQ(matches.size(), 2);
}

// T-SW-7：正则表达式搜索
TEST(SearchWorkerTest, T_SW7_RegexSearch)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.searchWithOptions("\\d{3}", "abc 123 def 456", 0, false, false, true);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    ASSERT_EQ(matches.size(), 2);
}

// T-SW-8：无效正则返回空结果
TEST(SearchWorkerTest, T_SW8_InvalidRegex)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.searchWithOptions("[invalid", "test", 0, false, false, true);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    EXPECT_TRUE(matches.isEmpty());
}

// T-SW-9：大小写敏感搜索
TEST(SearchWorkerTest, T_SW9_CaseSensitive)
{
    SearchWorker sw;
    QSignalSpy spy(&sw, &SearchWorker::searchFinished);
    sw.searchWithOptions("Hello", "hello Hello HELLO", 0, true, false, false);
    ASSERT_EQ(spy.count(), 1);
    auto matches = spy[0][0].value<QVector<QPair<int,int>>>();
    ASSERT_EQ(matches.size(), 1);
    EXPECT_EQ(matches[0].first, 6);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

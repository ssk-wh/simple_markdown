// tests/app/UpdateCheckerVersionTest.cpp
//
// Spec: specs/模块-app/23-检查更新.md INV-UPD-SEMVER
// 验收：T-UPD-1..5（语义化版本比较 + releases/tags JSON 解析的纯逻辑）
//
// 只测可单测的纯静态函数（不触发网络）：compareSemVer / isUpdateAvailable / parseLatestRelease。

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include "UpdateChecker.h"

// T-UPD-1：基本大小比较 + v 前缀归一
TEST(UpdateCheckerVersionTest, T_UPD_1_BasicCompare)
{
    EXPECT_GT(UpdateChecker::compareSemVer("1.2.0", "1.1.9"), 0);
    EXPECT_LT(UpdateChecker::compareSemVer("1.1.9", "1.2.0"), 0);
    EXPECT_EQ(UpdateChecker::compareSemVer("1.2.3", "1.2.3"), 0);
    // v 前缀不影响
    EXPECT_EQ(UpdateChecker::compareSemVer("v1.2.3", "1.2.3"), 0);
    EXPECT_GT(UpdateChecker::compareSemVer("v2.0.0", "v1.9.9"), 0);
}

// T-UPD-2：缺位补 0 + 逐段优先级（major > minor > patch）
TEST(UpdateCheckerVersionTest, T_UPD_2_MissingSegmentsAndPriority)
{
    EXPECT_EQ(UpdateChecker::compareSemVer("1.2", "1.2.0"), 0);
    EXPECT_EQ(UpdateChecker::compareSemVer("1", "1.0.0"), 0);
    EXPECT_GT(UpdateChecker::compareSemVer("1.0.0", "0.9.9"), 0);   // major 主导
    EXPECT_GT(UpdateChecker::compareSemVer("1.3.0", "1.2.99"), 0);  // minor 主导
    EXPECT_GT(UpdateChecker::compareSemVer("1.2.10", "1.2.9"), 0);  // patch 数值比较（非字典序）
}

// T-UPD-3：预发布/构建后缀被忽略（只比三段数字）
TEST(UpdateCheckerVersionTest, T_UPD_3_PrereleaseSuffixIgnored)
{
    EXPECT_EQ(UpdateChecker::compareSemVer("1.2.3-beta.1", "1.2.3"), 0);
    EXPECT_EQ(UpdateChecker::compareSemVer("1.2.3+build7", "1.2.3"), 0);
    EXPECT_GT(UpdateChecker::compareSemVer("1.3.0-rc1", "1.2.9"), 0);
}

// T-UPD-4：isUpdateAvailable 语义（latest 严格新于 current）
TEST(UpdateCheckerVersionTest, T_UPD_4_IsUpdateAvailable)
{
    EXPECT_TRUE(UpdateChecker::isUpdateAvailable("1.1.15", "1.2.0"));
    EXPECT_TRUE(UpdateChecker::isUpdateAvailable("1.1.15", "v1.1.16"));
    EXPECT_FALSE(UpdateChecker::isUpdateAvailable("1.2.0", "1.2.0"));   // 相等不算更新
    EXPECT_FALSE(UpdateChecker::isUpdateAvailable("1.2.0", "1.1.9"));   // 远端更旧
    // 异常/空版本号按 0.0.0 处理，不应误判为有更新
    EXPECT_FALSE(UpdateChecker::isUpdateAvailable("1.0.0", ""));
    EXPECT_FALSE(UpdateChecker::isUpdateAvailable("1.0.0", "garbage"));
}

// T-UPD-5：JSON 解析——releases/latest（对象）与 tags（数组）两种形态
TEST(UpdateCheckerVersionTest, T_UPD_5_ParseLatestRelease)
{
    // releases/latest：对象，含 tag_name / body / html_url
    const QByteArray releaseJson = R"({
        "tag_name": "v1.2.0",
        "body": "### Fixed\n- something",
        "html_url": "https://github.com/ssk-wh/simple_markdown/releases/tag/v1.2.0"
    })";
    QString tag, notes, url;
    ASSERT_TRUE(UpdateChecker::parseLatestRelease(releaseJson, tag, notes, url));
    EXPECT_EQ(tag, QStringLiteral("v1.2.0"));
    EXPECT_TRUE(notes.contains(QStringLiteral("something")));
    EXPECT_TRUE(url.contains(QStringLiteral("releases/tag/v1.2.0")));

    // tags：数组，取首个 name；无 body，url 回退到通用 releases 页
    const QByteArray tagsJson = R"([
        {"name": "v1.3.0"},
        {"name": "v1.2.0"}
    ])";
    QString tag2, notes2, url2;
    ASSERT_TRUE(UpdateChecker::parseLatestRelease(tagsJson, tag2, notes2, url2));
    EXPECT_EQ(tag2, QStringLiteral("v1.3.0"));
    EXPECT_TRUE(notes2.isEmpty());
    EXPECT_TRUE(url2.contains(QStringLiteral("/releases")));

    // 非法 JSON / 空对象 → 解析失败
    QString t3, n3, u3;
    EXPECT_FALSE(UpdateChecker::parseLatestRelease("not json", t3, n3, u3));
    EXPECT_FALSE(UpdateChecker::parseLatestRelease("{}", t3, n3, u3));
    EXPECT_FALSE(UpdateChecker::parseLatestRelease("[]", t3, n3, u3));
}

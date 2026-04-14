// Spec: specs/模块-preview/10-Frontmatter渲染.md §7 验收条件 T-1 .. T-19
// Last synced: 2026-04-14
#include <gtest/gtest.h>

#include "MarkdownParser.h"
#include "MarkdownAst.h"

#include <QString>
#include <QStringList>

namespace {

static const AstNode* findFrontmatter(const AstNode* root)
{
    if (!root) return nullptr;
    for (const auto& c : root->children) {
        if (c->type == AstNodeType::Frontmatter) return c.get();
    }
    return nullptr;
}

static QString makeDoc(std::initializer_list<const char*> lines)
{
    QStringList out;
    for (const char* s : lines) out << QString::fromUtf8(s);
    return out.join('\n');
}

} // namespace

// T-1：基本提取
TEST(FrontmatterParseTest, T1_BasicExtraction)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "title: Hello",
        "date: 2026-04-14",
        "---",
        "",
        "# Body"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 2u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QStringLiteral("title"));
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("Hello"));
    EXPECT_EQ(fm->frontmatterEntries[1].first,  QStringLiteral("date"));
    EXPECT_EQ(fm->frontmatterEntries[1].second, QStringLiteral("2026-04-14"));
}

// T-2：首行非 --- 不触发
TEST(FrontmatterParseTest, T2_NoFrontmatterWhenFirstLineIsHeading)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "# Title",
        "---",
        "body"
    });
    auto root = parser.parse(doc);
    EXPECT_EQ(findFrontmatter(root.get()), nullptr);
}

// T-3：中间 --- 是 ThematicBreak，不与 frontmatter 冲突
TEST(FrontmatterParseTest, T3_MidDocHrDoesNotConflict)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "title: T",
        "---",
        "",
        "Para1",
        "",
        "---",
        "",
        "Para2"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    // 在剩余 children 里至少存在一个 ThematicBreak
    bool foundHr = false;
    for (const auto& c : root->children) {
        if (c->type == AstNodeType::ThematicBreak) { foundHr = true; break; }
    }
    EXPECT_TRUE(foundHr);
}

// T-7：数组字面量原样保留
TEST(FrontmatterParseTest, T7_ArrayLiteralPreservedAsString)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "owners: [pcfan, alice]",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QStringLiteral("owners"));
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("[pcfan, alice]"));
}

// T-8：注释行被忽略
TEST(FrontmatterParseTest, T8_CommentLinesIgnored)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "a: 1",
        "# comment line",
        "   # leading-space comment",
        "b: 2",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 2u);
    EXPECT_EQ(fm->frontmatterEntries[0].first, QStringLiteral("a"));
    EXPECT_EQ(fm->frontmatterEntries[1].first, QStringLiteral("b"));
}

// T-9：空行被忽略
TEST(FrontmatterParseTest, T9_EmptyLinesIgnored)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "a: 1",
        "",
        "    ",
        "b: 2",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 2u);
}

// T-16：首行 --- 但缺少结束 --- → 回退
TEST(FrontmatterParseTest, T16_MissingEndFallback)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "title: T",
        "no closing marker"
    });
    auto root = parser.parse(doc);
    EXPECT_EQ(findFrontmatter(root.get()), nullptr);
}

// T-18：无冒号行 → 整行作 value，key 留空（INV-14）
TEST(FrontmatterParseTest, T18_LineWithoutColon)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "TODO",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QString());
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("TODO"));
}

// T-19：多冒号 → 取首个冒号拆分
TEST(FrontmatterParseTest, T19_MultipleColonsFirstWins)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "site: https://example.com",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QStringLiteral("site"));
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("https://example.com"));
}

// 行号偏移：§8.9 剥离后 cmark 节点的 startLine 要加回 frontmatterLineCount
TEST(FrontmatterParseTest, LineNumberShiftAfterExtraction)
{
    MarkdownParser parser;
    // frontmatter 占 3 行（行号 0..2），紧随空行（行 3），标题在行 4
    QString doc = makeDoc({
        "---",
        "a: 1",
        "---",
        "",
        "# Heading"
    });
    auto root = parser.parse(doc);
    const AstNode* heading = nullptr;
    for (const auto& c : root->children) {
        if (c->type == AstNodeType::Heading) { heading = c.get(); break; }
    }
    ASSERT_NE(heading, nullptr);
    EXPECT_EQ(heading->startLine, 4);
}

// BOM 容忍（INV-2）
TEST(FrontmatterParseTest, BomToleranceOnFirstLine)
{
    MarkdownParser parser;
    QString doc;
    doc.append(QChar(0xFEFF));
    doc.append(QStringLiteral("---\na: 1\n---\n\n# Body"));
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
}

// rawText 保留（INV-13）
TEST(FrontmatterParseTest, RawTextRetainsOriginalYaml)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "a: 1",
        "# comment",
        "",
        "b: 2",
        "---",
        "# body"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    EXPECT_TRUE(fm->frontmatterRawText.startsWith(QStringLiteral("---")));
    EXPECT_TRUE(fm->frontmatterRawText.endsWith(QStringLiteral("---")));
    EXPECT_TRUE(fm->frontmatterRawText.contains(QStringLiteral("# comment")));
}

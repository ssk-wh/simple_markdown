// tests/viewer/ChunkScannerTest.cpp
//
// Spec: specs/模块-viewer/01-分块扫描.md
// 验收：T1 ~ T17
#include <gtest/gtest.h>

#include <QByteArray>
#include <string>

#include "ChunkScanner.h"

using viewer::Chunk;
using viewer::ChunkScanner;
using viewer::ScanOptions;

namespace {

ChunkScanner scanAll(const std::string& text, ScanOptions opt = {})
{
    ChunkScanner s(opt);
    s.scan(text.data(), text.size(), true);
    return s;
}

// 校验 INV-SCAN-COVER
void expectCover(const ChunkScanner& s, const std::string& text)
{
    const auto& cs = s.chunks();
    uint64_t expectStart = 0;
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF) expectStart = 3;
    uint64_t lines = 0;
    for (const auto& c : cs) {
        EXPECT_EQ(c.byteStart, expectStart);
        EXPECT_GE(c.byteEnd, c.byteStart);
        expectStart = c.byteEnd;
        lines += c.lineCount;
    }
    if (!text.empty()) EXPECT_EQ(expectStart, text.size());
    // 物理行数：每个换行符一行，末尾无换行的残行再算一行
    uint64_t phys = 0;
    for (char ch : text) if (ch == '\n') ++phys;
    if (!text.empty() && text.back() != '\n') ++phys;
    EXPECT_EQ(lines, phys);
}

std::string makeParagraphs(int n)
{
    std::string t;
    for (int i = 0; i < n; ++i) t += "Paragraph " + std::to_string(i) + " text.\n\n";
    return t;
}

// 是否存在某 chunk 完整覆盖 [firstLine, lastLine]
bool someChunkCovers(const ChunkScanner& s, uint32_t firstLine, uint32_t lastLine)
{
    for (const auto& c : s.chunks())
        if (c.lineStart <= firstLine && c.lineStart + c.lineCount > lastLine) return true;
    return false;
}

}  // namespace

TEST(ChunkScannerTest, T1_EmptyAndBlank)
{
    auto s = scanAll("");
    EXPECT_TRUE(s.chunks().empty());
    auto s2 = scanAll("\n\n\n");
    expectCover(s2, "\n\n\n");
    EXPECT_TRUE(s2.headings().empty());
}

TEST(ChunkScannerTest, T2_SingleLineNoNewline)
{
    const std::string t = "# Title";
    auto s = scanAll(t);
    ASSERT_EQ(s.chunks().size(), 1u);
    expectCover(s, t);
    ASSERT_EQ(s.headings().size(), 1u);
    EXPECT_EQ(s.headings()[0].title, QString("Title"));
}

TEST(ChunkScannerTest, T3_CrlfLines)
{
    const std::string t = "# A\r\n\r\ntext\r\n\r\n## B\r\n";
    auto s = scanAll(t);
    expectCover(s, t);
    ASSERT_EQ(s.headings().size(), 2u);
    EXPECT_EQ(s.headings()[0].title, QString("A"));
    EXPECT_EQ(s.headings()[1].level, 2);
    EXPECT_EQ(s.headings()[1].line, 4u);
}

TEST(ChunkScannerTest, T4_BomSkipped)
{
    const std::string t = std::string("\xEF\xBB\xBF") + "# Hi\n\nbody\n";
    auto s = scanAll(t);
    ASSERT_FALSE(s.chunks().empty());
    EXPECT_EQ(s.chunks()[0].byteStart, 3u);
    expectCover(s, t);
    ASSERT_EQ(s.headings().size(), 1u);
    EXPECT_EQ(s.headings()[0].title, QString("Hi"));
}

TEST(ChunkScannerTest, T5_FenceBlankLinesAndHashNotHeadingNotCut)
{
    ScanOptions o;
    o.targetLines = 2;  // 逼迫尽可能切块
    std::string t = "intro\n\n```py\n# comment\n\nx = 1\n\n# other\n```\n\nafter\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    EXPECT_TRUE(s.headings().empty());
    EXPECT_TRUE(someChunkCovers(s, 2, 8));
}

TEST(ChunkScannerTest, T6_LooseListNotSplit)
{
    ScanOptions o;
    o.targetLines = 1;
    std::string t = "- a\n\n- b\n\n  continuation\n\n- c\n\nnext para\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    EXPECT_TRUE(someChunkCovers(s, 0, 6));
}

TEST(ChunkScannerTest, T7_AtxAndSetextHeadings)
{
    std::string t = "Title One\n=========\n\ntext\n\nSub Title\n---\n\n### H3 **bold** `code` [link](http://x) ###\n";
    auto s = scanAll(t);
    ASSERT_EQ(s.headings().size(), 3u);
    EXPECT_EQ(s.headings()[0].level, 1);
    EXPECT_EQ(s.headings()[0].title, QString("Title One"));
    EXPECT_EQ(s.headings()[0].line, 0u);
    EXPECT_EQ(s.headings()[1].level, 2);
    EXPECT_EQ(s.headings()[1].line, 5u);
    EXPECT_EQ(s.headings()[2].level, 3);
    EXPECT_EQ(s.headings()[2].title, QString("H3 bold code link"));
}

TEST(ChunkScannerTest, T8_RefDefsCollectedAndAppended)
{
    std::string t = "see [x][r]\n\ntext\n\n[r]: http://example.com\n";
    auto s = scanAll(t);
    EXPECT_TRUE(s.refDefs().contains("[r]: http://example.com"));
    auto a = s.assemble(t.data(), s.chunks()[0]);
    EXPECT_TRUE(a.text.contains("[r]: http://example.com"));
}

TEST(ChunkScannerTest, T9_FrontmatterNotSplitAndLaterHrIsNotFrontmatter)
{
    ScanOptions o;
    o.targetLines = 1;
    std::string t = "---\ntitle: a\n\ntags: [b]\n---\n\n# H\n\ntext\n\n---\nk: v\n---\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    ASSERT_GE(s.chunks().size(), 1u);
    EXPECT_GE(s.chunks()[0].lineCount, 5u);  // frontmatter 5 行整体在首 chunk
    ASSERT_GE(s.headings().size(), 1u);
    EXPECT_EQ(s.headings()[0].line, 6u);
}

TEST(ChunkScannerTest, T10_FenceHardCapSplitsWithPrologue)
{
    ScanOptions o;
    o.hardLines = 10;
    o.targetLines = 5;
    std::string t = "```cpp\n";
    for (int i = 0; i < 35; ++i) t += "line " + std::to_string(i) + "\n";
    t += "```\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    ASSERT_GE(s.chunks().size(), 3u);
    EXPECT_TRUE(s.chunks()[0].flags & Chunk::kEndsInOpenFence);
    EXPECT_GT(s.chunks()[1].prologueIdx, 0u);
    auto a = s.assemble(t.data(), s.chunks()[1]);
    EXPECT_TRUE(a.text.startsWith("```cpp\n"));
    EXPECT_EQ(a.lineBias, 1);
}

TEST(ChunkScannerTest, T11_LongLineSlicedOnUtf8Boundary)
{
    ScanOptions o;
    o.maxLineBytes = 100;
    std::string t = "head\n\n";
    for (int i = 0; i < 200; ++i) t += "中";  // 每字 3 字节，共 600B
    t += "\n\ntail\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    EXPECT_GT(s.chunks().size(), 4u);
    for (const auto& c : s.chunks()) {
        // 每个 chunk 的起点必须是字符边界
        if (c.byteStart < t.size())
            EXPECT_NE(static_cast<uint8_t>(t[c.byteStart]) & 0xC0, 0x80);
    }
}

TEST(ChunkScannerTest, T12_IncrementalEqualsWhole)
{
    ScanOptions o;
    o.targetLines = 8;
    std::string t;
    for (int i = 0; i < 300; ++i) {
        t += "## Section " + std::to_string(i) + "\n\n";
        t += "para one\nsecond line\n\n- item\n- item2\n\n```\ncode\n\nmore\n```\n\n";
    }
    auto whole = scanAll(t, o);
    ChunkScanner inc(o);
    for (size_t n = 0; n < t.size(); n += 37) inc.scan(t.data(), std::min(t.size(), n + 37), false);
    inc.scan(t.data(), t.size(), true);
    ASSERT_EQ(whole.chunks().size(), inc.chunks().size());
    for (size_t i = 0; i < whole.chunks().size(); ++i) {
        EXPECT_EQ(whole.chunks()[i].byteStart, inc.chunks()[i].byteStart);
        EXPECT_EQ(whole.chunks()[i].byteEnd, inc.chunks()[i].byteEnd);
        EXPECT_EQ(whole.chunks()[i].lineStart, inc.chunks()[i].lineStart);
    }
    ASSERT_EQ(whole.headings().size(), inc.headings().size());
    EXPECT_EQ(whole.headings().size(), 300u);
}

TEST(ChunkScannerTest, T13_CoverInvariantOnMixedDocument)
{
    ScanOptions o;
    o.targetLines = 6;
    std::string t = makeParagraphs(100) + "| a | b |\n|---|---|\n| 1 | 2 |\n\n> quote\n\n    indented code\n\n1. x\n2. y\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    EXPECT_GT(s.chunks().size(), 5u);
}

TEST(ChunkScannerTest, T14_HtmlCommentBlankLinesNotCut)
{
    ScanOptions o;
    o.targetLines = 1;
    std::string t = "text\n\n<!--\nhidden\n\n# not heading\n\nstill hidden\n-->\n\nafter\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    EXPECT_TRUE(s.headings().empty());
    EXPECT_TRUE(someChunkCovers(s, 2, 8));
}

TEST(ChunkScannerTest, T15_IndentedCodeAndNestedContentNotCut)
{
    ScanOptions o;
    o.targetLines = 1;
    std::string t = "para\n\n    code a\n\n    code b\n\n1. item\n\n   para in item\n\n2. second\n\nend\n";
    auto s = scanAll(t, o);
    expectCover(s, t);
    EXPECT_TRUE(someChunkCovers(s, 2, 4));
    EXPECT_TRUE(someChunkCovers(s, 6, 10));
}

TEST(ChunkScannerTest, T16_UnclosedFenceToEof)
{
    std::string t = "a\n\n```\nnever closed\n\nstill code\n";
    auto s = scanAll(t);
    expectCover(s, t);
    EXPECT_TRUE(s.headings().empty());
}

TEST(ChunkScannerTest, T17_SnakeCaseTitleKeepsUnderscore)
{
    auto s = scanAll("# my_var_name and _emph_\n");
    ASSERT_EQ(s.headings().size(), 1u);
    EXPECT_EQ(s.headings()[0].title, QString("my_var_name and emph"));
}

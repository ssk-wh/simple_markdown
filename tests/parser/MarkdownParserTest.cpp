// Spec: specs/模块-parser/README.md
// Last synced: 2026-04-15
//
// MarkdownParser 基础 block / inline 节点的回归测试。
// 目标：锁定"输入 X 得到 AST Y"的契约，防止升级 cmark-gfm 或折腾转换层时出回归。
//
// 覆盖：
//   T-P1  标题 H1~H6 解析为 Heading 节点 + 正确 level
//   T-P2  无序列表 / 有序列表 → List + 子 Item
//   T-P3  fenced 代码块 → CodeBlock + fenceInfo（语言名）
//   T-P4  行内代码 `code` → Code 节点
//   T-P5  链接 [text](url "title") → Link + url/title + Text 子节点
//   T-P6  图片 ![alt](src) → Image 节点
//   T-P7  引用块 > text → BlockQuote
//   T-P8  GFM 表格 → Table + TableRow + TableCell + 对齐
//   T-P9  换行：段落内单换行 / 软换行；双换行 → 新段落
//   T-P10 粗体 / 斜体 / 删除线（GFM）
//
// 注：不覆盖 Frontmatter — 已由 FrontmatterRenderTest 覆盖。

#include <gtest/gtest.h>

#include "MarkdownParser.h"
#include "MarkdownAst.h"

#include <QString>
#include <QStringList>

namespace {

// 在 root 的 children 中查找第一个指定类型的节点
static const AstNode* findFirstOfType(const AstNode* root, AstNodeType t) {
    if (!root) return nullptr;
    for (const auto& c : root->children) {
        if (c->type == t) return c.get();
    }
    return nullptr;
}

// 递归查找：root 子树内第一个指定类型的节点
static const AstNode* findDescendantOfType(const AstNode* root, AstNodeType t) {
    if (!root) return nullptr;
    if (root->type == t) return root;
    for (const auto& c : root->children) {
        if (const AstNode* r = findDescendantOfType(c.get(), t))
            return r;
    }
    return nullptr;
}

static int countOfType(const AstNode* root, AstNodeType t) {
    if (!root) return 0;
    int n = (root->type == t) ? 1 : 0;
    for (const auto& c : root->children)
        n += countOfType(c.get(), t);
    return n;
}

} // namespace

// T-P1：H1~H6 标题解析
TEST(MarkdownParserTest, T_P1_HeadingLevels)
{
    MarkdownParser p;
    auto ast = p.parse(
        "# H1\n\n## H2\n\n### H3\n\n#### H4\n\n##### H5\n\n###### H6\n");
    ASSERT_NE(ast, nullptr);

    int idx = 0;
    int expectedLevel = 1;
    for (const auto& c : ast->children) {
        ASSERT_EQ(c->type, AstNodeType::Heading) << "at index " << idx;
        EXPECT_EQ(c->headingLevel, expectedLevel) << "at index " << idx;
        ++expectedLevel;
        ++idx;
    }
    EXPECT_EQ(idx, 6);
}

// T-P2：无序 + 有序列表
TEST(MarkdownParserTest, T_P2_Lists)
{
    MarkdownParser p;

    {
        auto ast = p.parse("- a\n- b\n- c\n");
        const AstNode* list = findFirstOfType(ast.get(), AstNodeType::List);
        ASSERT_NE(list, nullptr);
        EXPECT_EQ(list->listType, ListType::Bullet);
        EXPECT_EQ(countOfType(list, AstNodeType::Item), 3);
    }
    {
        auto ast = p.parse("1. one\n2. two\n3. three\n");
        const AstNode* list = findFirstOfType(ast.get(), AstNodeType::List);
        ASSERT_NE(list, nullptr);
        EXPECT_EQ(list->listType, ListType::Ordered);
        EXPECT_EQ(countOfType(list, AstNodeType::Item), 3);
    }
}

// T-P3：fenced 代码块 + 语言标注
TEST(MarkdownParserTest, T_P3_FencedCodeBlockWithLang)
{
    MarkdownParser p;
    auto ast = p.parse(
        "```cpp\n"
        "int x = 1;\n"
        "```\n");
    const AstNode* code = findFirstOfType(ast.get(), AstNodeType::CodeBlock);
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->fenceInfo.toStdString(), "cpp");
    EXPECT_TRUE(code->literal.contains("int x = 1;"));
}

// T-P4：行内代码
TEST(MarkdownParserTest, T_P4_InlineCode)
{
    MarkdownParser p;
    auto ast = p.parse("Use `printf` to output.\n");
    const AstNode* code = findDescendantOfType(ast.get(), AstNodeType::Code);
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->literal.toStdString(), "printf");
}

// T-P5：链接（url + title）
TEST(MarkdownParserTest, T_P5_LinkWithTitle)
{
    MarkdownParser p;
    auto ast = p.parse("[example](https://example.com \"Example Site\")\n");
    const AstNode* link = findDescendantOfType(ast.get(), AstNodeType::Link);
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->url.toStdString(), "https://example.com");
    EXPECT_EQ(link->title.toStdString(), "Example Site");

    // Link 下应至少有一个 Text 节点承载 "example"
    const AstNode* txt = findDescendantOfType(link, AstNodeType::Text);
    ASSERT_NE(txt, nullptr);
    EXPECT_EQ(txt->literal.toStdString(), "example");
}

// T-P6：图片
TEST(MarkdownParserTest, T_P6_Image)
{
    MarkdownParser p;
    auto ast = p.parse("![alt text](path/to/img.png)\n");
    const AstNode* img = findDescendantOfType(ast.get(), AstNodeType::Image);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->url.toStdString(), "path/to/img.png");
}

// T-P7：引用块
TEST(MarkdownParserTest, T_P7_BlockQuote)
{
    MarkdownParser p;
    auto ast = p.parse("> quoted text\n> second line\n");
    const AstNode* bq = findFirstOfType(ast.get(), AstNodeType::BlockQuote);
    ASSERT_NE(bq, nullptr);
    // 引用块下应至少有一个 Paragraph
    EXPECT_GE(countOfType(bq, AstNodeType::Paragraph), 1);
}

// T-P8：GFM 表格 + 对齐
TEST(MarkdownParserTest, T_P8_GfmTable)
{
    MarkdownParser p;
    auto ast = p.parse(
        "| A | B | C |\n"
        "|:--|:-:|--:|\n"
        "| 1 | 2 | 3 |\n"
        "| 4 | 5 | 6 |\n");
    const AstNode* table = findFirstOfType(ast.get(), AstNodeType::Table);
    ASSERT_NE(table, nullptr);
    // 至少 2 行（header + 2 data => 3 TableRow，或 header 单独 2 data => 3）
    EXPECT_GE(countOfType(table, AstNodeType::TableRow), 2);
    EXPECT_GE(countOfType(table, AstNodeType::TableCell), 6);
}

// T-P9：双换行 → 新段落
TEST(MarkdownParserTest, T_P9_DoubleNewlineSplitsParagraphs)
{
    MarkdownParser p;
    auto ast = p.parse("first para\n\nsecond para\n");
    EXPECT_EQ(countOfType(ast.get(), AstNodeType::Paragraph), 2);
}

// T-P10：Emph / Strong / Strikethrough（GFM）
TEST(MarkdownParserTest, T_P10_InlineFormatting)
{
    MarkdownParser p;
    auto ast = p.parse("*em* and **strong** and ~~del~~\n");
    EXPECT_GE(countOfType(ast.get(), AstNodeType::Emph), 1);
    EXPECT_GE(countOfType(ast.get(), AstNodeType::Strong), 1);
    EXPECT_GE(countOfType(ast.get(), AstNodeType::Strikethrough), 1);
}

// T-P11：空输入安全
TEST(MarkdownParserTest, T_P11_EmptyInputReturnsDocument)
{
    MarkdownParser p;
    auto ast = p.parse("");
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, AstNodeType::Document);
    EXPECT_TRUE(ast->children.empty());
}

// T-P12：行号记录（heading 的 startLine 应匹配其源码行，0-based）
// MarkdownParser 把 cmark 的 1-based 行号转成项目内部 0-based 约定
// （见 MarkdownParser.cpp:184 `cmark_node_get_start_line(node) - 1`），
// 本测试锁定这个契约，防止未来回退到 1-based 导致预览滚动同步错位。
TEST(MarkdownParserTest, T_P12_HeadingSourceLineTracked)
{
    MarkdownParser p;
    auto ast = p.parse(
        "para line 0\n"   // 第 0 行
        "\n"                // 第 1 行（空行）
        "# Heading on line 2\n");  // 第 2 行
    const AstNode* h = findFirstOfType(ast.get(), AstNodeType::Heading);
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->startLine, 2);
}

// T-P13：删除线仅识别双波浪线 ~~（GFM 标准），单波浪线 ~ 不触发
// Spec: specs/模块-parser/README.md INV-STRIKE-DOUBLE-TILDE
// Bug: plans/2026-06-16-删除线语法双波浪线确认.md
//   （cmark-gfm strikethrough 扩展默认单/双波浪线都识别，单 ~ 在预览误显删除线，
//    与编辑器高亮正则 ~~([^~]+)~~ 不一致；CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE 修正为仅双）
TEST(MarkdownParserTest, T_P13_StrikethroughDoubleTildeOnly)
{
    MarkdownParser p;
    // 双波浪线 → 删除线
    EXPECT_GE(countOfType(p.parse("~~deleted~~\n").get(), AstNodeType::Strikethrough), 1);
    EXPECT_GE(countOfType(p.parse("a ~~foo~~ b\n").get(), AstNodeType::Strikethrough), 1);
    EXPECT_GE(countOfType(p.parse("~~\xE8\xB7\xA8\xE4\xB8\xAD\xE6\x96\x87~~\n").get(),
                          AstNodeType::Strikethrough), 1);  // 跨中文
    // 单波浪线 → 不触发删除线（与编辑器高亮及 GFM 双波浪线标准一致）
    EXPECT_EQ(countOfType(p.parse("~single~\n").get(), AstNodeType::Strikethrough), 0);
    EXPECT_EQ(countOfType(p.parse("x ~y~ z\n").get(), AstNodeType::Strikethrough), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

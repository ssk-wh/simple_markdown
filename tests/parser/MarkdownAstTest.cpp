// Spec: specs/模块-parser/README.md
// Last synced: 2026-06-24
//
// AstNode 核心契约测试：类型判定、树操作。
// T-AST-1 ~ T-AST-7

#include <gtest/gtest.h>
#include "MarkdownAst.h"

// T-AST-1：block 类型判定
TEST(MarkdownAstTest, T_AST1_BlockTypes)
{
    std::vector<AstNodeType> blocks = {
        AstNodeType::Document, AstNodeType::Paragraph,
        AstNodeType::Heading, AstNodeType::CodeBlock,
        AstNodeType::BlockQuote, AstNodeType::List,
        AstNodeType::Item, AstNodeType::Table,
        AstNodeType::TableRow, AstNodeType::TableCell,
        AstNodeType::ThematicBreak, AstNodeType::HtmlBlock,
        AstNodeType::Frontmatter
    };
    for (auto t : blocks) {
        AstNode n;
        n.type = t;
        EXPECT_TRUE(n.isBlock()) << "type=" << static_cast<int>(t);
        EXPECT_FALSE(n.isInline()) << "type=" << static_cast<int>(t);
    }
}

// T-AST-2：inline 类型判定
TEST(MarkdownAstTest, T_AST2_InlineTypes)
{
    std::vector<AstNodeType> inlines = {
        AstNodeType::Text, AstNodeType::Emph,
        AstNodeType::Strong, AstNodeType::Link,
        AstNodeType::Image, AstNodeType::Code,
        AstNodeType::SoftBreak, AstNodeType::LineBreak,
        AstNodeType::HtmlInline, AstNodeType::Strikethrough
    };
    for (auto t : inlines) {
        AstNode n;
        n.type = t;
        EXPECT_FALSE(n.isBlock()) << "type=" << static_cast<int>(t);
        EXPECT_TRUE(n.isInline()) << "type=" << static_cast<int>(t);
    }
}

// T-AST-3：树构造
TEST(MarkdownAstTest, T_AST3_TreeConstruction)
{
    auto doc = std::make_unique<AstNode>();
    doc->type = AstNodeType::Document;

    auto h1 = std::make_unique<AstNode>();
    h1->type = AstNodeType::Heading;
    h1->headingLevel = 1;
    h1->startLine = 0;

    auto p = std::make_unique<AstNode>();
    p->type = AstNodeType::Paragraph;
    p->startLine = 2;

    auto text = std::make_unique<AstNode>();
    text->type = AstNodeType::Text;
    text->literal = "hello";

    p->addChild(std::move(text));
    doc->addChild(std::move(h1));
    doc->addChild(std::move(p));

    EXPECT_EQ(doc->children.size(), 2);
    EXPECT_EQ(doc->children[0]->type, AstNodeType::Heading);
    EXPECT_EQ(doc->children[0]->headingLevel, 1);
    EXPECT_EQ(doc->children[1]->type, AstNodeType::Paragraph);
    EXPECT_EQ(doc->children[1]->children.size(), 1);
    EXPECT_EQ(doc->children[1]->children[0]->literal.toStdString(), "hello");
}

// T-AST-4：frontmatter 字段
TEST(MarkdownAstTest, T_AST4_FrontmatterFields)
{
    AstNode fm;
    fm.type = AstNodeType::Frontmatter;
    fm.frontmatterEntries = {{"title", "Test"}, {"author", "Me"}};
    fm.frontmatterRawText = "---\ntitle: Test\nauthor: Me\n---\n";

    ASSERT_EQ(fm.frontmatterEntries.size(), 2);
    EXPECT_EQ(fm.frontmatterEntries[0].first.toStdString(), "title");
    EXPECT_EQ(fm.frontmatterEntries[0].second.toStdString(), "Test");
    EXPECT_TRUE(fm.isBlock());
}

// T-AST-5：默认构造状态
TEST(MarkdownAstTest, T_AST5_DefaultConstructed)
{
    AstNode n;
    EXPECT_EQ(n.type, AstNodeType::Document);
    EXPECT_TRUE(n.literal.isEmpty());
    EXPECT_EQ(n.startLine, 0);
    EXPECT_EQ(n.headingLevel, 0);
    EXPECT_TRUE(n.fenceInfo.isEmpty());
    EXPECT_TRUE(n.url.isEmpty());
    EXPECT_TRUE(n.children.empty());
}

// T-AST-6：listType / listStart / listTight
TEST(MarkdownAstTest, T_AST6_ListProperties)
{
    AstNode list;
    list.type = AstNodeType::List;
    list.listType = ListType::Ordered;
    list.listStart = 5;
    list.listTight = true;

    EXPECT_EQ(list.listType, ListType::Ordered);
    EXPECT_EQ(list.listStart, 5);
    EXPECT_TRUE(list.listTight);
    EXPECT_TRUE(list.isBlock());
}

// T-AST-7：TableAlign
TEST(MarkdownAstTest, T_AST7_TableAlign)
{
    AstNode cell;
    cell.type = AstNodeType::TableCell;
    cell.tableAlign = TableAlign::Center;
    EXPECT_EQ(cell.tableAlign, TableAlign::Center);
    EXPECT_TRUE(cell.isBlock());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

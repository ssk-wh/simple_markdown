#include <gtest/gtest.h>
#include "MarkdownParser.h"

TEST(MarkdownParser, EmptyInput)
{
    MarkdownParser parser;
    auto root = parser.parse("");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->type, AstNodeType::Document);
}

TEST(MarkdownParser, SimpleParagraph)
{
    MarkdownParser parser;
    auto root = parser.parse("Hello World");
    ASSERT_EQ(root->children.size(), 1u);
    EXPECT_EQ(root->children[0]->type, AstNodeType::Paragraph);
}

TEST(MarkdownParser, Heading)
{
    MarkdownParser parser;
    auto root = parser.parse("# Title\n\nParagraph");
    ASSERT_GE(root->children.size(), 2u);
    EXPECT_EQ(root->children[0]->type, AstNodeType::Heading);
    EXPECT_EQ(root->children[0]->headingLevel, 1);
}

TEST(MarkdownParser, CodeBlock)
{
    MarkdownParser parser;
    auto root = parser.parse("```cpp\nint x = 0;\n```");
    ASSERT_GE(root->children.size(), 1u);
    auto& cb = root->children[0];
    EXPECT_EQ(cb->type, AstNodeType::CodeBlock);
    EXPECT_EQ(cb->fenceInfo, "cpp");
    EXPECT_FALSE(cb->literal.isEmpty());
}

TEST(MarkdownParser, Bold)
{
    MarkdownParser parser;
    auto root = parser.parse("**bold**");
    auto& para = root->children[0];
    EXPECT_EQ(para->type, AstNodeType::Paragraph);
    bool hasStrong = false;
    for (auto& child : para->children) {
        if (child->type == AstNodeType::Strong)
            hasStrong = true;
    }
    EXPECT_TRUE(hasStrong);
}

TEST(MarkdownParser, Link)
{
    MarkdownParser parser;
    auto root = parser.parse("[click](https://example.com)");
    auto& para = root->children[0];
    bool hasLink = false;
    for (auto& child : para->children) {
        if (child->type == AstNodeType::Link) {
            EXPECT_EQ(child->url, "https://example.com");
            hasLink = true;
        }
    }
    EXPECT_TRUE(hasLink);
}

TEST(MarkdownParser, LineNumbers)
{
    MarkdownParser parser;
    auto root = parser.parse("# H1\n\nPara\n");
    ASSERT_GE(root->children.size(), 2u);
    EXPECT_EQ(root->children[0]->startLine, 0); // 0-based
    EXPECT_EQ(root->children[1]->startLine, 2);
}

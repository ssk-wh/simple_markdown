#pragma once

#include <QString>
#include <vector>
#include <memory>

enum class AstNodeType {
    Document, Paragraph, Heading, CodeBlock, BlockQuote,
    List, Item, Table, TableRow, TableCell,
    Text, Emph, Strong, Link, Image, Code,
    SoftBreak, LineBreak, ThematicBreak,
    HtmlBlock, HtmlInline, Strikethrough
};

enum class ListType { Bullet, Ordered };
enum class TableAlign { None, Left, Center, Right };

class AstNode {
public:
    AstNodeType type = AstNodeType::Document;
    QString literal;
    int startLine = 0;
    int endLine = 0;
    int headingLevel = 0;
    QString fenceInfo;
    QString url;
    QString title;
    ListType listType = ListType::Bullet;
    int listStart = 1;
    bool listTight = false;
    TableAlign tableAlign = TableAlign::None;

    std::vector<std::unique_ptr<AstNode>> children;

    bool isBlock() const;
    bool isInline() const;
    void addChild(std::unique_ptr<AstNode> child);
};

using AstNodePtr = std::unique_ptr<AstNode>;

// Spec: specs/模块-preview/10-Frontmatter渲染.md §4.1
// Last synced: 2026-04-14
#pragma once

#include <QString>
#include <vector>
#include <memory>
#include <utility>

enum class AstNodeType {
    Document, Paragraph, Heading, CodeBlock, BlockQuote,
    List, Item, Table, TableRow, TableCell,
    Text, Emph, Strong, Link, Image, Code,
    SoftBreak, LineBreak, ThematicBreak,
    HtmlBlock, HtmlInline, Strikethrough,
    Frontmatter   // Spec §4.1
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

    // Spec 模块-preview/10 §4.1：Frontmatter 专用字段
    std::vector<std::pair<QString, QString>> frontmatterEntries;
    QString frontmatterRawText;   // 原始 YAML（含起止 --- 行），用于复制

    std::vector<std::unique_ptr<AstNode>> children;

    bool isBlock() const;
    bool isInline() const;
    void addChild(std::unique_ptr<AstNode> child);
};

using AstNodePtr = std::unique_ptr<AstNode>;

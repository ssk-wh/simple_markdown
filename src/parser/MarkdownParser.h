// Spec: specs/模块-preview/10-Frontmatter渲染.md §4.2 §5.2
// Spec: specs/模块-parser/README.md
// Last synced: 2026-04-14
#pragma once

#include "MarkdownAst.h"

struct cmark_node;

class MarkdownParser {
public:
    MarkdownParser();
    ~MarkdownParser();
    AstNodePtr parse(const QString& markdown);
    // 分块渲染用：直接解析 UTF-8 片段（无 QString 往返）；结果行号整体加 lineDelta。
    // allowFrontmatter=true 时（仅文件首块）识别并生成 Frontmatter 节点。
    AstNodePtr parseChunk(const QByteArray& utf8, int lineDelta, bool allowFrontmatter);
    QString renderHtml(const QString& markdown);

    // Spec §4.2 §5.2：尝试从原文剥离 frontmatter。
    // 成功 → outBody 为剥离后正文，outNode 为 Frontmatter 节点，返回 true
    // 失败 → outBody 原样，outNode = nullptr，返回 false（INV-7）
    // outFrontmatterLineCount：被消费的行数（含起止 --- 两行），用于 §8.9 行号偏移
    bool extractFrontmatter(const QString& rawMarkdown,
                            QString& outBody,
                            AstNodePtr& outNode,
                            int& outFrontmatterLineCount);

private:
    AstNodePtr parseBody(const QByteArray& utf8);
    AstNodePtr convertNode(cmark_node* node);
    static constexpr int kMaxAstDepth = 64;  // AST 递归深度上限：防栈溢出，同时使畸形的深层嵌套（实测 5000 层列表布局 >40s）有界
    int m_depth = 0;
    AstNodeType mapNodeType(cmark_node* node);
    void shiftLineNumbers(AstNode* node, int delta);
    bool m_extensionsRegistered = false;
    void ensureExtensions();
};

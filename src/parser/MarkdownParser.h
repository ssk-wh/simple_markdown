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
    AstNodePtr convertNode(cmark_node* node);
    AstNodeType mapNodeType(cmark_node* node);
    void shiftLineNumbers(AstNode* node, int delta);
    bool m_extensionsRegistered = false;
    void ensureExtensions();
};

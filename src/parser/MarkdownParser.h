#pragma once

#include "MarkdownAst.h"

class MarkdownParser {
public:
    MarkdownParser();
    ~MarkdownParser();
    AstNodePtr parse(const QString& markdown);

private:
    struct cmark_node;
    AstNodePtr convertNode(struct cmark_node* node);
    AstNodeType mapNodeType(int cmarkType);
    bool m_extensionsRegistered = false;
    void ensureExtensions();
};

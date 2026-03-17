#pragma once

#include "MarkdownAst.h"

struct cmark_node;

class MarkdownParser {
public:
    MarkdownParser();
    ~MarkdownParser();
    AstNodePtr parse(const QString& markdown);

private:
    AstNodePtr convertNode(cmark_node* node);
    AstNodeType mapNodeType(cmark_node* node);
    bool m_extensionsRegistered = false;
    void ensureExtensions();
};

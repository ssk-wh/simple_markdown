#include "MarkdownParser.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <cstring>

MarkdownParser::MarkdownParser() = default;
MarkdownParser::~MarkdownParser() = default;

AstNodePtr MarkdownParser::parse(const QString& markdown)
{
    ensureExtensions();

    QByteArray utf8 = markdown.toUtf8();

    cmark_parser* parser = cmark_parser_new(CMARK_OPT_DEFAULT);

    cmark_syntax_extension* tableExt = cmark_find_syntax_extension("table");
    if (tableExt)
        cmark_parser_attach_syntax_extension(parser, tableExt);

    cmark_syntax_extension* strikeExt = cmark_find_syntax_extension("strikethrough");
    if (strikeExt)
        cmark_parser_attach_syntax_extension(parser, strikeExt);

    cmark_syntax_extension* tasklistExt = cmark_find_syntax_extension("tasklist");
    if (tasklistExt)
        cmark_parser_attach_syntax_extension(parser, tasklistExt);

    cmark_parser_feed(parser, utf8.data(), utf8.size());
    cmark_node* doc = cmark_parser_finish(parser);

    AstNodePtr root = convertNode(doc);

    cmark_node_free(doc);
    cmark_parser_free(parser);

    return root;
}

void MarkdownParser::ensureExtensions()
{
    if (!m_extensionsRegistered) {
        cmark_gfm_core_extensions_ensure_registered();
        m_extensionsRegistered = true;
    }
}

AstNodePtr MarkdownParser::convertNode(cmark_node* node)
{
    auto ast = std::make_unique<AstNode>();
    ast->type = mapNodeType(node);
    ast->startLine = cmark_node_get_start_line(node) - 1; // cmark 1-based -> 0-based
    ast->endLine = cmark_node_get_end_line(node) - 1;

    const char* lit = cmark_node_get_literal(node);
    if (lit)
        ast->literal = QString::fromUtf8(lit);

    if (ast->type == AstNodeType::Heading)
        ast->headingLevel = cmark_node_get_heading_level(node);

    if (ast->type == AstNodeType::CodeBlock) {
        const char* info = cmark_node_get_fence_info(node);
        if (info)
            ast->fenceInfo = QString::fromUtf8(info);
    }

    if (ast->type == AstNodeType::Link || ast->type == AstNodeType::Image) {
        const char* u = cmark_node_get_url(node);
        if (u)
            ast->url = QString::fromUtf8(u);
        const char* t = cmark_node_get_title(node);
        if (t)
            ast->title = QString::fromUtf8(t);
    }

    if (ast->type == AstNodeType::List) {
        ast->listType = (cmark_node_get_list_type(node) == CMARK_ORDERED_LIST)
                            ? ListType::Ordered
                            : ListType::Bullet;
        ast->listStart = cmark_node_get_list_start(node);
        ast->listTight = cmark_node_get_list_tight(node);
    }

    // Recurse into children
    cmark_node* child = cmark_node_first_child(node);
    while (child) {
        ast->addChild(convertNode(child));
        child = cmark_node_next(child);
    }

    return ast;
}

AstNodeType MarkdownParser::mapNodeType(cmark_node* node)
{
    int type = cmark_node_get_type(node);

    switch (type) {
    case CMARK_NODE_DOCUMENT:
        return AstNodeType::Document;
    case CMARK_NODE_PARAGRAPH:
        return AstNodeType::Paragraph;
    case CMARK_NODE_HEADING:
        return AstNodeType::Heading;
    case CMARK_NODE_CODE_BLOCK:
        return AstNodeType::CodeBlock;
    case CMARK_NODE_BLOCK_QUOTE:
        return AstNodeType::BlockQuote;
    case CMARK_NODE_LIST:
        return AstNodeType::List;
    case CMARK_NODE_ITEM:
        return AstNodeType::Item;
    case CMARK_NODE_THEMATIC_BREAK:
        return AstNodeType::ThematicBreak;
    case CMARK_NODE_HTML_BLOCK:
        return AstNodeType::HtmlBlock;
    case CMARK_NODE_TEXT:
        return AstNodeType::Text;
    case CMARK_NODE_EMPH:
        return AstNodeType::Emph;
    case CMARK_NODE_STRONG:
        return AstNodeType::Strong;
    case CMARK_NODE_LINK:
        return AstNodeType::Link;
    case CMARK_NODE_IMAGE:
        return AstNodeType::Image;
    case CMARK_NODE_CODE:
        return AstNodeType::Code;
    case CMARK_NODE_SOFTBREAK:
        return AstNodeType::SoftBreak;
    case CMARK_NODE_LINEBREAK:
        return AstNodeType::LineBreak;
    case CMARK_NODE_HTML_INLINE:
        return AstNodeType::HtmlInline;
    default:
        break;
    }

    // Handle GFM extension node types (dynamically registered)
    const char* typeStr = cmark_node_get_type_string(node);
    if (typeStr) {
        if (std::strcmp(typeStr, "table") == 0)
            return AstNodeType::Table;
        if (std::strcmp(typeStr, "table_row") == 0)
            return AstNodeType::TableRow;
        if (std::strcmp(typeStr, "table_cell") == 0)
            return AstNodeType::TableCell;
        if (std::strcmp(typeStr, "strikethrough") == 0)
            return AstNodeType::Strikethrough;
    }

    return AstNodeType::Text;
}

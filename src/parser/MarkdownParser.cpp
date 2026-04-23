// Spec: specs/模块-preview/10-Frontmatter渲染.md §5.2 §8.9
// Spec: specs/模块-parser/README.md
// Invariants enforced here: INV-1, INV-2, INV-3, INV-4, INV-5, INV-6, INV-7, INV-14
// Last synced: 2026-04-14
#include "MarkdownParser.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <cstring>

#include <QStringList>

MarkdownParser::MarkdownParser() = default;
MarkdownParser::~MarkdownParser() = default;

// Spec §4.2 §5.2：预处理剥离 frontmatter
bool MarkdownParser::extractFrontmatter(const QString& rawMarkdown,
                                        QString& outBody,
                                        AstNodePtr& outNode,
                                        int& outFrontmatterLineCount)
{
    outNode.reset();
    outFrontmatterLineCount = 0;
    outBody = rawMarkdown;

    // 1. 跳过可选 BOM（INV-2）
    QString text = rawMarkdown;
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);
    const int bomTrimmed = rawMarkdown.length() - text.length();

    // 2. 按行切分（保留空串尾项）
    const QStringList lines = text.split('\n');

    // 3. 扫首个非空行（INV-5 空行忽略）
    int i = 0;
    while (i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;
    if (i >= lines.size())
        return false;

    // 4. 首行必须严格 '---'（INV-1、INV-2）
    if (lines[i].trimmed() != QStringLiteral("---"))
        return false;

    const int start = i;
    ++i;

    // 5. 向下找结束 '---'
    int end = -1;
    while (i < lines.size()) {
        if (lines[i].trimmed() == QStringLiteral("---")) {
            end = i;
            break;
        }
        ++i;
    }
    if (end == -1)
        return false;  // INV-7 回退

    // 6. 构造 Frontmatter AST 节点
    auto node = std::make_unique<AstNode>();
    node->type = AstNodeType::Frontmatter;
    node->startLine = start;  // 0-based
    node->endLine = end;

    // rawText：起止 --- 行之间（含两行）
    QStringList rawLines;
    for (int k = start; k <= end; ++k)
        rawLines << lines[k];
    node->frontmatterRawText = rawLines.join('\n');

    // entries：逐行解析 key: value（INV-4 / INV-5 / INV-6 / INV-14）
    for (int k = start + 1; k <= end - 1; ++k) {
        const QString raw = lines[k];
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty())
            continue;                         // INV-5
        if (trimmed.startsWith(QLatin1Char('#')))
            continue;                         // INV-6

        const int colon = trimmed.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            // INV-14：无 key 的行，整行作为 value
            node->frontmatterEntries.emplace_back(QString(), trimmed);
        } else {
            QString key = trimmed.left(colon).trimmed();
            QString value = trimmed.mid(colon + 1).trimmed();  // INV-4 / 陷阱 §8.8
            node->frontmatterEntries.emplace_back(std::move(key), std::move(value));
        }
    }

    // 7. 剥离后的 body：end+1 起的所有行
    QStringList bodyLines;
    for (int k = end + 1; k < lines.size(); ++k)
        bodyLines << lines[k];
    outBody = bodyLines.join('\n');

    outNode = std::move(node);
    outFrontmatterLineCount = end + 1;  // 含起止 --- 两行
    // BOM 补偿：若原文带 BOM，仍按去 BOM 后的行数映射（BOM 不独占一行）
    (void)bomTrimmed;

    return true;
}

void MarkdownParser::shiftLineNumbers(AstNode* node, int delta)
{
    if (!node || delta == 0) return;
    // 不移动 Frontmatter 自己的行号（保持 0）
    if (node->type != AstNodeType::Frontmatter) {
        node->startLine += delta;
        node->endLine += delta;
    }
    for (auto& child : node->children)
        shiftLineNumbers(child.get(), delta);
}

AstNodePtr MarkdownParser::parse(const QString& markdown)
{
    ensureExtensions();

    // Spec §5.2：先剥离 frontmatter
    QString body;
    AstNodePtr fmNode;
    int fmLines = 0;
    const bool hasFm = extractFrontmatter(markdown, body, fmNode, fmLines);

    QByteArray utf8 = body.toUtf8();

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

    // Spec §8.9：把 cmark 返回的所有节点的行号加回剥离掉的行数
    if (hasFm && fmLines > 0) {
        for (auto& child : root->children)
            shiftLineNumbers(child.get(), fmLines);
    }

    // Spec §4.1：Frontmatter 作为 Document 首个 child
    if (hasFm && fmNode) {
        std::vector<std::unique_ptr<AstNode>> newChildren;
        newChildren.reserve(root->children.size() + 1);
        newChildren.push_back(std::move(fmNode));
        for (auto& c : root->children)
            newChildren.push_back(std::move(c));
        root->children = std::move(newChildren);
    }

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

    // 递归处理子节点
    cmark_node* child = cmark_node_first_child(node);
    while (child) {
        ast->addChild(convertNode(child));
        child = cmark_node_next(child);
    }

    return ast;
}

QString MarkdownParser::renderHtml(const QString& markdown)
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

    cmark_llist* extensions = cmark_parser_get_syntax_extensions(parser);
    char* html = cmark_render_html(doc, CMARK_OPT_DEFAULT, extensions);
    QString result = QString::fromUtf8(html);

    free(html);
    cmark_node_free(doc);
    cmark_parser_free(parser);

    return result;
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

    // 处理 GFM 扩展节点类型（动态注册）
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

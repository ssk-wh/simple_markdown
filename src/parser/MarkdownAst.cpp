#include "MarkdownAst.h"

bool AstNode::isBlock() const
{
    switch (type) {
    case AstNodeType::Document:
    case AstNodeType::Paragraph:
    case AstNodeType::Heading:
    case AstNodeType::CodeBlock:
    case AstNodeType::BlockQuote:
    case AstNodeType::List:
    case AstNodeType::Item:
    case AstNodeType::Table:
    case AstNodeType::TableRow:
    case AstNodeType::TableCell:
    case AstNodeType::ThematicBreak:
    case AstNodeType::HtmlBlock:
    case AstNodeType::Frontmatter:
        return true;
    default:
        return false;
    }
}

bool AstNode::isInline() const
{
    return !isBlock();
}

void AstNode::addChild(std::unique_ptr<AstNode> child)
{
    children.push_back(std::move(child));
}

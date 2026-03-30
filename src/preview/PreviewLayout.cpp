#include "PreviewLayout.h"
#include "MarkdownAst.h"

#include <QFontMetricsF>
#include <QtMath>
#include <algorithm>

PreviewLayout::PreviewLayout()
{
    m_baseFont = QFont("Segoe UI", 10);
    m_monoFont = QFont("Consolas", 9);
    m_monoFont.setStyleHint(QFont::Monospace);

    QFontMetricsF fm(m_baseFont);
    m_lineHeight = fm.height() * 1.5;
    QFontMetricsF fmCode(m_monoFont);
    m_codeLineHeight = fmCode.height() * 1.4;
}

PreviewLayout::~PreviewLayout() = default;

void PreviewLayout::setViewportWidth(qreal width)
{
    m_viewportWidth = width;
}

void PreviewLayout::setFont(const QFont& baseFont)
{
    m_baseFont = baseFont;
    QFontMetricsF fm(m_baseFont);
    m_lineHeight = fm.height() * 1.5;
}

void PreviewLayout::updateMetrics(QPaintDevice* device)
{
    // 在高 DPI 下，必须使用 device 参数确保字体度量与渲染一致
    QFontMetricsF fm(m_baseFont, device);
    m_lineHeight = fm.height() * 1.5;
    QFontMetricsF fmCode(m_monoFont, device);
    m_codeLineHeight = fmCode.height() * 1.4;
}

void PreviewLayout::setTheme(const Theme& theme)
{
    m_theme = theme;
}

void PreviewLayout::buildFromAst(const std::shared_ptr<AstNode>& root)
{
    m_root = LayoutBlock();
    m_root.type = LayoutBlock::Document;

    if (!root) return;

    qreal y = 0;
    for (const auto& child : root->children) {
        LayoutBlock block = layoutBlock(child.get(), m_viewportWidth);
        block.bounds.moveTop(y);
        y += block.bounds.height() + 12.0; // 12px spacing between blocks
        m_root.children.push_back(std::move(block));
    }
    m_root.bounds = QRectF(0, 0, m_viewportWidth, y);
}

qreal PreviewLayout::totalHeight() const
{
    return m_root.bounds.height();
}

const LayoutBlock& PreviewLayout::rootBlock() const
{
    return m_root;
}

LayoutBlock PreviewLayout::layoutBlock(const AstNode* node, qreal maxWidth)
{
    LayoutBlock block;
    block.sourceStartLine = node->startLine;
    block.sourceEndLine = node->endLine;

    switch (node->type) {
    case AstNodeType::Paragraph: {
        block.type = LayoutBlock::Paragraph;
        collectInlineRuns(node, block.inlineRuns, m_baseFont, m_theme.previewFg);
        qreal h = estimateParagraphHeight(block.inlineRuns, maxWidth);
        block.bounds = QRectF(0, 0, maxWidth, h);
        break;
    }
    case AstNodeType::Heading: {
        block.type = LayoutBlock::Heading;
        block.headingLevel = node->headingLevel;

        QFont headingFont = m_baseFont;
        qreal scale = 1.0;
        switch (node->headingLevel) {
        case 1: scale = 1.8; break;
        case 2: scale = 1.5; break;
        case 3: scale = 1.3; break;
        case 4: scale = 1.15; break;
        case 5: scale = 1.05; break;
        default: scale = 1.0; break;
        }
        headingFont.setPointSizeF(m_baseFont.pointSizeF() * scale);
        headingFont.setWeight(QFont::Bold);

        collectInlineRuns(node, block.inlineRuns, headingFont, m_theme.previewHeading);
        qreal h = estimateParagraphHeight(block.inlineRuns, maxWidth);
        // Extra bottom margin for H1/H2
        if (node->headingLevel <= 2) h += 8;
        block.bounds = QRectF(0, 0, maxWidth, h);
        break;
    }
    case AstNodeType::CodeBlock: {
        block.type = LayoutBlock::CodeBlock;
        block.codeText = node->literal;
        block.language = node->fenceInfo;
        int lineCount = qMax(1, block.codeText.count('\n') + (block.codeText.endsWith('\n') ? 0 : 1));
        qreal h = lineCount * m_codeLineHeight + 16.0; // 8px padding top+bottom
        block.bounds = QRectF(0, 0, maxWidth, h);
        break;
    }
    case AstNodeType::BlockQuote: {
        block.type = LayoutBlock::BlockQuote;
        qreal indent = 19.0; // 3px line + 16px spacing
        qreal childMaxWidth = maxWidth - indent;
        qreal y = 8.0; // top padding
        for (const auto& child : node->children) {
            LayoutBlock childBlock = layoutBlock(child.get(), childMaxWidth);
            childBlock.bounds.moveLeft(indent);
            childBlock.bounds.moveTop(y);
            y += childBlock.bounds.height() + 8.0;
            block.children.push_back(std::move(childBlock));
        }
        y += 4.0; // bottom padding
        block.bounds = QRectF(0, 0, maxWidth, y);
        break;
    }
    case AstNodeType::List: {
        block.type = LayoutBlock::List;
        block.ordered = (node->listType == ListType::Ordered);
        block.listStart = node->listStart;
        qreal indent = 24.0;
        qreal childMaxWidth = maxWidth - indent;
        qreal y = 0;
        for (const auto& child : node->children) {
            LayoutBlock childBlock = layoutBlock(child.get(), childMaxWidth);
            childBlock.bounds.moveLeft(indent);
            childBlock.bounds.moveTop(y);
            y += childBlock.bounds.height() + 4.0;
            block.children.push_back(std::move(childBlock));
        }
        block.bounds = QRectF(0, 0, maxWidth, y);
        break;
    }
    case AstNodeType::Item: {
        block.type = LayoutBlock::ListItem;
        qreal y = 0;
        for (const auto& child : node->children) {
            LayoutBlock childBlock = layoutBlock(child.get(), maxWidth);
            childBlock.bounds.moveTop(y);
            y += childBlock.bounds.height() + 4.0;
            block.children.push_back(std::move(childBlock));
        }
        block.bounds = QRectF(0, 0, maxWidth, qMax(y, m_lineHeight));
        break;
    }
    case AstNodeType::Table: {
        block.type = LayoutBlock::Table;
        // Count columns from first row
        int cols = 0;
        if (!node->children.empty()) {
            cols = static_cast<int>(node->children[0]->children.size());
        }
        if (cols == 0) cols = 1;
        qreal colWidth = maxWidth / cols;
        block.columnWidths.resize(cols, colWidth);

        qreal y = 0;
        qreal cellPadding = 8.0;
        for (const auto& rowNode : node->children) {
            LayoutBlock rowBlock;
            rowBlock.type = LayoutBlock::TableRow;
            rowBlock.sourceStartLine = rowNode->startLine;
            rowBlock.sourceEndLine = rowNode->endLine;

            // First pass: collect inline runs and compute row height
            qreal rowHeight = m_lineHeight + cellPadding;
            std::vector<LayoutBlock> cellBlocks;
            for (const auto& cellNode : rowNode->children) {
                LayoutBlock cellBlock;
                cellBlock.type = LayoutBlock::TableCell;
                cellBlock.sourceStartLine = cellNode->startLine;
                cellBlock.sourceEndLine = cellNode->endLine;
                collectInlineRuns(cellNode.get(), cellBlock.inlineRuns, m_baseFont, m_theme.previewFg);
                qreal cellContentH = estimateParagraphHeight(cellBlock.inlineRuns, colWidth - cellPadding);
                rowHeight = qMax(rowHeight, cellContentH + cellPadding);
                cellBlocks.push_back(std::move(cellBlock));
            }

            // Second pass: assign bounds with computed row height
            qreal cellX = 0;
            for (auto& cellBlock : cellBlocks) {
                cellBlock.bounds = QRectF(cellX, 0, colWidth, rowHeight);
                cellX += colWidth;
                rowBlock.children.push_back(std::move(cellBlock));
            }
            rowBlock.bounds = QRectF(0, y, maxWidth, rowHeight);
            y += rowHeight;
            block.children.push_back(std::move(rowBlock));
        }
        block.bounds = QRectF(0, 0, maxWidth, y);
        break;
    }
    case AstNodeType::Image: {
        block.type = LayoutBlock::Image;
        block.imageUrl = node->url;
        block.bounds = QRectF(0, 0, maxWidth, 200.0);
        break;
    }
    case AstNodeType::ThematicBreak: {
        block.type = LayoutBlock::ThematicBreak;
        block.bounds = QRectF(0, 0, maxWidth, 33.0);
        break;
    }
    case AstNodeType::HtmlBlock: {
        block.type = LayoutBlock::HtmlBlock;
        block.codeText = node->literal;
        int lineCount = qMax(1, block.codeText.count('\n') + 1);
        qreal h = lineCount * m_codeLineHeight + 16.0;
        block.bounds = QRectF(0, 0, maxWidth, h);
        break;
    }
    default: {
        // For unknown block types, try to layout children
        block.type = LayoutBlock::Paragraph;
        collectInlineRuns(node, block.inlineRuns, m_baseFont, m_theme.previewFg);
        qreal h = estimateParagraphHeight(block.inlineRuns, maxWidth);
        block.bounds = QRectF(0, 0, maxWidth, qMax(h, m_lineHeight));
        break;
    }
    }

    return block;
}

void PreviewLayout::collectInlineRuns(const AstNode* node, std::vector<InlineRun>& runs,
                                       QFont currentFont, QColor currentColor)
{
    for (const auto& child : node->children) {
        switch (child->type) {
        case AstNodeType::Text: {
            InlineRun run;
            run.text = child->literal;
            run.font = currentFont;
            run.color = currentColor;
            runs.push_back(std::move(run));
            break;
        }
        case AstNodeType::Emph: {
            QFont italicFont = currentFont;
            italicFont.setItalic(true);
            collectInlineRuns(child.get(), runs, italicFont, currentColor);
            break;
        }
        case AstNodeType::Strong: {
            QFont boldFont = currentFont;
            boldFont.setWeight(QFont::Bold);
            collectInlineRuns(child.get(), runs, boldFont, currentColor);
            break;
        }
        case AstNodeType::Strikethrough: {
            InlineRun run;
            // Collect child text
            std::vector<InlineRun> subRuns;
            collectInlineRuns(child.get(), subRuns, currentFont, currentColor);
            for (auto& sr : subRuns) {
                sr.isStrikethrough = true;
                runs.push_back(std::move(sr));
            }
            break;
        }
        case AstNodeType::Code: {
            InlineRun run;
            run.text = child->literal;
            run.font = m_monoFont;
            run.font.setPointSizeF(currentFont.pointSizeF() * 0.9);
            run.color = m_theme.previewCodeFg;
            run.bgColor = m_theme.previewCodeBg;
            runs.push_back(std::move(run));
            break;
        }
        case AstNodeType::Link: {
            QColor linkColor = m_theme.previewLink;
            collectInlineRuns(child.get(), runs, currentFont, linkColor);
            // Set linkUrl on all runs added
            // Find runs we just added and set their URL
            // Simple approach: mark from current size
            // (We already collected with blue color; set URL on them)
            for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
                if (it->color == linkColor && it->linkUrl.isEmpty()) {
                    it->linkUrl = child->url;
                } else {
                    break;
                }
            }
            break;
        }
        case AstNodeType::Image: {
            // Inline image - treat as text placeholder
            InlineRun run;
            run.text = child->title.isEmpty() ? QStringLiteral("[image]") : child->title;
            run.font = currentFont;
            run.color = m_theme.previewImagePlaceholderText;
            runs.push_back(std::move(run));
            break;
        }
        case AstNodeType::SoftBreak: {
            InlineRun run;
            run.text = QStringLiteral(" ");
            run.font = currentFont;
            run.color = currentColor;
            runs.push_back(std::move(run));
            break;
        }
        case AstNodeType::LineBreak: {
            InlineRun run;
            run.text = QStringLiteral("\n");
            run.font = currentFont;
            run.color = currentColor;
            runs.push_back(std::move(run));
            break;
        }
        case AstNodeType::HtmlInline: {
            // Render as raw text
            InlineRun run;
            run.text = child->literal;
            run.font = currentFont;
            run.color = m_theme.syntaxFence;
            runs.push_back(std::move(run));
            break;
        }
        default:
            // Recurse into any other inline container
            collectInlineRuns(child.get(), runs, currentFont, currentColor);
            break;
        }
    }
}

qreal PreviewLayout::estimateParagraphHeight(const std::vector<InlineRun>& runs, qreal maxWidth) const
{
    if (runs.empty()) return m_lineHeight;

    qreal totalWidth = 0;
    qreal lineHeight = m_lineHeight;
    int newlineCount = 0;

    for (const auto& run : runs) {
        if (run.text == "\n") {
            newlineCount++;
            continue;
        }
        QFontMetricsF fm(run.font);
        totalWidth += fm.horizontalAdvance(run.text);
        qreal runLineH = fm.height() * 1.5;
        if (runLineH > lineHeight) lineHeight = runLineH;
    }

    int wrappedLines = qMax(1, static_cast<int>(qCeil(totalWidth / qMax(maxWidth, 1.0))));
    int totalLines = wrappedLines + newlineCount;
    return totalLines * lineHeight;
}

void PreviewLayout::collectSourceMappings(const LayoutBlock& block, qreal offsetY,
                                           std::vector<std::pair<int, qreal>>& mappings) const
{
    qreal absY = offsetY + block.bounds.y();
    if (block.sourceStartLine >= 0) {
        mappings.emplace_back(block.sourceStartLine, absY);
    }
    for (const auto& child : block.children) {
        collectSourceMappings(child, absY, mappings);
    }
}

qreal PreviewLayout::sourceLineToY(int sourceLine) const
{
    std::vector<std::pair<int, qreal>> mappings;
    collectSourceMappings(m_root, 0, mappings);

    if (mappings.empty()) return 0;

    std::sort(mappings.begin(), mappings.end());

    // Binary search for closest source line
    auto it = std::lower_bound(mappings.begin(), mappings.end(),
                                std::make_pair(sourceLine, 0.0));

    if (it == mappings.end()) {
        return mappings.back().second;
    }
    if (it == mappings.begin()) {
        return it->second;
    }
    if (it->first == sourceLine) {
        return it->second;
    }

    // Interpolate between surrounding mappings
    auto prev = std::prev(it);
    if (it->first == prev->first) return it->second;
    qreal ratio = static_cast<qreal>(sourceLine - prev->first) /
                  static_cast<qreal>(it->first - prev->first);
    return prev->second + ratio * (it->second - prev->second);
}

int PreviewLayout::yToSourceLine(qreal y) const
{
    std::vector<std::pair<int, qreal>> mappings;
    collectSourceMappings(m_root, 0, mappings);

    if (mappings.empty()) return 0;

    // Sort by Y position
    std::sort(mappings.begin(), mappings.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Find closest Y
    auto it = std::lower_bound(mappings.begin(), mappings.end(),
                                std::make_pair(0, y),
                                [](const auto& a, const auto& b) { return a.second < b.second; });

    if (it == mappings.end()) {
        return mappings.back().first;
    }
    if (it == mappings.begin()) {
        return it->first;
    }

    auto prev = std::prev(it);
    if (qAbs(y - prev->second) < qAbs(y - it->second)) {
        return prev->first;
    }
    return it->first;
}

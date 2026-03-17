#include "PreviewPainter.h"

#include <QFontMetricsF>

PreviewPainter::PreviewPainter()
    : m_theme(Theme::light())
{
}

PreviewPainter::~PreviewPainter() = default;

void PreviewPainter::setTheme(const Theme& theme)
{
    m_theme = theme;
}

void PreviewPainter::paint(QPainter* painter, const LayoutBlock& root,
                            qreal scrollY, qreal viewportHeight, qreal viewportWidth)
{
    if (!painter) return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    for (const auto& child : root.children) {
        paintBlock(painter, child, 0, 0, scrollY, viewportHeight, viewportWidth);
    }
}

void PreviewPainter::paintBlock(QPainter* p, const LayoutBlock& block,
                                 qreal offsetX, qreal offsetY,
                                 qreal scrollY, qreal viewportHeight, qreal viewportWidth)
{
    qreal absX = offsetX + block.bounds.x();
    qreal absY = offsetY + block.bounds.y();
    qreal blockBottom = absY + block.bounds.height();

    // Culling: skip blocks entirely outside viewport
    if (blockBottom < scrollY - 50 || absY > scrollY + viewportHeight + 50) {
        return;
    }

    // Translate to viewport coordinates
    qreal drawX = absX;
    qreal drawY = absY - scrollY;

    switch (block.type) {
    case LayoutBlock::Paragraph: {
        paintInlineRuns(p, block, drawX, drawY, block.bounds.width());
        break;
    }
    case LayoutBlock::Heading: {
        paintInlineRuns(p, block, drawX, drawY, block.bounds.width());
        // H1/H2: bottom separator line
        if (block.headingLevel <= 2) {
            qreal lineY = drawY + block.bounds.height() - 4;
            p->setPen(QPen(m_theme.previewHeadingSeparator, 1));
            p->drawLine(QPointF(drawX, lineY), QPointF(drawX + block.bounds.width(), lineY));
        }
        break;
    }
    case LayoutBlock::CodeBlock:
    case LayoutBlock::HtmlBlock: {
        // Background
        QRectF bgRect(drawX, drawY, block.bounds.width(), block.bounds.height());
        p->fillRect(bgRect, m_theme.previewCodeBg);
        p->setPen(QPen(m_theme.previewCodeBorder, 1));
        p->drawRoundedRect(bgRect, 4, 4);

        // Draw code text
        QFont monoFont("Consolas", 9);
        monoFont.setStyleHint(QFont::Monospace);
        p->setFont(monoFont);
        p->setPen(m_theme.previewCodeFg);
        QFontMetricsF fm(monoFont);
        qreal lineH = fm.height() * 1.4;
        qreal textX = drawX + 8;
        qreal textY = drawY + 8;

        const QStringList lines = block.codeText.split('\n');
        for (const auto& line : lines) {
            p->drawText(QPointF(textX, textY + fm.ascent()), line);
            textY += lineH;
        }
        break;
    }
    case LayoutBlock::BlockQuote: {
        // Background
        QRectF bgRect(drawX, drawY, block.bounds.width(), block.bounds.height());
        p->fillRect(bgRect, m_theme.previewBlockQuoteBg);

        // Left bar
        p->fillRect(QRectF(drawX, drawY, 3, block.bounds.height()), m_theme.previewBlockQuoteBorder);

        // Children
        for (const auto& child : block.children) {
            paintBlock(p, child, absX, absY, scrollY, viewportHeight, viewportWidth);
        }
        break;
    }
    case LayoutBlock::List: {
        int itemIndex = 0;
        for (const auto& child : block.children) {
            qreal itemAbsY = absY + child.bounds.y();
            qreal itemDrawY = itemAbsY - scrollY;
            qreal bulletX = drawX;

            // Draw bullet/number
            QFont baseFont("Segoe UI", 10);
            p->setFont(baseFont);
            p->setPen(m_theme.previewFg);
            QFontMetricsF fm(baseFont);

            if (block.ordered) {
                QString num = QString::number(block.listStart + itemIndex) + ".";
                p->drawText(QPointF(bulletX, itemDrawY + fm.ascent()), num);
            } else {
                // Unicode bullet
                p->drawText(QPointF(bulletX + 4, itemDrawY + fm.ascent()),
                            QStringLiteral("\u2022"));
            }

            // Paint child block contents
            paintBlock(p, child, absX, absY, scrollY, viewportHeight, viewportWidth);
            itemIndex++;
        }
        break;
    }
    case LayoutBlock::ListItem: {
        for (const auto& child : block.children) {
            paintBlock(p, child, absX, absY, scrollY, viewportHeight, viewportWidth);
        }
        break;
    }
    case LayoutBlock::Table: {
        // Draw grid
        p->setPen(QPen(m_theme.previewTableBorder, 1));

        bool isFirstRow = true;
        for (const auto& row : block.children) {
            qreal rowAbsY = absY + row.bounds.y();
            qreal rowDrawY = rowAbsY - scrollY;
            qreal rowHeight = row.bounds.height();

            // Header row background
            if (isFirstRow) {
                p->fillRect(QRectF(drawX, rowDrawY, block.bounds.width(), rowHeight),
                            m_theme.previewTableHeaderBg);
            }

            // Row border
            p->setPen(QPen(m_theme.previewTableBorder, 1));
            p->drawLine(QPointF(drawX, rowDrawY + rowHeight),
                        QPointF(drawX + block.bounds.width(), rowDrawY + rowHeight));

            // Cells
            for (const auto& cell : row.children) {
                qreal cellX = drawX + cell.bounds.x();
                qreal cellY = rowDrawY;

                // Column border
                p->setPen(QPen(m_theme.previewTableBorder, 1));
                p->drawLine(QPointF(cellX, rowDrawY),
                            QPointF(cellX, rowDrawY + rowHeight));

                // Cell text
                QFont cellFont("Segoe UI", 10);
                if (isFirstRow) cellFont.setWeight(QFont::Bold);
                p->setFont(cellFont);
                p->setPen(m_theme.previewFg);

                if (!cell.inlineRuns.empty()) {
                    paintInlineRuns(p, cell, cellX + 4, cellY + 4, cell.bounds.width() - 8);
                }
            }

            // Right border
            p->setPen(QPen(m_theme.previewTableBorder, 1));
            p->drawLine(QPointF(drawX + block.bounds.width(), rowDrawY),
                        QPointF(drawX + block.bounds.width(), rowDrawY + rowHeight));

            isFirstRow = false;
        }
        break;
    }
    case LayoutBlock::Image: {
        // Placeholder
        QRectF rect(drawX, drawY, block.bounds.width(), block.bounds.height());
        p->fillRect(rect, m_theme.previewImagePlaceholderBg);
        p->setPen(QPen(m_theme.previewImagePlaceholderBorder, 1));
        p->drawRect(rect);
        p->setPen(m_theme.previewImagePlaceholderText);
        QFont f("Segoe UI", 10);
        p->setFont(f);
        QString label = block.imageUrl.isEmpty()
                            ? QStringLiteral("Loading...")
                            : QStringLiteral("Image: ") + block.imageUrl;
        p->drawText(rect, Qt::AlignCenter, label);
        break;
    }
    case LayoutBlock::ThematicBreak: {
        qreal lineY = drawY + block.bounds.height() / 2.0;
        p->setPen(QPen(m_theme.previewHr, 2));
        p->drawLine(QPointF(drawX, lineY), QPointF(drawX + block.bounds.width(), lineY));
        break;
    }
    default:
        for (const auto& child : block.children) {
            paintBlock(p, child, absX, absY, scrollY, viewportHeight, viewportWidth);
        }
        break;
    }
}

void PreviewPainter::paintInlineRuns(QPainter* p, const LayoutBlock& block,
                                      qreal x, qreal y, qreal maxWidth)
{
    if (block.inlineRuns.empty()) return;

    qreal curX = x;
    qreal curY = y;
    QFontMetricsF defaultFm(block.inlineRuns[0].font);
    qreal lineHeight = defaultFm.height() * 1.5;

    for (const auto& run : block.inlineRuns) {
        p->setFont(run.font);
        p->setPen(run.color);

        if (run.text == "\n") {
            curX = x;
            curY += lineHeight;
            continue;
        }

        QFontMetricsF fm(run.font);

        // Inline code background
        if (run.bgColor.isValid() && run.bgColor != Qt::transparent) {
            qreal w = fm.horizontalAdvance(run.text);
            if (curX + w > x + maxWidth && curX > x) {
                curX = x;
                curY += lineHeight;
            }
            p->fillRect(QRectF(curX - 2, curY, w + 4, lineHeight), run.bgColor);
        }

        // Simple word-wrap
        const QStringList words = run.text.split(' ', Qt::SkipEmptyParts);
        for (int i = 0; i < words.size(); ++i) {
            QString word = words[i];
            if (i > 0) word = " " + word;
            qreal wordWidth = fm.horizontalAdvance(word);

            if (curX + wordWidth > x + maxWidth && curX > x) {
                curX = x;
                curY += lineHeight;
            }

            p->drawText(QPointF(curX, curY + fm.ascent()), word);

            // Link underline
            if (!run.linkUrl.isEmpty()) {
                p->drawLine(QPointF(curX, curY + fm.ascent() + 2),
                            QPointF(curX + wordWidth, curY + fm.ascent() + 2));
            }

            // Strikethrough
            if (run.isStrikethrough) {
                qreal strikeY = curY + fm.ascent() / 2;
                p->drawLine(QPointF(curX, strikeY), QPointF(curX + wordWidth, strikeY));
            }

            curX += wordWidth;
        }

        // Handle whitespace-only runs (e.g., soft breaks rendered as " ")
        if (words.isEmpty() && !run.text.isEmpty()) {
            qreal w = fm.horizontalAdvance(run.text);
            curX += w;
        }
    }
}

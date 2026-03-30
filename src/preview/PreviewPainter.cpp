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

void PreviewPainter::setSelection(int selStart, int selEnd)
{
    m_selStart = qMin(selStart, selEnd);
    m_selEnd = qMax(selStart, selEnd);
}

void PreviewPainter::setHighlights(const QVector<QPair<int,int>>& highlights)
{
    m_highlights = highlights;
}

void PreviewPainter::recordSegment(const QRectF& rect, int charStart, int charLen,
                                    const QString& text, const QFont& font)
{
    m_textSegments.append({rect, charStart, charLen, text, font});
}

void PreviewPainter::paint(QPainter* painter, const LayoutBlock& root,
                            qreal scrollY, qreal viewportHeight, qreal viewportWidth)
{
    if (!painter) return;

    m_textSegments.clear();
    m_charCounter = 0;

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

    // Culling: skip blocks entirely outside viewport, but count their chars
    if (blockBottom < scrollY - 50 || absY > scrollY + viewportHeight + 50) {
        countBlockChars(block);
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
        QFontMetricsF fm(monoFont, p->device());  // 使用 painter 的设备确保高 DPI 下度量正确
        qreal textHeight = fm.ascent() + fm.descent();  // 文本实际高度
        qreal lineH = fm.height() * 1.4;  // 行高（包括间距）
        qreal textX = drawX + 8;
        qreal textY = drawY + 8;

        const QStringList lines = block.codeText.split('\n');
        for (int li = 0; li < lines.size(); ++li) {
            const auto& line = lines[li];
            // 跳过 split 产生的尾部空元素，与 extractBlockText 保持一致
            if (li == lines.size() - 1 && line.isEmpty())
                break;

            qreal w = fm.horizontalAdvance(line);
            // segRect 用于鼠标点击定位，必须与选区高亮矩形高度一致
            QRectF segRect(textX, textY, w, textHeight);

            int segStart = m_charCounter;
            int segEnd = segStart + line.length();

            // 标记高亮（先绘制，作为底层）
            for (const auto& hl : m_highlights) {
                int hlS = qMax(segStart, hl.first);
                int hlE = qMin(segEnd, hl.second);
                if (hlS < hlE) {
                    qreal x1 = fm.horizontalAdvance(line.left(hlS - segStart));
                    qreal x2 = fm.horizontalAdvance(line.left(hlE - segStart));
                    p->fillRect(QRectF(textX + x1, textY, x2 - x1, textHeight),
                                m_theme.previewHighlight);
                }
            }

            // 选区高亮 - 使用文本实际高度而不是行高
            if (m_selStart >= 0 && m_selEnd > m_selStart) {
                int hlStart = qMax(segStart, m_selStart);
                int hlEnd = qMin(segEnd, m_selEnd);
                if (hlStart < hlEnd) {
                    qreal x1 = fm.horizontalAdvance(line.left(hlStart - segStart));
                    qreal x2 = fm.horizontalAdvance(line.left(hlEnd - segStart));
                    p->fillRect(QRectF(textX + x1, textY, x2 - x1, textHeight),
                                QColor(0, 120, 215, 80));
                }
            }

            recordSegment(segRect, m_charCounter, line.length(), line, monoFont);
            p->drawText(QPointF(textX, textY + fm.ascent()), line);
            m_charCounter += line.length() + 1; // +1 for '\n'
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

    QColor selColor(0, 120, 215, 80);

    auto drawSelectionHighlight = [&](const QString& segText, qreal sx, qreal sy, qreal sw, qreal sh, int charStart, int charLen) {
        QFont curFont = p->font();
        recordSegment(QRectF(sx, sy, sw, sh), charStart, charLen, segText, curFont);

        // 标记高亮（先绘制，作为底层）
        if (charLen > 0) {
            int segEnd = charStart + charLen;
            for (const auto& hl : m_highlights) {
                int hlS = qMax(charStart, hl.first);
                int hlE = qMin(segEnd, hl.second);
                if (hlS < hlE) {
                    QFontMetricsF segFm(curFont);
                    qreal x1 = segFm.horizontalAdvance(segText.left(hlS - charStart));
                    qreal x2 = segFm.horizontalAdvance(segText.left(hlE - charStart));
                    p->fillRect(QRectF(sx + x1, sy, x2 - x1, sh), m_theme.previewHighlight);
                }
            }
        }

        // 选区高亮（后绘制，叠加在上）
        if (m_selStart >= 0 && m_selEnd > m_selStart && charLen > 0) {
            int segEnd = charStart + charLen;
            int hlStart = qMax(charStart, m_selStart);
            int hlEnd = qMin(segEnd, m_selEnd);
            if (hlStart < hlEnd) {
                // 使用字体度量精确定位高亮区域
                QFontMetricsF segFm(curFont);
                qreal x1 = segFm.horizontalAdvance(segText.left(hlStart - charStart));
                qreal x2 = segFm.horizontalAdvance(segText.left(hlEnd - charStart));
                p->fillRect(QRectF(sx + x1, sy, x2 - x1, sh), selColor);
            }
        }
    };

    for (const auto& run : block.inlineRuns) {
        p->setFont(run.font);
        p->setPen(run.color);

        if (run.text == "\n") {
            m_charCounter++; // count the newline
            curX = x;
            curY += lineHeight;
            continue;
        }

        // 关键修复：使用 painter 的设备来计算度量，确保与实际绘制宽度一致
        // 原因：painter 会根据目标设备调整字体渲染，度量必须与之匹配
        QFontMetricsF fm(run.font, p->device());

        bool hasBg = run.bgColor.isValid() && run.bgColor != Qt::transparent;

        // Helper: draw a segment with background, selection, decorations
        auto drawSegment = [&](const QString& seg, qreal segW, int segOffset) {
            // 反引号和内联代码背景色绘制
            // drawText 在 (curX, curY + fm.ascent()) 以基线绘制
            // 文本范围 Y：从 curY 到 curY + fm.height()
            // 背景、选区高亮必须使用一致的高度，避免 DPI 切换时产生空白
            qreal textHeight = fm.height();  // 文本实际高度

            if (hasBg) {
                p->fillRect(QRectF(curX, curY, segW, textHeight), run.bgColor);
            }
            drawSelectionHighlight(seg, curX, curY, segW, textHeight,
                                   m_charCounter + segOffset, seg.length());
            p->drawText(QPointF(curX, curY + fm.ascent()), seg);
            if (!run.linkUrl.isEmpty())
                p->drawLine(QPointF(curX, curY + fm.ascent() + 2), QPointF(curX + segW, curY + fm.ascent() + 2));
            if (run.isStrikethrough)
                p->drawLine(QPointF(curX, curY + fm.ascent() / 2), QPointF(curX + segW, curY + fm.ascent() / 2));
        };

        // Find how many chars fit within 'remaining' width using incremental measurement
        auto findFitCount = [&](const QString& str, qreal remaining) -> int {
            int fit = 0;
            qreal accW = 0;
            for (int i = 0; i < str.length(); ++i) {
                accW += fm.horizontalAdvance(str[i]);
                if (accW > remaining) break;
                fit = i + 1;
            }
            return fit;
        };

        // Draw run text with character-level line-wrap
        QString text = run.text;
        int textOffset = 0;
        while (!text.isEmpty()) {
            qreal fullWidth = fm.horizontalAdvance(text);
            qreal remaining = x + maxWidth - curX;

            // Fits on current line?
            if (fullWidth <= remaining || curX <= x) {
                // At line start but still doesn't fit: split at char boundary
                if (fullWidth > remaining && curX <= x) {
                    int fitCount = findFitCount(text, remaining);
                    if (fitCount <= 0) fitCount = 1;

                    QString segment = text.left(fitCount);
                    qreal segW = fm.horizontalAdvance(segment);
                    drawSegment(segment, segW, textOffset);

                    textOffset += fitCount;
                    curX = x;
                    curY += lineHeight;
                    text = text.mid(fitCount);
                    continue;
                }
                drawSegment(text, fullWidth, textOffset);
                curX += fullWidth;
                break;
            }

            // Need to wrap: find last char that fits
            int wrapAt = findFitCount(text, remaining);

            if (wrapAt <= 0) {
                // Nothing fits here (curX > x), move to next line and retry
                curX = x;
                curY += lineHeight;
                continue;
            }

            QString segment = text.left(wrapAt);
            qreal segW = fm.horizontalAdvance(segment);
            drawSegment(segment, segW, textOffset);

            textOffset += wrapAt;
            curX = x;
            curY += lineHeight;
            text = text.mid(wrapAt);
        }
        m_charCounter += run.text.length();
    }
    // Block separator newline
    m_charCounter++;
}

void PreviewPainter::countBlockChars(const LayoutBlock& block)
{
    // Count inline runs (Paragraph, Heading, TableCell, etc.)
    for (const auto& run : block.inlineRuns) {
        m_charCounter += run.text.length();
    }
    if (!block.inlineRuns.empty())
        m_charCounter++; // block separator newline

    // Count code block text
    if (!block.codeText.isEmpty()) {
        const QStringList lines = block.codeText.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (i == lines.size() - 1 && lines[i].isEmpty())
                break;
            m_charCounter += lines[i].length() + 1;
        }
    }

    // Recurse into children
    for (const auto& child : block.children) {
        countBlockChars(child);
    }
}

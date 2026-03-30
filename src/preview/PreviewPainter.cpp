#include "PreviewPainter.h"

#include <QFontMetricsF>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

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

#ifdef ENABLE_TEST_MODE
    m_blockInfos.clear();

    // [测试模式调试] 输出调试信息
    QString debugPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/render_blocks_debug.txt";
    QFile debugFile(debugPath);
    if (debugFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        QTextStream stream(&debugFile);
        stream << QString("paint() called: viewport=%1x%2\n")
                  .arg((int)viewportWidth).arg((int)viewportHeight);
        debugFile.close();
    }
#endif

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    for (const auto& child : root.children) {
        paintBlock(painter, child, 0, 0, scrollY, viewportHeight, viewportWidth);
    }

#ifdef ENABLE_TEST_MODE
    saveBlocksToJson((int)viewportWidth, (int)viewportHeight);
#endif
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

#ifdef ENABLE_TEST_MODE
    // [测试模式] 记录块信息供自动化测试验证
    BlockInfo blockInfo;
    blockInfo.x = (int)drawX;
    blockInfo.y = (int)drawY;
    blockInfo.width = (int)block.bounds.width();
    blockInfo.height = (int)block.bounds.height();
    blockInfo.headingLevel = block.headingLevel;
    blockInfo.listLevel = 0;  // LayoutBlock 没有 listLevel，仅用于兼容 BlockInfo

    // 确定块类型名称
    switch (block.type) {
    case LayoutBlock::Paragraph: blockInfo.type = "paragraph"; break;
    case LayoutBlock::Heading: blockInfo.type = "heading"; break;
    case LayoutBlock::CodeBlock: blockInfo.type = "code_block"; break;
    case LayoutBlock::HtmlBlock: blockInfo.type = "html_block"; break;
    case LayoutBlock::BlockQuote: blockInfo.type = "blockquote"; break;
    case LayoutBlock::List: blockInfo.type = "list"; break;
    case LayoutBlock::ListItem: blockInfo.type = "list_item"; break;
    case LayoutBlock::Table: blockInfo.type = "table"; break;
    case LayoutBlock::Image: blockInfo.type = "image"; break;
    case LayoutBlock::ThematicBreak: blockInfo.type = "thematic_break"; break;
    default: blockInfo.type = "unknown"; break;
    }
    m_blockInfos.append(blockInfo);
#endif

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

        // 绘制代码块文本
        QFont monoFont("Consolas", 9);
        monoFont.setStyleHint(QFont::Monospace);
        p->setFont(monoFont);
        p->setPen(m_theme.previewCodeFg);

        // [高 DPI 修复] 必须使用 p->device() 参数获取正确的字体度量
        // 原因：p->device() 返回 painter 所绘制设备的 DPI 信息
        // - 在高 DPI 屏上，QFontMetricsF(font, device) 返回物理像素度量
        // - 不带 device 的 QFontMetricsF(font) 只返回逻辑像素度量
        // - QPainter 在高 DPI 下自动缩放坐标，所以必须使用物理像素度量
        // - 这样布局阶段的 lineH 计算才能与渲染阶段一致
        QFontMetricsF fm(monoFont, p->device());
        qreal textHeight = fm.ascent() + fm.descent();  // 文本实际高度（不含行间距）
        qreal lineH = fm.height() * 1.4;  // 行高（包括间距）-- 必须与 PreviewLayout 的计算一致
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

        // Children - 传入正确的子块绝对坐标（包括 X 偏移）
        for (const auto& child : block.children) {
            qreal childAbsX = absX + child.bounds.x();
            qreal childAbsY = absY + child.bounds.y();
            paintBlock(p, child, childAbsX, childAbsY, scrollY, viewportHeight, viewportWidth);
        }
        break;
    }
    case LayoutBlock::List: {
        int itemIndex = 0;
        for (const auto& child : block.children) {
            qreal itemAbsX = absX + child.bounds.x();  // [坐标系统统一] 列表项 X 偏移（缩进）
            qreal itemAbsY = absY + child.bounds.y();
            qreal itemDrawY = itemAbsY - scrollY;
            qreal bulletX = itemAbsX;  // 序号与内容使用同一 X 坐标系统

            QFont baseFont("Segoe UI", 10);
            p->setFont(baseFont);
            p->setPen(m_theme.previewFg);
            QFontMetricsF fm(baseFont, p->device());

            if (block.ordered) {
                QString num = QString::number(block.listStart + itemIndex) + ".";
                p->drawText(QPointF(bulletX, itemDrawY + fm.ascent()), num);
            } else {
                p->drawText(QPointF(bulletX + 4, itemDrawY + fm.ascent()),
                            QStringLiteral("\u2022"));
            }

            paintBlock(p, child, itemAbsX, itemAbsY, scrollY, viewportHeight, viewportWidth);
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

            // Cells - 传入正确的单元格绝对坐标
            for (const auto& cell : row.children) {
                qreal cellAbsX = absX + cell.bounds.x();
                qreal cellAbsY = rowAbsY;  // 单元格使用行的绝对 y
                qreal cellX = cellAbsX;
                qreal cellY = cellAbsY - scrollY;

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
    QFontMetricsF defaultFm(block.inlineRuns[0].font, p->device());  // 使用 device 参数
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

#ifdef ENABLE_TEST_MODE
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDateTime>

void PreviewPainter::saveBlocksToJson(int viewportWidth, int viewportHeight) const
{
    // [测试模式调试] 输出调试信息到临时文件
    QString debugPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/render_blocks_debug.txt";
    QFile debugFile(debugPath);
    if (debugFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        QTextStream stream(&debugFile);
        stream << QString("saveBlocksToJson called: blocks=%1, viewport=%2x%3\n")
                  .arg(m_blockInfos.size()).arg(viewportWidth).arg(viewportHeight);
        debugFile.close();
    }

    if (m_blockInfos.isEmpty()) {
        return;  // 没有块信息，不需要保存
    }

    QJsonArray blocksArray;
    for (const auto& blockInfo : m_blockInfos) {
        QJsonObject blockObj;
        blockObj["type"] = blockInfo.type;
        blockObj["x"] = blockInfo.x;
        blockObj["y"] = blockInfo.y;
        blockObj["width"] = blockInfo.width;
        blockObj["height"] = blockInfo.height;
        if (blockInfo.headingLevel > 0) {
            blockObj["heading_level"] = blockInfo.headingLevel;
        }
        if (blockInfo.listLevel > 0) {
            blockObj["list_level"] = blockInfo.listLevel;
        }
        blocksArray.append(blockObj);
    }

    QJsonObject rootObj;
    rootObj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    rootObj["viewport_width"] = viewportWidth;
    rootObj["viewport_height"] = viewportHeight;
    rootObj["blocks"] = blocksArray;

    QJsonDocument doc(rootObj);

    // 保存到应用临时目录下
    QString outputPath;
#ifdef Q_OS_WIN
    outputPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/render_blocks.json";
#else
    outputPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/render_blocks.json";
#endif

    QFile file(outputPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(doc.toJson());
        file.close();
    }
}
#endif

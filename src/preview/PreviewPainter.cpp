#include "PreviewPainter.h"
#include "ImageCache.h"
#include "CodeBlockRenderer.h"

#include <QCoreApplication>
#include <QFileInfo>
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

void PreviewPainter::setImageCache(ImageCache* cache)
{
    m_imageCache = cache;
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

void PreviewPainter::setTargetLineHighlight(int sourceLine, qreal opacity)
{
    m_targetSourceLine = sourceLine;
    m_targetHighlightOpacity = opacity;
}

void PreviewPainter::recordSegment(const QRectF& rect, int charStart, int charLen,
                                    const QString& text, const QFont& font,
                                    const QString& linkUrl)
{
    m_textSegments.append({rect, charStart, charLen, text, font, linkUrl});
}

void PreviewPainter::paint(QPainter* painter, const LayoutBlock& root,
                            qreal scrollY, qreal viewportHeight, qreal viewportWidth)
{
    if (!painter) return;

    m_textSegments.clear();
    m_charCounter = 0;

#ifdef ENABLE_TEST_MODE
    // [测试模式] 从 LayoutBlock 树构建完整的 BlockInfo 渲染树
    m_rootBlockInfo = BlockInfo();
    m_rootBlockInfo.type = "document";
    m_rootBlockInfo.x = 0;
    m_rootBlockInfo.y = 0;
    m_rootBlockInfo.width = (int)viewportWidth;
    m_rootBlockInfo.height = (int)root.bounds.height();
    for (const auto& child : root.children) {
        m_rootBlockInfo.children.append(buildBlockInfo(child, 0, -scrollY));
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

    // (BlockInfo 现在在 paint() 中通过 buildBlockInfo 递归构建，不再在此处收集)

    switch (block.type) {
    case LayoutBlock::Paragraph: {
        paintInlineRuns(p, block, drawX, drawY, block.bounds.width());
        break;
    }
    case LayoutBlock::Heading: {
        // TOC 跳转目标行高亮
        if (m_targetSourceLine >= 0 && m_targetHighlightOpacity > 0.0 &&
            block.sourceStartLine <= m_targetSourceLine && block.sourceEndLine >= m_targetSourceLine) {
            QColor highlightColor = m_theme.previewLink;
            highlightColor.setAlphaF(m_targetHighlightOpacity * 0.2);
            QRectF highlightRect(drawX, drawY, block.bounds.width(), block.bounds.height());
            p->fillRect(highlightRect, highlightColor);
        }

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

        // 绘制代码块文本 [Spec 模块-preview/03 INV-8: 字体来自 layout]
        QFont monoFont = m_layout ? m_layout->monoFont() : QFont("Consolas", 9);
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

        // 语法高亮渲染
        CodeBlockRenderer renderer;
        auto hlLines = renderer.highlight(block.codeText, block.language, m_theme.isDark);
        const QStringList rawLines = block.codeText.split('\n');

        for (int li = 0; li < (int)hlLines.size(); ++li) {
            // 跳过 split 产生的尾部空元素，与 extractBlockText 保持一致
            const QString& line = (li < rawLines.size()) ? rawLines[li] : QString();
            if (li == (int)hlLines.size() - 1 && line.isEmpty())
                break;

            qreal w = fm.horizontalAdvance(line);
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

            // 选区高亮
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

            // 按语法高亮段绘制文本
            qreal segX = textX;
            for (const auto& seg : hlLines[li]) {
                p->setPen(seg.color);
                if (seg.bold) {
                    QFont boldFont = monoFont;
                    boldFont.setBold(true);
                    p->setFont(boldFont);
                    p->drawText(QPointF(segX, textY + fm.ascent()), seg.text);
                    p->setFont(monoFont);
                } else {
                    p->drawText(QPointF(segX, textY + fm.ascent()), seg.text);
                }
                segX += fm.horizontalAdvance(seg.text);
            }

            m_charCounter += line.length() + 1;
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

        // Children - 传入父块绝对坐标，子块通过自身 bounds 定位
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
            // 序号/圆点画在 List 左边缘（absX），内容在 absX + child.bounds.x()
            qreal bulletX = absX;

            // [Spec 模块-preview/03 INV-8: 列表序号字体从 layout 取]
            QFont baseFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
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

            // 传入父块绝对坐标，子块通过自身 bounds 定位（避免双倍偏移）
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

                // Cell text [Spec 模块-preview/03 INV-8: 表格单元格字体从 layout 取]
                QFont cellFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
                if (isFirstRow) cellFont.setWeight(QFont::Bold);
                p->setFont(cellFont);
                p->setPen(m_theme.previewFg);

                if (!cell.inlineRuns.empty()) {
                    paintInlineRuns(p, cell, cellX + 4, cellY + 4, cell.bounds.width() - 8);
                    m_charCounter++;  // 每个 cell 结尾的 '\n'，与 extractBlockText 一致
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
        QRectF rect(drawX, drawY, block.bounds.width(), block.bounds.height());
        const QString& url = block.imageUrl;

        // Extract basename from URL
        QString basename;
        if (!url.isEmpty()) {
            int lastSlash = url.lastIndexOf('/');
            int lastBackslash = url.lastIndexOf('\\');
            int pos = qMax(lastSlash, lastBackslash);
            basename = (pos >= 0) ? url.mid(pos + 1) : url;
        }

        // Check image cache state
        QPixmap* pixmap = m_imageCache ? m_imageCache->get(url) : nullptr;
        bool isFailed = m_imageCache && !url.isEmpty() && m_imageCache->isFailed(url);
        bool isNetwork = m_imageCache && m_imageCache->isNetworkUrl(url);

        if (isFailed || (isNetwork && !pixmap)) {
            // Error / unsupported state
            p->fillRect(rect, m_theme.previewImageErrorBg);
            p->setPen(QPen(m_theme.previewImageErrorBorder, 1.5));
            p->drawRect(rect);

            qreal cx = rect.center().x();
            qreal cy = rect.center().y();

            // Error icon: X mark [Spec INV-8: 从 layout base 派生, 比例 1.33]
            QFont iconFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
            iconFont.setPointSizeF(iconFont.pointSizeF() * 1.33);
            iconFont.setBold(true);
            p->setFont(iconFont);
            p->setPen(m_theme.previewImageErrorText);
            QFontMetricsF iconFm(iconFont, p->device());
            qreal iconW = iconFm.horizontalAdvance(QStringLiteral("\u2717"));
            qreal iconH = iconFm.height();
            p->drawText(QPointF(cx - iconW / 2, cy - 4), QStringLiteral("\u2717"));

            // Error label [Spec INV-8: 比例 0.83]
            QFont labelFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
            labelFont.setPointSizeF(labelFont.pointSizeF() * 0.83);
            p->setFont(labelFont);
            p->setPen(m_theme.previewImageErrorText);
            // 使用 QCoreApplication::translate 绑定 context 名称，符合 Spec INV-2
            QString errorLabel = isNetwork
                ? QCoreApplication::translate("PreviewPainter", "Network images not supported")
                : QCoreApplication::translate("PreviewPainter", "Failed to load image");
            p->drawText(QRectF(rect.x(), cy + iconH * 0.3, rect.width(), iconH),
                        Qt::AlignHCenter | Qt::AlignTop, errorLabel);

            // File name at bottom [Spec INV-8: 比例 0.75]
            if (!basename.isEmpty()) {
                QFont nameFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
                nameFont.setPointSizeF(nameFont.pointSizeF() * 0.75);
                p->setFont(nameFont);
                p->setPen(m_theme.previewImageInfoText);
                QFontMetricsF nameFm(nameFont, p->device());
                QString elidedName = nameFm.elidedText(basename, Qt::ElideMiddle, rect.width() - 16);
                p->drawText(QRectF(rect.x(), rect.bottom() - nameFm.height() - 6,
                                   rect.width(), nameFm.height()),
                            Qt::AlignHCenter | Qt::AlignTop, elidedName);
            }
        } else if (pixmap) {
            // Image loaded successfully - show placeholder with size info
            p->fillRect(rect, m_theme.previewImagePlaceholderBg);
            p->setPen(QPen(m_theme.previewImagePlaceholderBorder, 1));
            p->drawRect(rect);

            qreal cx = rect.center().x();
            qreal cy = rect.center().y();

            // Image icon [Spec INV-8: 比例 1.66]
            QFont iconFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
            iconFont.setPointSizeF(iconFont.pointSizeF() * 1.66);
            p->setFont(iconFont);
            p->setPen(m_theme.previewImagePlaceholderText);
            QFontMetricsF iconFm(iconFont, p->device());
            qreal iconW = iconFm.horizontalAdvance(QStringLiteral("\u25A3"));
            p->drawText(QPointF(cx - iconW / 2, cy - 6), QStringLiteral("\u25A3"));

            // File name [Spec INV-8: 比例 0.83]
            QFont nameFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
            nameFont.setPointSizeF(nameFont.pointSizeF() * 0.83);
            nameFont.setBold(true);
            p->setFont(nameFont);
            p->setPen(m_theme.previewFg);
            QFontMetricsF nameFm(nameFont, p->device());
            QString displayName = basename.isEmpty()
                ? QCoreApplication::translate("PreviewPainter", "Image")
                : basename;
            QString elidedName = nameFm.elidedText(displayName, Qt::ElideMiddle, rect.width() - 16);
            qreal nameY = cy + iconFm.height() * 0.3;
            p->drawText(QRectF(rect.x(), nameY, rect.width(), nameFm.height()),
                        Qt::AlignHCenter | Qt::AlignTop, elidedName);

            // Image dimensions [Spec INV-8: 比例 0.75]
            QFont dimFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
            dimFont.setPointSizeF(dimFont.pointSizeF() * 0.75);
            p->setFont(dimFont);
            p->setPen(m_theme.previewImageInfoText);
            QString dimText = QString("%1 \u00D7 %2 px")
                                  .arg(pixmap->width())
                                  .arg(pixmap->height());
            p->drawText(QRectF(rect.x(), nameY + nameFm.height() + 2,
                               rect.width(), nameFm.height()),
                        Qt::AlignHCenter | Qt::AlignTop, dimText);
        } else {
            // Not yet loaded / URL empty - neutral placeholder
            p->fillRect(rect, m_theme.previewImagePlaceholderBg);
            p->setPen(QPen(m_theme.previewImagePlaceholderBorder, 1));
            p->drawRect(rect);

            qreal cx = rect.center().x();
            qreal cy = rect.center().y();

            // Image icon [Spec INV-8: 比例 1.66]
            QFont iconFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
            iconFont.setPointSizeF(iconFont.pointSizeF() * 1.66);
            p->setFont(iconFont);
            p->setPen(m_theme.previewImagePlaceholderText);
            QFontMetricsF iconFm(iconFont, p->device());
            qreal iconW = iconFm.horizontalAdvance(QStringLiteral("\u25A3"));
            p->drawText(QPointF(cx - iconW / 2, cy - 6), QStringLiteral("\u25A3"));

            // File name or "Image" [Spec INV-8: 比例 0.83]
            QFont nameFont = m_layout ? m_layout->baseFont() : QFont("Segoe UI", 12);
            nameFont.setPointSizeF(nameFont.pointSizeF() * 0.83);
            p->setFont(nameFont);
            p->setPen(m_theme.previewImagePlaceholderText);
            QFontMetricsF nameFm(nameFont, p->device());
            QString displayName = basename.isEmpty()
                ? QCoreApplication::translate("PreviewPainter", "Image")
                : basename;
            QString elidedName = nameFm.elidedText(displayName, Qt::ElideMiddle, rect.width() - 16);
            p->drawText(QRectF(rect.x(), cy + iconFm.height() * 0.3,
                               rect.width(), nameFm.height()),
                        Qt::AlignHCenter | Qt::AlignTop, elidedName);
        }
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
    // [Spec 模块-preview/03 INV-10] 行内所有 run 共享统一基线，防止小字号 run
    // （如 inline code，字号 = 正文 * 0.9）按自身 ascent 定位导致基线上移、视觉偏上
    qreal lineAscent = defaultFm.ascent();

    QColor selColor(0, 120, 215, 80);

    auto drawSelectionHighlight = [&](const QString& segText, qreal sx, qreal sy, qreal sw, qreal sh, int charStart, int charLen, const QString& linkUrl = QString()) {
        QFont curFont = p->font();
        recordSegment(QRectF(sx, sy, sw, sh), charStart, charLen, segText, curFont, linkUrl);

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
            // [Spec 模块-preview/03 INV-10] 所有 run 共享 lineAscent（行主字体基线），
            // 不能用 run 自身 fm.ascent()——否则 inline code 这种小字号 run 会上浮
            // 文本范围 Y：从 curY 到 curY + fm.height()
            // 背景、选区高亮必须使用一致的高度，避免 DPI 切换时产生空白
            qreal textHeight = fm.height();  // 文本实际高度

            if (hasBg) {
                p->fillRect(QRectF(curX, curY, segW, textHeight), run.bgColor);
            }
            drawSelectionHighlight(seg, curX, curY, segW, textHeight,
                                   m_charCounter + segOffset, seg.length(), run.linkUrl);
            p->drawText(QPointF(curX, curY + lineAscent), seg);
            if (!run.linkUrl.isEmpty())
                p->drawLine(QPointF(curX, curY + lineAscent + 2), QPointF(curX + segW, curY + lineAscent + 2));
            if (run.isStrikethrough)
                p->drawLine(QPointF(curX, curY + lineAscent / 2), QPointF(curX + segW, curY + lineAscent / 2));
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

// 从 LayoutBlock 递归构建 BlockInfo 树
BlockInfo PreviewPainter::buildBlockInfo(const LayoutBlock& block, qreal offsetX, qreal offsetY)
{
    BlockInfo info;

    // 坐标（视口坐标）
    qreal drawX = offsetX + block.bounds.x();
    qreal drawY = offsetY + block.bounds.y();
    info.x = (int)drawX;
    info.y = (int)drawY;
    info.width = (int)block.bounds.width();
    info.height = (int)block.bounds.height();

    // 类型
    switch (block.type) {
    case LayoutBlock::Paragraph:    info.type = "paragraph"; break;
    case LayoutBlock::Heading:      info.type = "heading"; break;
    case LayoutBlock::CodeBlock:    info.type = "code_block"; break;
    case LayoutBlock::HtmlBlock:    info.type = "html_block"; break;
    case LayoutBlock::BlockQuote:   info.type = "blockquote"; break;
    case LayoutBlock::List:         info.type = "list"; break;
    case LayoutBlock::ListItem:     info.type = "list_item"; break;
    case LayoutBlock::Table:        info.type = "table"; break;
    case LayoutBlock::TableRow:     info.type = "table_row"; break;
    case LayoutBlock::TableCell:    info.type = "table_cell"; break;
    case LayoutBlock::Image:        info.type = "image"; break;
    case LayoutBlock::ThematicBreak: info.type = "thematic_break"; break;
    default:                        info.type = "unknown"; break;
    }

    // 源码行号
    info.sourceStart = block.sourceStartLine;
    info.sourceEnd = block.sourceEndLine;

    // 标题
    info.headingLevel = block.headingLevel;

    // 列表属性
    info.ordered = block.ordered;
    info.listStart = block.listStart;

    // 代码块语言
    info.codeLanguage = block.language;

    // 文本内容：从 inlineRuns 拼接，或取 codeText
    if (!block.inlineRuns.empty()) {
        QString text;
        for (const auto& run : block.inlineRuns) {
            text += run.text;
        }
        info.contentText = text.left(200);

        // 主字体信息（取第一个非换行 run）
        for (const auto& run : block.inlineRuns) {
            if (run.text != "\n") {
                info.fontFamily = run.font.family();
                info.fontSize = run.font.pointSizeF();
                info.fontWeight = run.font.weight();
                break;
            }
        }

        // 行内元素详情
        for (const auto& run : block.inlineRuns) {
            if (run.text == "\n") continue;
            InlineRunInfo ri;
            ri.text = run.text;
            ri.fontFamily = run.font.family();
            ri.fontSize = run.font.pointSizeF();
            ri.fontWeight = run.font.weight();
            ri.color = run.color.name();
            if (run.bgColor.isValid() && run.bgColor != Qt::transparent) {
                ri.bgColor = run.bgColor.name();
            }
            ri.isLink = !run.linkUrl.isEmpty();
            ri.isStrikethrough = run.isStrikethrough;
            info.inlineRuns.append(ri);
        }
    } else if (!block.codeText.isEmpty()) {
        info.contentText = block.codeText.left(200);
    }

    // 递归子块 —— 所有块类型统一：传入父块绝对坐标，子块通过自身 bounds 定位
    // 这与修复后的 paintBlock 行为一致（不预加 child 偏移，避免双倍偏移）
    if (block.type == LayoutBlock::Table) {
        // Table 在 paintBlock 中内联处理 row/cell，不递归调用 paintBlock
        // 这里直接构造 row/cell 的 BlockInfo，用 paintBlock 相同的坐标计算
        for (const auto& row : block.children) {
            BlockInfo rowInfo;
            rowInfo.type = "table_row";
            // paintBlock 中: rowAbsY = absY + row.bounds.y()
            // 这里 drawY 就是 table 的视口 Y（= absY - scrollY），row.bounds.y() 是行相对偏移
            rowInfo.x = (int)drawX;
            rowInfo.y = (int)(drawY + row.bounds.y());
            rowInfo.width = info.width;
            rowInfo.height = (int)row.bounds.height();

            for (const auto& cell : row.children) {
                BlockInfo cellInfo;
                cellInfo.type = "table_cell";
                cellInfo.x = (int)(drawX + cell.bounds.x());  // absX + cell 相对偏移
                cellInfo.y = rowInfo.y;  // 单元格 Y = 行 Y
                cellInfo.width = (int)cell.bounds.width();
                cellInfo.height = (int)row.bounds.height();
                cellInfo.sourceStart = cell.sourceStartLine;
                cellInfo.sourceEnd = cell.sourceEndLine;
                // 单元格的行内元素
                if (!cell.inlineRuns.empty()) {
                    QString text;
                    for (const auto& run : cell.inlineRuns) text += run.text;
                    cellInfo.contentText = text.left(200);
                    for (const auto& run : cell.inlineRuns) {
                        if (run.text == "\n") continue;
                        cellInfo.fontFamily = run.font.family();
                        cellInfo.fontSize = run.font.pointSizeF();
                        cellInfo.fontWeight = run.font.weight();
                        break;
                    }
                }
                rowInfo.children.append(cellInfo);
            }
            info.children.append(rowInfo);
        }
    } else if (block.type == LayoutBlock::List) {
        // List: 为每个 ListItem 记录 bullet/number 的绘制坐标
        // paintBlock 中: bulletX = absX (List 左边缘), bulletY = absY + child.bounds.y()
        int itemIdx = 0;
        for (const auto& child : block.children) {
            BlockInfo childInfo = buildBlockInfo(child, drawX, drawY);
            // bullet 坐标（与 paintBlock 中 List 分支保持一致）
            childInfo.bulletX = (int)drawX;  // List 左边缘
            childInfo.bulletY = childInfo.y;  // ListItem 的 Y 坐标
            // bullet 宽度估算
            if (block.ordered) {
                QString num = QString::number(block.listStart + itemIdx) + ".";
                childInfo.bulletWidth = num.length() * 8;  // 粗略估算
            } else {
                childInfo.bulletWidth = 12;  // "•" 宽度
            }
            info.children.append(childInfo);
            itemIdx++;
        }
    } else {
        // 所有其他块类型：统一传入父块绝对坐标
        for (const auto& child : block.children) {
            info.children.append(buildBlockInfo(child, drawX, drawY));
        }
    }

    return info;
}

// BlockInfo → QJsonObject（递归）
QJsonObject PreviewPainter::blockInfoToJson(const BlockInfo& info)
{
    QJsonObject obj;
    obj["type"] = info.type;
    obj["x"] = info.x;
    obj["y"] = info.y;
    obj["width"] = info.width;
    obj["height"] = info.height;

    if (!info.contentText.isEmpty())
        obj["content"] = info.contentText;
    if (info.sourceStart >= 0)
        obj["source_start"] = info.sourceStart;
    if (info.sourceEnd >= 0)
        obj["source_end"] = info.sourceEnd;
    if (info.headingLevel > 0)
        obj["heading_level"] = info.headingLevel;
    if (!info.fontFamily.isEmpty()) {
        obj["font_family"] = info.fontFamily;
        obj["font_size"] = info.fontSize;
        obj["font_weight"] = info.fontWeight;
    }
    if (info.type == "list") {
        obj["ordered"] = info.ordered;
        obj["list_start"] = info.listStart;
    }
    if (!info.codeLanguage.isEmpty())
        obj["code_language"] = info.codeLanguage;
    if (info.bulletX >= 0) {
        obj["bullet_x"] = info.bulletX;
        obj["bullet_y"] = info.bulletY;
        obj["bullet_width"] = info.bulletWidth;
    }

    // 行内元素
    if (!info.inlineRuns.isEmpty()) {
        QJsonArray runsArray;
        for (const auto& ri : info.inlineRuns) {
            QJsonObject ro;
            ro["text"] = ri.text;
            ro["font_size"] = ri.fontSize;
            ro["font_weight"] = ri.fontWeight;
            if (!ri.color.isEmpty()) ro["color"] = ri.color;
            if (!ri.bgColor.isEmpty()) ro["bg_color"] = ri.bgColor;
            if (ri.isLink) ro["is_link"] = true;
            if (ri.isStrikethrough) ro["is_strikethrough"] = true;
            runsArray.append(ro);
        }
        obj["inline_runs"] = runsArray;
    }

    // 递归子块
    if (!info.children.isEmpty()) {
        QJsonArray childrenArray;
        for (const auto& child : info.children) {
            childrenArray.append(blockInfoToJson(child));
        }
        obj["children"] = childrenArray;
    }

    return obj;
}

void PreviewPainter::saveBlocksToJson(int viewportWidth, int viewportHeight) const
{
    if (m_rootBlockInfo.children.isEmpty()) return;

    QJsonObject rootObj;
    rootObj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    rootObj["viewport_width"] = viewportWidth;
    rootObj["viewport_height"] = viewportHeight;
    rootObj["blocks"] = blockInfoToJson(m_rootBlockInfo)["children"].toArray();

    QString outputPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/render_blocks.json";
    QFile file(outputPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(rootObj).toJson());
        file.close();
    }
}
#endif

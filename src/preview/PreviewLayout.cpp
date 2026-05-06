#include "PreviewLayout.h"
#include "ImageCache.h"
#include "MarkdownAst.h"
#include "FontDefaults.h"
#include "PerfProbe.h"

#include <QByteArray>
#include <QFontMetricsF>
#include <QtMath>
#include <algorithm>

PreviewLayout::PreviewLayout()
{
    // [字体系统] Spec: specs/横切关注点/80-字体系统.md INV-1, INV-2, INV-3
    // 默认字体统一从 FontDefaults 取，保证与编辑器基础字号相等
    m_baseFont = font_defaults::defaultPreviewFont();
    m_monoFont = font_defaults::defaultMonoFont();

    // [高 DPI 修复] 不在构造函数中计算行高
    // 原因：构造函数中没有 device 参数，只能得到逻辑像素
    // 当 updateMetrics(device) 被调用时，需要根据实际 DPI 重新计算
    // 否则 DPI 改变时，初始值与 updateMetrics 值会使用不同基准，导致高度不一致
    //
    // 使用默认值，会在 updateMetrics 中被正确初始化
    m_lineHeight = 24.0;    // 临时值，会被 updateMetrics 覆盖
    m_codeLineHeight = 20.0; // 临时值，会被 updateMetrics 覆盖
}

PreviewLayout::~PreviewLayout() = default;

// 性能优化方案 B：缓存 QFontMetricsF，避免每个 InlineRun 重复构造
//
// [Spec 模块-preview/02 INV-12] 字体度量缓存键必须包含所有影响度量的属性
// ----------------------------------------------------------------------
// 历史 bug：原先用 qHash(QFont) 作 key，Qt 5.12 的 qHash 实现对
// "同 family、同 weight、同 italic、不同 pointSize" 的字体会返回同一 hash
// （例如 SimSun bold size=16.2 与 SimSun bold size=9 在我机器上都得到
// key=2476512596）。当 H1（1.8x base）先于列表项加粗段落进入缓存时，
// 列表项 bold size=9 的查询命中 H1 的缓存项，glyphHeight 被读成 27 而非 15，
// estimateParagraphHeight 把单行段落估为多行，ListItem.height 比实际墨迹
// 高出 0.5~1 整行，相邻列表项之间出现一行多余空白。
// 修复：用 (family, pointSizeF, weight, italic, stretch, styleStrategy)
// 元组拼成的字符串作 key，确保唯一性。Qt 5.12 没有给 QString 提供
// std::hash 特化，所以 unordered_map 用 std::string 做 key。
const QFontMetricsF& PreviewLayout::cachedFontMetrics(const QFont& font) const
{
    char buf[256];
    QByteArray familyUtf8 = font.family().toUtf8();
    qsnprintf(buf, sizeof(buf), "%.*s|%.6g|%d|%d|%d|%d",
              static_cast<int>(familyUtf8.size()), familyUtf8.constData(),
              font.pointSizeF(),
              font.weight(),
              font.italic() ? 1 : 0,
              font.stretch(),
              static_cast<int>(font.styleStrategy()));
    std::string key(buf);
    auto it = m_fontMetricsCache.find(key);
    if (it != m_fontMetricsCache.end())
        return it->second;
    auto [inserted, _] = m_fontMetricsCache.emplace(std::move(key), QFontMetricsF(font, m_device));
    return inserted->second;
}

void PreviewLayout::clearFontMetricsCache()
{
    m_fontMetricsCache.clear();
}

void PreviewLayout::setViewportWidth(qreal width)
{
    m_viewportWidth = width;
}

void PreviewLayout::setImageCache(ImageCache* cache)
{
    m_imageCache = cache;
}

void PreviewLayout::setFont(const QFont& baseFont)
{
    // [Spec 模块-preview/02 INV-8 + 横切 80 INV-9]
    // setFont 必须同步更新 m_monoFont，使其相对 m_baseFont 保持 kMonoDelta
    m_baseFont = baseFont;
    int delta = baseFont.pointSize() - font_defaults::kDefaultBaseFontSizePt;
    m_monoFont = font_defaults::defaultMonoFont(delta);
    QFontMetricsF fm(m_baseFont);
    // [Spec 模块-preview/02 INV-13] 行高乘数从 m_lineSpacingFactor 读取，
    // 而非字面量 1.5。否则用户在"视图 → 行间距"切换 1.0/2.0 后，
    // 一旦后续 setFont 被触发（例如缩放、字体切换）会把乘数重置回历史默认。
    m_lineHeight = fm.height() * m_lineSpacingFactor;
}

void PreviewLayout::setLineSpacingFactor(qreal factor)
{
    // [Spec 模块-preview/02 INV-13] 仅作用于正文段落行高
    if (qFuzzyCompare(m_lineSpacingFactor, factor))
        return;
    m_lineSpacingFactor = factor;
    if (m_device) {
        QFontMetricsF fm(m_baseFont, m_device);
        m_lineHeight = fm.height() * m_lineSpacingFactor;
        // 缓存键不依赖乘数，但调用方紧接着会 rebuildLayout，
        // estimateParagraphHeight 会用新乘数重新计算所有段落高度，
        // 此处清缓存让语义更干净（且代价仅一次性，rebuildLayout 后会自然回填）
        clearFontMetricsCache();
    }
    // 若 m_device 尚未绑定（widget 未首次绘制），保留 m_lineHeight 临时值，
    // 下一次 updateMetrics(device) 会自动用新乘数重算
}

bool PreviewLayout::updateMetrics(QPaintDevice* device)
{
    // 基于 device 的实际字体度量计算行高
    // 返回 true 表示度量值发生了变化，需要重建布局
    QFontMetricsF fm(m_baseFont, device);
    // [Spec 模块-preview/02 INV-13] 正文行高乘数 = m_lineSpacingFactor（用户可调）
    // 代码行高 1.4 与 lineSpacingFactor 无关，是代码块密度的独立基线
    qreal newLineHeight = fm.height() * m_lineSpacingFactor;
    QFontMetricsF fmCode(m_monoFont, device);
    qreal newCodeLineHeight = fmCode.height() * 1.4;

    bool changed = !qFuzzyCompare(newLineHeight, m_lineHeight)
                || !qFuzzyCompare(newCodeLineHeight, m_codeLineHeight);

    bool deviceChanged = (m_device != device);
    m_device = device;
    m_lineHeight = newLineHeight;
    m_codeLineHeight = newCodeLineHeight;

    if (changed || deviceChanged)
        clearFontMetricsCache();

    return changed;
}

void PreviewLayout::setTheme(const Theme& theme)
{
    m_theme = theme;
}

void PreviewLayout::buildFromAst(const std::shared_ptr<AstNode>& root)
{
    SM_PERF_SCOPE("preview.buildFromAst");
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
        // 检测段落是否只包含一个 Image 子节点（cmark 把图片放在段落里）
        // 如果是，提升为块级图片渲染
        if (node->children.size() == 1 && node->children[0]->type == AstNodeType::Image) {
            const auto& imgNode = node->children[0];
            block.type = LayoutBlock::Image;
            block.imageUrl = imgNode->url;
            qreal imgHeight = 200.0;
            if (m_imageCache) {
                QPixmap* pix = m_imageCache->get(block.imageUrl);
                if (pix && !pix->isNull() && pix->width() > 0) {
                    qreal scale = qMin(1.0, maxWidth / static_cast<qreal>(pix->width()));
                    imgHeight = pix->height() * scale;
                }
            }
            block.bounds = QRectF(0, 0, maxWidth, imgHeight);
            break;
        }
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
        case 6: scale = 1.02; break;
        default: scale = 1.0; break;
        }
        headingFont.setPointSizeF(m_baseFont.pointSizeF() * scale);
        headingFont.setWeight(QFont::Bold);

        collectInlineRuns(node, block.inlineRuns, headingFont, m_theme.previewHeading);
        qreal h = estimateParagraphHeight(block.inlineRuns, maxWidth);
        // H1/H2 额外底部边距
        if (node->headingLevel <= 2) h += 8;
        block.bounds = QRectF(0, 0, maxWidth, h);
        break;
    }
    case AstNodeType::CodeBlock: {
        // [Spec 模块-preview/02 INV-14] CodeBlock 高度必须按软换行后的视觉行数计算，
        // 与 PreviewPainter::paintCodeBlock 同构。否则超长代码行从右边界飞出，
        // 后续 block 也会压在被裁切的代码内容上。
        block.type = LayoutBlock::CodeBlock;
        block.codeText = node->literal;
        block.language = node->fenceInfo;

        const qreal hPad = 8.0;  // 与 painter textX = drawX + 8 同构
        const qreal contentWidth = qMax<qreal>(1.0, maxWidth - 2 * hPad);
        const QFontMetricsF& fmCode = cachedFontMetrics(m_monoFont);

        const QStringList rawLines = block.codeText.split('\n');
        const int rawCount = rawLines.size();
        int visualLineCount = 0;
        for (int li = 0; li < rawCount; ++li) {
            // 末尾 \n 产生的空 trailing 行不计（与 painter 跳过逻辑同构）
            if (li == rawCount - 1 && rawLines[li].isEmpty() && block.codeText.endsWith('\n'))
                break;
            const QString& line = rawLines[li];
            if (line.isEmpty()) {
                visualLineCount += 1;
                continue;
            }
            qreal fullW = fmCode.horizontalAdvance(line);
            if (fullW <= contentWidth) {
                visualLineCount += 1;
                continue;
            }
            // 字符级 wrap：与 painter 的 fit 算法同构（INV-14 / 03 INV-13）
            int physOffset = 0;
            while (physOffset < line.length()) {
                int fit = 0;
                qreal acc = 0;
                for (int i = physOffset; i < line.length(); ++i) {
                    qreal cw = fmCode.horizontalAdvance(QString(line[i]));
                    if (acc + cw > contentWidth && fit > 0) break;
                    acc += cw;
                    fit = i + 1 - physOffset;
                }
                if (fit <= 0) fit = 1;  // 极窄视口下保底，防死循环
                physOffset += fit;
                visualLineCount += 1;
            }
        }
        if (visualLineCount == 0) visualLineCount = 1;  // 空代码块仍占一行
        qreal h = visualLineCount * m_codeLineHeight + 2 * hPad;
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
        bool isFirst = true;
        for (const auto& child : node->children) {
            LayoutBlock childBlock = layoutBlock(child.get(), maxWidth);
            childBlock.bounds.moveTop(y);
            // [列表对齐修复] 第一个子块与 bullet point 同基线，不添加上方间距
            // 只有后续子块之间需要间距
            qreal spacing = isFirst ? 0.0 : 4.0;
            y += childBlock.bounds.height() + spacing;
            block.children.push_back(std::move(childBlock));
            isFirst = false;
        }
        // [Spec INV-11] 不要用 m_lineHeight (= baseFm.height()*1.5) 做下界保底，
        // 否则会把单行段落 (height ≈ baseFm.height()) 抬高到 1.5 倍墨迹高，
        // 在连续列表项之间留出半行空白。空 item 才用墨迹高度保底。
        QFontMetricsF baseFm(m_baseFont, m_device);
        qreal itemMinHeight = baseFm.height();
        block.bounds = QRectF(0, 0, maxWidth, qMax(y, itemMinHeight));
        break;
    }
    case AstNodeType::Table: {
        block.type = LayoutBlock::Table;
        // 从第一行计算列数
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

            // 第一遍：收集行内 run 并计算行高
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

            // 第二遍：使用计算出的行高分配边界
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
        // 查询缓存获取实际图片尺寸，按 maxWidth 等比缩放
        qreal imgHeight = 200.0; // 默认高度（未加载时）
        if (m_imageCache) {
            QPixmap* pix = m_imageCache->get(block.imageUrl);
            if (pix && !pix->isNull() && pix->width() > 0) {
                qreal scale = qMin(1.0, maxWidth / static_cast<qreal>(pix->width()));
                imgHeight = pix->height() * scale;
            }
        }
        block.bounds = QRectF(0, 0, maxWidth, imgHeight);
        break;
    }
    case AstNodeType::ThematicBreak: {
        block.type = LayoutBlock::ThematicBreak;
        block.bounds = QRectF(0, 0, maxWidth, 33.0);
        break;
    }
    case AstNodeType::Frontmatter: {
        // Spec: specs/模块-preview/10-Frontmatter渲染.md §4.3 §4.5 §5.3
        block = layoutFrontmatter(node, maxWidth);
        block.sourceStartLine = node->startLine;
        block.sourceEndLine = node->endLine;
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
        // 未知块类型，尝试布局子节点
        block.type = LayoutBlock::Paragraph;
        collectInlineRuns(node, block.inlineRuns, m_baseFont, m_theme.previewFg);
        qreal h = estimateParagraphHeight(block.inlineRuns, maxWidth);
        block.bounds = QRectF(0, 0, maxWidth, qMax(h, m_lineHeight));
        break;
    }
    }

    return block;
}

// Spec: specs/模块-preview/10-Frontmatter渲染.md §5.3
// Invariants: INV-9 (monoFont), INV-10 (列宽), INV-11 (行高), INV-12 (value 按字符换行)
// 高 DPI: QFontMetricsF 必须带 m_device 参数（specs/横切关注点/40-高DPI适配.md INV-2）
LayoutBlock PreviewLayout::layoutFrontmatter(const AstNode* node, qreal maxWidth)
{
    LayoutBlock block;
    block.type = LayoutBlock::Frontmatter;
    block.frontmatterEntries = node->frontmatterEntries;
    block.frontmatterRawText = node->frontmatterRawText;

    // Spec §INV-9：frontmatter 使用 monoFont 字体族 + baseFont 字号
    QFont fmFont = m_monoFont;
    fmFont.setPointSizeF(m_baseFont.pointSizeF());

    // 度量必须带 device —— 高 DPI INV-2（性能优化 B：缓存复用）
    const QFontMetricsF& fm = cachedFontMetrics(fmFont);
    // INV-11：行高由字体度量派生（baseFont 字号 > monoFont 字号，不再复用 codeLineHeight）
    const qreal lineH = fm.height() * 1.4;
    const qreal hPad = fm.height() * 0.5;              // 左右内边距，由字体度量派生（禁止硬编码）
    const qreal vPad = fm.height() * 0.4;              // 上下内边距

    // 最长 key 宽度
    qreal maxKeyW = 0;
    for (const auto& kv : block.frontmatterEntries)
        maxKeyW = qMax(maxKeyW, fm.horizontalAdvance(kv.first));

    // INV-10：首列宽度 = max(maxKey + 2*innerCellPad, innerWidth*0.5 上限)
    const qreal innerCellPad = fm.height() * 0.25;      // 列内小内边距
    const qreal innerWidth = qMax<qreal>(1.0, maxWidth - 2 * hPad);
    const qreal cap = innerWidth * 0.5;
    qreal keyColW = qMin(maxKeyW + 2 * innerCellPad, cap);
    if (keyColW < innerCellPad * 2)
        keyColW = innerCellPad * 2;  // 保底，即使 entries 为空
    const qreal valColW = qMax<qreal>(1.0, innerWidth - keyColW);

    // INV-12：value 按字符换行
    const qreal avgCharW = qMax<qreal>(1.0, fm.averageCharWidth());
    int totalLines = 0;
    for (const auto& kv : block.frontmatterEntries) {
        const int valCharsPerLine = qMax(1, static_cast<int>(qFloor(valColW / avgCharW)));
        const int len = kv.second.length();
        const int valLines = (len <= 0) ? 1 : static_cast<int>(qCeil(qreal(len) / qreal(valCharsPerLine)));
        totalLines += qMax(1, valLines);
    }
    if (totalLines <= 0)
        totalLines = 1;  // 无 entry 时仍占一行（视觉上呈现为空白带）

    const qreal height = 2 * vPad + totalLines * lineH;

    block.frontmatterKeyColumnWidth = keyColW;
    block.bounds = QRectF(0, 0, maxWidth, height);
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
            // 收集子节点文本
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
            // Spec: specs/模块-app/12-主题插件系统.md — 行内代码专用色（fallback 到 previewCode*）
            run.color = m_theme.previewInlineCodeFg;
            run.bgColor = m_theme.previewInlineCodeBg;
            runs.push_back(std::move(run));
            break;
        }
        case AstNodeType::Link: {
            QColor linkColor = m_theme.previewLink;
            collectInlineRuns(child.get(), runs, currentFont, linkColor);
            // 为刚添加的所有 run 设置 linkUrl
            // 简单方案：从当前数组末尾向前标记颜色为链接色的 run
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
            // 行内图片 - 作为文本占位显示
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
            // 按原始文本渲染
            InlineRun run;
            run.text = child->literal;
            run.font = currentFont;
            run.color = m_theme.syntaxFence;
            runs.push_back(std::move(run));
            break;
        }
        default:
            // 递归处理其他行内容器
            collectInlineRuns(child.get(), runs, currentFont, currentColor);
            break;
        }
    }
}

qreal PreviewLayout::estimateParagraphHeight(const std::vector<InlineRun>& runs, qreal maxWidth) const
{
    if (runs.empty()) return m_lineHeight;

    // 行高必须与 paintInlineRuns 一致：渲染侧使用 firstRun.font.height() * m_lineSpacingFactor
    // [Spec 模块-preview/02 INV-10 + INV-13]
    const QFontMetricsF& firstRunFm = cachedFontMetrics(runs[0].font);
    qreal lineHeight = firstRunFm.height() * m_lineSpacingFactor;
    qreal glyphHeight = firstRunFm.height();

    // [Spec 模块-preview/02 INV-TABLE-CELL-NO-OVERFLOW]（2026-05-06 新增）
    // 历史 bug（plans/归档/2026-05-06-表格单元格内容超出与下方重合.md）：
    //   原算法 totalLines = qCeil(segmentWidth / maxWidth) 是「整段总宽度 ÷ 行宽 上取整」，
    //   假设字符可以紧凑填满每一行。实际 paintInlineRuns 是逐字符换行——每行尾部
    //   只要剩余空间放不下下一个字符就立即换行，**每行末尾几乎必有取整浪费**。
    //   在窄列（如 1/3 屏幕宽的表格 cell）下，单行只能放 2-3 个汉字，多 run 切换更糟，
    //   导致 estimate 比实际行数少 30-50%。表格中体现为 cell.bounds.height < 真实占用，
    //   下一行 row 起点压在上一行末尾 → cell 内容视觉重叠。
    // 修复：直接模拟 paintInlineRuns 的逐字符换行算法，与渲染端字节级对齐。
    qreal safeMaxWidth = qMax(maxWidth, 1.0);
    qreal x = 0;
    qreal curX = 0;
    int totalLines = 1;

    auto findFitCount = [&](const QFontMetricsF& fm, const QString& str, qreal remaining) -> int {
        int fit = 0;
        qreal accW = 0;
        for (int i = 0; i < str.length(); ++i) {
            accW += fm.horizontalAdvance(str[i]);
            if (accW > remaining) break;
            fit = i + 1;
        }
        return fit;
    };

    for (const auto& run : runs) {
        if (run.text == "\n") {
            curX = x;
            ++totalLines;
            continue;
        }
        const QFontMetricsF& fm = cachedFontMetrics(run.font);
        QString text = run.text;
        while (!text.isEmpty()) {
            qreal fullWidth = fm.horizontalAdvance(text);
            qreal remaining = x + safeMaxWidth - curX;
            // 与 PreviewPainter::paintInlineRuns 的换行分支保持完全一致
            if (fullWidth <= remaining || curX <= x) {
                if (fullWidth > remaining && curX <= x) {
                    int fitCount = findFitCount(fm, text, remaining);
                    if (fitCount <= 0) fitCount = 1;
                    text = text.mid(fitCount);
                    curX = x;
                    ++totalLines;
                    continue;
                }
                curX += fullWidth;
                break;
            }
            int wrapAt = findFitCount(fm, text, remaining);
            if (wrapAt <= 0) {
                curX = x;
                ++totalLines;
                continue;
            }
            text = text.mid(wrapAt);
            curX = x;
            ++totalLines;
        }
    }

    // [Spec 模块-preview/02 INV-11] N-1 个完整行高 + 末行墨迹高度
    return (totalLines - 1) * lineHeight + glyphHeight;
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

    // 二分查找最近的源码行
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

    // 在相邻映射之间线性插值
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

    // 按 Y 坐标排序
    std::sort(mappings.begin(), mappings.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // 查找最近的 Y 坐标
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

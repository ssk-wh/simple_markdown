#include "PreviewWidget.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "ImageCache.h"
#include "TocPanel.h"
#include "MarkdownAst.h"

#include <QPainter>
#include <QScrollBar>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QSet>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new PreviewLayout();
    m_layout->setFont(font());

    m_painter = new PreviewPainter();

    m_imageCache = new ImageCache(this);

    viewport()->setAutoFillBackground(true);
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, Qt::white);
    viewport()->setPalette(pal);

    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    m_tocPanel = new TocPanel(viewport());
    connect(m_tocPanel, &TocPanel::headingClicked,
            this, &PreviewWidget::scrollToSourceLine);
}

PreviewWidget::~PreviewWidget()
{
    delete m_layout;
    delete m_painter;
    // m_imageCache is owned by QObject parent
}

PreviewLayout* PreviewWidget::previewLayout() const
{
    return m_layout;
}

void PreviewWidget::updateAst(std::shared_ptr<AstNode> root)
{
    m_currentAst = std::move(root);

    qreal contentWidth = m_wordWrap ? (viewport()->width() - 40) : 10000;
    if (contentWidth < 100) contentWidth = 100;

    m_layout->setViewportWidth(contentWidth);
    m_layout->buildFromAst(m_currentAst);

    m_plainText = extractPlainText();
    m_selStart = m_selEnd = -1;
    m_highlights.clear();  // 切换文档时清空标记

    buildHeadingCharOffsets();  // 收集标题字符位置
    updateScrollBars();
    updateTocEntries();
    viewport()->update();
}

void PreviewWidget::rebuildLayout()
{
    if (!m_currentAst) return;
    qreal contentWidth = m_wordWrap ? (viewport()->width() - 40) : 10000;
    if (contentWidth < 100) contentWidth = 100;
    m_layout->setViewportWidth(contentWidth);
    m_layout->buildFromAst(m_currentAst);
    updateScrollBars();
}

void PreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), m_theme.previewBg);

    if (!m_currentAst) return;

    // 检测 DPI 变化（切换屏幕时用新设备度量重建布局）
    qreal currentDpr = viewport()->devicePixelRatioF();
    if (!qFuzzyCompare(currentDpr, m_lastDevicePixelRatio)) {
        m_lastDevicePixelRatio = currentDpr;
        m_layout->updateMetrics(viewport());
        rebuildLayout();
    }

    qreal scrollY = verticalScrollBar()->value();
    qreal vpHeight = viewport()->height();
    qreal vpWidth = viewport()->width();

    // Apply 20px horizontal padding, minus horizontal scroll
    qreal scrollXVal = m_wordWrap ? 0 : horizontalScrollBar()->value();
    painter.translate(20 - scrollXVal, 0);

    m_painter->setSelection(m_selStart, m_selEnd);
    m_painter->setHighlights(m_highlights);
    qreal contentWidth = m_wordWrap ? (vpWidth - 40) : 10000;
    m_painter->paint(&painter, m_layout->rootBlock(), scrollY, vpHeight, contentWidth);
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    rebuildLayout();
    m_tocPanel->reposition();
}

void PreviewWidget::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    viewport()->update();
}

void PreviewWidget::updateScrollBars()
{
    qreal totalH = m_layout->totalHeight();
    qreal vpH = viewport()->height();
    int maxScroll = qMax(0, static_cast<int>(totalH - vpH));
    verticalScrollBar()->setRange(0, maxScroll);
    verticalScrollBar()->setPageStep(static_cast<int>(vpH));
    verticalScrollBar()->setSingleStep(20);

    if (!m_wordWrap) {
        qreal totalW = m_layout->rootBlock().bounds.width() + 40;
        qreal vpW = viewport()->width();
        horizontalScrollBar()->setRange(0, qMax(0, static_cast<int>(totalW - vpW)));
        horizontalScrollBar()->setPageStep(static_cast<int>(vpW));
        horizontalScrollBar()->setSingleStep(20);
    } else {
        horizontalScrollBar()->setRange(0, 0);
    }
}

void PreviewWidget::setTheme(const Theme& theme)
{
    m_theme = theme;
    m_painter->setTheme(theme);
    m_layout->setTheme(theme);
    m_tocPanel->setTheme(theme);

    // 重建 layout 以更新 InlineRun 中的主题色
    if (m_currentAst) {
        m_layout->buildFromAst(m_currentAst);
        updateScrollBars();
    }

    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, theme.previewBg);
    viewport()->setPalette(pal);

    viewport()->update();
}

void PreviewWidget::setWordWrap(bool enabled)
{
    m_wordWrap = enabled;

    if (enabled) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }

    if (m_currentAst) {
        qreal contentWidth = m_wordWrap ? (viewport()->width() - 40) : 10000;
        if (contentWidth < 100) contentWidth = 100;
        m_layout->setViewportWidth(contentWidth);
        m_layout->buildFromAst(m_currentAst);
        m_plainText = extractPlainText();
        updateScrollBars();
    }
    viewport()->update();
}

void PreviewWidget::scrollToSourceLine(int line)
{
    qreal y = m_layout->sourceLineToY(line);
    verticalScrollBar()->setValue(static_cast<int>(y));
}

void PreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // CRITICAL: DPI 改变检查
        // 问题场景：窗口从 A 屏（DPI=1.0）移到 B 屏（DPI=1.25）时：
        //   1. 鼠标事件在下一次 paint 之前可能先来
        //   2. textSegments 仍是旧 DPI 坐标
        //   3. event->pos() 已是新 DPI 坐标
        //   4. 坐标变换不匹配，导致 textIndexAtPoint 返回错误位置
        // 解决方案：在处理鼠标前同步 DPI，强制重建布局和 textSegments
        qreal currentDpr = viewport()->devicePixelRatioF();
        if (!qFuzzyCompare(currentDpr, m_lastDevicePixelRatio)) {
            m_lastDevicePixelRatio = currentDpr;
            m_layout->updateMetrics(viewport());
            rebuildLayout();
            // 强制 paint 来重新生成 textSegments（使用当前 DPI）
            // repaint() 立即执行，不是异步的 update()
            viewport()->repaint();
        }

        m_selecting = true;
        // segment rects 在 painter translate 后的视口坐标系，鼠标也转到同一坐标系
        // 坐标变换规则（与 paintEvent 中的 painter.translate(20 - scrollXVal, 0) 对应）：
        // 视口坐标 -> 内容坐标 = 减去 20 的左边距，加上水平滚动值
        qreal scrollXVal = m_wordWrap ? 0 : horizontalScrollBar()->value();
        QPointF pt(event->pos().x() - 20 + scrollXVal, event->pos().y());
        m_selStart = m_selEnd = textIndexAtPoint(pt);
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        // 同 mousePressEvent：坐标变换规则与 paintEvent 中的 translate 对应
        qreal scrollXVal = m_wordWrap ? 0 : horizontalScrollBar()->value();
        QPointF pt(event->pos().x() - 20 + scrollXVal, event->pos().y());
        m_selEnd = textIndexAtPoint(pt);
        viewport()->update();
    }
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    m_selecting = false;
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_plainText.isEmpty()) {
        // CRITICAL: DPI 改变检查（同 mousePressEvent）
        // 双击事件中也需要检查 DPI 变化，否则跨屏移动后坐标不同步
        qreal currentDpr = viewport()->devicePixelRatioF();
        if (!qFuzzyCompare(currentDpr, m_lastDevicePixelRatio)) {
            m_lastDevicePixelRatio = currentDpr;
            m_layout->updateMetrics(viewport());
            rebuildLayout();
            viewport()->repaint();
        }

        // 同 mousePressEvent：坐标变换规则与 paintEvent 中的 translate 对应
        qreal scrollXVal = m_wordWrap ? 0 : horizontalScrollBar()->value();
        QPointF pt(event->pos().x() - 20 + scrollXVal, event->pos().y());
        int idx = textIndexAtPoint(pt);

        if (idx >= 0 && idx < m_plainText.length()) {
            auto isWordChar = [](QChar c) {
                return c.isLetterOrNumber() || c == QLatin1Char('_');
            };

            if (isWordChar(m_plainText.at(idx))) {
                int start = idx;
                while (start > 0 && isWordChar(m_plainText.at(start - 1)))
                    --start;
                int end = idx + 1;
                while (end < m_plainText.length() && isWordChar(m_plainText.at(end)))
                    ++end;
                m_selStart = start;
                m_selEnd = end;
            } else {
                m_selStart = idx;
                m_selEnd = idx + 1;
            }

            m_selecting = false;
            viewport()->update();
        }
    }
}

void PreviewWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        m_selStart = 0;
        m_selEnd = m_plainText.length();
        viewport()->update();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PreviewWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    int start = qMin(m_selStart, m_selEnd);
    int end = qMax(m_selStart, m_selEnd);

    QAction* copyAct = menu.addAction(tr("Copy"), this, &PreviewWidget::copySelection, QKeySequence::Copy);
    copyAct->setEnabled(start >= 0 && end > start);

    QAction* selectAllAct = menu.addAction(tr("Select All"), [this]() {
        m_selStart = 0;
        m_selEnd = m_plainText.length();
        viewport()->update();
    }, QKeySequence::SelectAll);
    Q_UNUSED(selectAllAct);

    menu.addSeparator();

    QAction* hlAct = menu.addAction(tr("Mark"), this, &PreviewWidget::addHighlight);
    hlAct->setEnabled(start >= 0 && end > start);

    QAction* clearHlAct = menu.addAction(tr("Clear All Marks"), this, &PreviewWidget::clearHighlights);
    clearHlAct->setEnabled(!m_highlights.isEmpty());

    menu.exec(event->globalPos());
}

void PreviewWidget::copySelection()
{
    if (m_selStart < 0 || m_selEnd < 0) return;
    int start = qMin(m_selStart, m_selEnd);
    int end = qMax(m_selStart, m_selEnd);
    if (start == end) return;

    QString sel = m_plainText.mid(start, end - start);
    QApplication::clipboard()->setText(sel);
}

void PreviewWidget::updateTocEntries()
{
    QVector<TocEntry> entries;
    if (m_currentAst) {
        // 递归提取节点内所有文本
        std::function<void(const AstNode*, QString&)> extractText = [&](const AstNode* n, QString& out) {
            if (!n->literal.isEmpty())
                out += n->literal;
            for (const auto& c : n->children)
                extractText(c.get(), out);
        };

        std::function<void(const AstNode*)> collect = [&](const AstNode* node) {
            if (node->type == AstNodeType::Heading && node->headingLevel >= 1) {
                TocEntry entry;
                entry.level = node->headingLevel;
                entry.sourceLine = node->startLine;
                extractText(node, entry.title);
                if (!entry.title.isEmpty())
                    entries.append(entry);
            }
            for (const auto& child : node->children)
                collect(child.get());
        };
        collect(m_currentAst.get());
    }
    m_tocPanel->setEntries(entries);
}

QString PreviewWidget::extractPlainText() const
{
    QString text;
    extractBlockText(m_layout->rootBlock(), text);
    return text;
}

void PreviewWidget::extractBlockText(const LayoutBlock& block, QString& out) const
{
    // Inline text - 匹配 paintInlineRuns 的计数：所有 run.text + 无条件分隔换行
    if (!block.inlineRuns.empty()) {
        for (const auto& run : block.inlineRuns) {
            out += run.text;
        }
        out += '\n';
    }

    // Code block - 匹配 paintBlock 的逐行计数，跳过 split 产生的尾部空元素
    if (!block.codeText.isEmpty()) {
        const QStringList lines = block.codeText.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (i == lines.size() - 1 && lines[i].isEmpty())
                break;
            out += lines[i];
            out += '\n';
        }
    }

    // Recurse into children
    for (const auto& child : block.children) {
        extractBlockText(child, out);
    }
}

// 在段内使用字体度量逐字精确定位字符索引
static int hitTestSegment(const TextSegment& seg, qreal relX)
{
    if (seg.text.isEmpty()) return seg.charStart;
    QFontMetricsF fm(seg.font);
    for (int i = 0; i < seg.text.length(); ++i) {
        qreal w = fm.horizontalAdvance(seg.text.left(i + 1));
        if (relX < w) {
            qreal prevW = (i > 0) ? fm.horizontalAdvance(seg.text.left(i)) : 0;
            return seg.charStart + ((relX - prevW < w - relX) ? i : i + 1);
        }
    }
    return seg.charStart + seg.charLen;
}

int PreviewWidget::textIndexAtPoint(const QPointF& point) const
{
    const auto& segments = m_painter->textSegments();
    if (segments.isEmpty())
        return 0;

    int closest = 0;
    qreal closestDist = std::numeric_limits<qreal>::max();

    for (const auto& seg : segments) {
        if (seg.rect.contains(point)) {
            return hitTestSegment(seg, point.x() - seg.rect.x());
        }

        // 计算点到矩形的 2D 距离（解决表格单元格间隙定位问题）
        qreal dx = 0, dy = 0;
        if (point.x() < seg.rect.left())
            dx = seg.rect.left() - point.x();
        else if (point.x() > seg.rect.right())
            dx = point.x() - seg.rect.right();
        if (point.y() < seg.rect.top())
            dy = seg.rect.top() - point.y();
        else if (point.y() > seg.rect.bottom())
            dy = point.y() - seg.rect.bottom();

        qreal dist = dy * dy + dx * dx;

        if (dist < closestDist) {
            closestDist = dist;
            if (point.x() >= seg.rect.right())
                closest = seg.charStart + seg.charLen;
            else if (point.x() <= seg.rect.left())
                closest = seg.charStart;
            else
                closest = hitTestSegment(seg, point.x() - seg.rect.x());
        }
    }

    return closest;
}

void PreviewWidget::addHighlight()
{
    int s = qMin(m_selStart, m_selEnd);
    int e = qMax(m_selStart, m_selEnd);
    if (s < 0 || e <= s) return;

    // 检查是否已存在重叠的标记
    for (const auto& hl : m_highlights) {
        if ((s >= hl.first && s < hl.second) ||
            (e > hl.first && e <= hl.second) ||
            (s <= hl.first && e >= hl.second)) {
            // 已存在重叠标记，不重复添加
            return;
        }
    }

    m_highlights.append({s, e});
    updateTocHighlights();
    viewport()->update();
}

void PreviewWidget::clearHighlights()
{
    m_highlights.clear();
    m_tocPanel->setHighlightedEntries({});
    viewport()->update();
}

void PreviewWidget::updateTocHighlights()
{
    QSet<int> highlighted;

    // 对每个高亮范围，找到所属的标题
    for (const auto& hl : m_highlights) {
        int hlStart = hl.first;

        // 找到最后一个 <= hlStart 的标题
        int titleIdx = -1;
        for (int i = 0; i < m_headingCharOffsets.size(); ++i) {
            if (m_headingCharOffsets[i] <= hlStart) {
                titleIdx = i;
            } else {
                break;
            }
        }

        if (titleIdx >= 0) {
            highlighted.insert(titleIdx);
        }
    }

    m_tocPanel->setHighlightedEntries(highlighted);
}

void PreviewWidget::buildHeadingCharOffsets()
{
    m_headingCharOffsets.clear();

    // 遍历布局，记录每个 Heading 块的字符起始位置
    std::function<void(const LayoutBlock&, int&)> collectHeadings =
        [&](const LayoutBlock& block, int& charIdx) {
            if (block.type == LayoutBlock::Heading) {
                m_headingCharOffsets.append(charIdx);
            }

            // 计算这个块的字符数
            if (!block.inlineRuns.empty()) {
                for (const auto& run : block.inlineRuns) {
                    charIdx += run.text.length();
                }
                charIdx++;  // block separator newline
            }

            if (!block.codeText.isEmpty()) {
                const QStringList lines = block.codeText.split('\n');
                for (int i = 0; i < lines.size(); ++i) {
                    if (i == lines.size() - 1 && lines[i].isEmpty())
                        break;
                    charIdx += lines[i].length() + 1;
                }
            }

            // 递归子块
            for (const auto& child : block.children) {
                collectHeadings(child, charIdx);
            }
        };

    int charIdx = 0;
    for (const auto& child : m_layout->rootBlock().children) {
        collectHeadings(child, charIdx);
    }
}

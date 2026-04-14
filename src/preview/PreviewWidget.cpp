#include "PreviewWidget.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "ImageCache.h"
#include "TocPanel.h"  // for TocEntry
#include "MarkdownAst.h"
#include "MarkdownParser.h"

#include <QPainter>
#include <QScrollBar>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QSet>
#include <QMimeData>
#include <QPropertyAnimation>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new PreviewLayout();
    m_layout->setFont(font());

    m_painter = new PreviewPainter();
    m_painter->setLayout(m_layout);  // Spec 模块-preview/03 INV-9
    m_imageCache = new ImageCache(this);
    m_painter->setImageCache(m_imageCache);

    viewport()->setAutoFillBackground(true);
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, Qt::white);
    viewport()->setPalette(pal);

    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

}

PreviewWidget::~PreviewWidget()
{
    if (m_scrollAnimation) {
        m_scrollAnimation->disconnect();
        m_scrollAnimation->stop();
    }
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

    // 初始化 DPI 度量（重要：在布局计算前同步，避免高 DPI 初始化错误）
    qreal currentDpr = viewport()->devicePixelRatioF();
    if (m_lastDevicePixelRatio <= 0) {
        m_lastDevicePixelRatio = currentDpr;
        m_layout->updateMetrics(viewport());
    }

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

    // 每次 paintEvent 都用 painter.device() 同步度量
    // 直接检测字体度量是否变化（比 DPR 比较更可靠，能捕获跨屏不同物理 DPI 的情况）
    if (m_layout->updateMetrics(painter.device())) {
        m_lastDevicePixelRatio = viewport()->devicePixelRatioF();
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
    m_painter->setTargetLineHighlight(m_targetSourceLine, m_highlightOpacity);
    qreal contentWidth = m_wordWrap ? (vpWidth - 40) : 10000;
    m_painter->paint(&painter, m_layout->rootBlock(), scrollY, vpHeight, contentWidth);
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);

    // 同步 DPI 度量，确保高 DPI 初始化正确
    qreal currentDpr = viewport()->devicePixelRatioF();
    if (!qFuzzyCompare(currentDpr, m_lastDevicePixelRatio)) {
        m_lastDevicePixelRatio = currentDpr;
        m_layout->updateMetrics(viewport());
    }

    rebuildLayout();
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

void PreviewWidget::smoothScrollToSourceLine(int line)
{
    qreal targetY = m_layout->sourceLineToY(line);
    int currentY = verticalScrollBar()->value();
    int targetYInt = static_cast<int>(targetY);

    // 如果已经在目标位置附近，直接跳转
    if (qAbs(targetYInt - currentY) < 10) {
        verticalScrollBar()->setValue(targetYInt);
        return;
    }

    // 停止之前的动画
    if (m_scrollAnimation) {
        m_scrollAnimation->disconnect();
        m_scrollAnimation->stop();
        m_scrollAnimation->deleteLater();
        m_scrollAnimation = nullptr;
    }

    // 创建平滑滚动动画
    m_scrollAnimation = new QPropertyAnimation(verticalScrollBar(), "value", this);
    m_scrollAnimation->setDuration(300);  // 300ms 动画时长
    m_scrollAnimation->setStartValue(currentY);
    m_scrollAnimation->setEndValue(targetYInt);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_targetSourceLine = line;

    connect(m_scrollAnimation, &QPropertyAnimation::valueChanged, this, &PreviewWidget::onScrollAnimationValueChanged);
    connect(m_scrollAnimation, &QPropertyAnimation::finished, this, &PreviewWidget::onScrollAnimationFinished);

    m_scrollAnimation->start();
}

void PreviewWidget::onScrollAnimationValueChanged(const QVariant &value)
{
    Q_UNUSED(value);
    viewport()->update();
}

void PreviewWidget::onScrollAnimationFinished()
{
    // 动画结束后，启动高亮动画
    if (m_targetSourceLine >= 0) {
        // 创建透明度动画
        QPropertyAnimation* highlightAnim = new QPropertyAnimation(this, "highlightOpacity");
        highlightAnim->setDuration(500);  // 500ms 高亮持续时间
        highlightAnim->setStartValue(0.3);
        highlightAnim->setEndValue(0.0);
        highlightAnim->setEasingCurve(QEasingCurve::OutQuad);

        connect(highlightAnim, &QPropertyAnimation::valueChanged, this, [this]() {
            viewport()->update();
        });
        connect(highlightAnim, &QPropertyAnimation::finished, this, [this]() {
            m_targetSourceLine = -1;
            m_highlightOpacity = 0.0;
            viewport()->update();
        });

        highlightAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // 清理滚动动画
    if (m_scrollAnimation) {
        m_scrollAnimation->deleteLater();
        m_scrollAnimation = nullptr;
    }
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

        // segment rects 在 painter translate 后的视口坐标系，鼠标也转到同一坐标系
        qreal scrollXVal = m_wordWrap ? 0 : horizontalScrollBar()->value();
        QPointF pt(event->pos().x() - 20 + scrollXVal, event->pos().y());

        // [Spec 模块-preview/09 INV-1] Ctrl+LeftClick 触发链接打开
        if (event->modifiers() & Qt::ControlModifier) {
            for (const auto& seg : m_painter->textSegments()) {
                if (seg.linkUrl.isEmpty()) continue;
                if (seg.rect.contains(pt)) {
                    emit linkClicked(seg.linkUrl);
                    return;  // 不触发选区
                }
            }
        }

        m_selecting = true;
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
        return;
    }

    // [Spec 模块-preview/09 INV-5] 悬停链接时光标变手型（按 Ctrl 更明显）
    qreal scrollXVal = m_wordWrap ? 0 : horizontalScrollBar()->value();
    QPointF pt(event->pos().x() - 20 + scrollXVal, event->pos().y());
    bool overLink = false;
    for (const auto& seg : m_painter->textSegments()) {
        if (!seg.linkUrl.isEmpty() && seg.rect.contains(pt)) {
            overLink = true;
            break;
        }
    }
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
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
    bool hasSelection = (start >= 0 && end > start);

    // 复制相关
    QAction* copyAct = menu.addAction(tr("Copy as Plain Text"), this, &PreviewWidget::copySelection, QKeySequence::Copy);
    copyAct->setEnabled(hasSelection);

    QAction* copyHtmlAct = menu.addAction(tr("Copy as HTML"), this, &PreviewWidget::copyAsHtml);
    copyHtmlAct->setEnabled(hasSelection);

    QAction* selectAllAct = menu.addAction(tr("Select All"), [this]() {
        m_selStart = 0;
        m_selEnd = m_plainText.length();
        viewport()->update();
    }, QKeySequence::SelectAll);
    Q_UNUSED(selectAllAct);

    menu.addSeparator();

    // 标记相关
    QAction* hlAct = menu.addAction(tr("Mark"), this, &PreviewWidget::addHighlight);
    hlAct->setEnabled(hasSelection);

    QAction* clearHlAct = menu.addAction(tr("Clear All Marks"), this, &PreviewWidget::clearHighlights);
    clearHlAct->setEnabled(!m_highlights.isEmpty());

    menu.addSeparator();

    // 预览操作
    menu.addAction(tr("Open in Browser"), this, &PreviewWidget::openInBrowser);
    menu.addAction(tr("Refresh Preview"), this, &PreviewWidget::refreshPreview);

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

void PreviewWidget::copyAsHtml()
{
    if (m_selStart < 0 || m_selEnd < 0) return;
    int start = qMin(m_selStart, m_selEnd);
    int end = qMax(m_selStart, m_selEnd);
    if (start == end) return;

    // 提取选中的纯文本作为 Markdown
    QString selectedMarkdown = m_plainText.mid(start, end - start);

    // 使用 MarkdownParser 将选中文本转换为 HTML
    MarkdownParser parser;
    QString html = parser.renderHtml(selectedMarkdown);

    // 设置到剪贴板，同时提供纯文本和 HTML 格式
    QMimeData* mimeData = new QMimeData();
    mimeData->setText(selectedMarkdown);
    mimeData->setHtml(html);
    QApplication::clipboard()->setMimeData(mimeData);
}

void PreviewWidget::openInBrowser()
{
    emit openInBrowserRequested();
}

void PreviewWidget::refreshPreview()
{
    if (m_currentAst) {
        m_layout->buildFromAst(m_currentAst);
        m_plainText = extractPlainText();
        buildHeadingCharOffsets();
        updateScrollBars();
        updateTocEntries();
        viewport()->update();
    }
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
    m_tocEntries = entries;
    emit tocEntriesChanged(entries);
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
// device 参数必须传入，以确保高 DPI 下的度量与绘制时一致
static int hitTestSegment(const TextSegment& seg, qreal relX, QPaintDevice* device)
{
    if (seg.text.isEmpty()) return seg.charStart;
    QFontMetricsF fm(seg.font, device);  // 使用 device 参数确保度量一致
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

    // 获取 device 以确保高 DPI 下的度量一致性
    QPaintDevice* device = viewport();

    int closest = 0;
    qreal closestDist = std::numeric_limits<qreal>::max();

    for (const auto& seg : segments) {
        if (seg.rect.contains(point)) {
            return hitTestSegment(seg, point.x() - seg.rect.x(), device);
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
                closest = hitTestSegment(seg, point.x() - seg.rect.x(), device);
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
    m_tocHighlighted.clear();
    emit tocHighlightChanged(m_tocHighlighted);
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

    m_tocHighlighted = highlighted;
    emit tocHighlightChanged(highlighted);
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

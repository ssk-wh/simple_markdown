#include "PreviewWidget.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "ImageCache.h"
#include "TocPanel.h"  // for TocEntry
#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PerfProbe.h"

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
#include <QDataStream>
#include <QIODevice>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new PreviewLayout();
    m_layout->setFont(font());

    m_painter = new PreviewPainter();
    m_painter->setLayout(m_layout);  // Spec 模块-preview/03 INV-9
    m_imageCache = new ImageCache(this);
    m_painter->setImageCache(m_imageCache);
    m_layout->setImageCache(m_imageCache);

    // 图片异步加载完成后刷新预览
    connect(m_imageCache, &ImageCache::imageReady, this, [this](const QString& /*url*/) {
        // 重新布局以更新图片尺寸，然后刷新视口
        if (m_currentAst) {
            rebuildLayout();
            m_plainText = extractPlainText();
            updateScrollBars();
        }
        viewport()->update();
    });

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
    // m_imageCache 由 QObject 父对象管理
}

PreviewLayout* PreviewWidget::previewLayout() const
{
    return m_layout;
}

void PreviewWidget::updateAst(std::shared_ptr<AstNode> root)
{
    m_currentAst = std::move(root);

    // [Plan 2026-05-06-多Tab超阈值时休眠最早文档]
    // root == nullptr 是显式"清空预览状态"信号（典型场景：MainWindow 调用 dormantTab
    // 把 Tab 反向回到 lazy 状态时，需要释放 PreviewLayout 内存 + 清掉 highlights/TOC）。
    // 与编辑路径（root 非空）不同，此路径必须主动清空 m_highlights/m_tocHighlighted/
    // m_tocEntries 并 emit，让 TocPanel 同步进入"无内容"状态。
    if (!m_currentAst) {
        m_layout->buildFromAst(nullptr);
        m_plainText.clear();
        m_selStart = m_selEnd = -1;
        m_highlights.clear();
        m_pendingMarkings.clear();
        m_headingCharOffsets.clear();
        m_tocHighlighted.clear();
        emit tocHighlightChanged(m_tocHighlighted);
        m_tocEntries.clear();
        emit tocEntriesChanged(m_tocEntries);
        updateScrollBars();
        viewport()->update();
        return;
    }

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
    // [Spec 模块-preview/08 INV-MARK-EDIT-PRESERVE]（2026-05-06 plan #8 Step 1）
    // **不清空** m_highlights——updateAst 由 ParseScheduler 在用户编辑时频繁触发，
    // 原本的 m_highlights.clear() 会让用户每输入一字就丢失标记。
    // 真正需要清空的场景（切换/重建 PreviewWidget、用户主动 clearHighlights）由调用方处理：
    //   - openFile 创建新 PreviewWidget：m_highlights 默认空，无需 clear
    //   - promptReloadTab：用户期望同文档内容更新后保留标记
    //   - 用户右键"清除标记"：调 clearHighlights() 显式触发
    // 字符偏移可能因编辑漂移到错误位置——容忍策略见 Spec §8.3，paint 端按字符偏移
    // 自然忽略越界部分，标记跑偏由用户感知后手动清除。

    buildHeadingCharOffsets();  // 收集标题字符位置
    // [Spec 模块-preview/08 INV-5] 会话恢复传入的标记在此兑现（pending 为空时 no-op）
    applyPendingMarkings();
    // [Spec 模块-preview/08 INV-4] 总是按当前 m_highlights + 新 m_headingCharOffsets
    // 重算 m_tocHighlighted 并 emit，保持 TOC 与 m_highlights 同步——
    // 既覆盖编辑后 highlight 保留+章节边界刷新的场景，
    // 也覆盖恢复时 applyPendingMarkings 写入新标记的场景（idempotent，可能重复 emit 一次）
    updateTocHighlights();
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
    SM_PERF_SCOPE("preview.paintEvent");
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

    // 应用 20px 水平内边距，减去水平滚动偏移
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

void PreviewWidget::setDocumentDir(const QString& dir)
{
    m_imageCache->setDocumentDir(dir);
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

// [Spec 模块-preview/02 INV-13] 把"视图 → 行间距"菜单的乘数推送到 layout，
// 触发段落 / List / Table 高度重算与重绘。代码块和 frontmatter 行高保留 1.4 基线。
void PreviewWidget::setLineSpacingFactor(qreal factor)
{
    if (!m_layout) return;
    if (qFuzzyCompare(m_layout->lineSpacingFactor(), factor))
        return;
    m_layout->setLineSpacingFactor(factor);
    if (m_currentAst)
        rebuildLayout();
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
        // 选中拖拽时自动滚动：鼠标在 viewport 上/下方时滚动
        int y = event->pos().y();
        int vh = viewport()->height();
        if (y < 0) {
            verticalScrollBar()->setValue(verticalScrollBar()->value() + y / 2);
        } else if (y > vh) {
            verticalScrollBar()->setValue(verticalScrollBar()->value() + (y - vh) / 2);
        }

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

    // 检测右键位置是否落在某个标记内
    qreal scrollXVal = m_wordWrap ? 0 : horizontalScrollBar()->value();
    QPointF clickPt(event->pos().x() - 20 + scrollXVal, event->pos().y());
    int clickIdx = textIndexAtPoint(clickPt);
    int hitMarkIdx = -1;
    for (int i = 0; i < m_highlights.size(); ++i) {
        if (clickIdx >= m_highlights[i].first && clickIdx < m_highlights[i].second) {
            hitMarkIdx = i;
            break;
        }
    }

    QAction* clearCurrentAct = menu.addAction(tr("Clear Current Mark"), [this, hitMarkIdx]() {
        if (hitMarkIdx >= 0 && hitMarkIdx < m_highlights.size()) {
            m_highlights.removeAt(hitMarkIdx);
            updateTocHighlights();
            viewport()->update();
        }
    });
    clearCurrentAct->setEnabled(hitMarkIdx >= 0);

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
    // Spec: specs/模块-preview/10-Frontmatter渲染.md INV-13
    // Frontmatter：导出原始 YAML（rawText），与 paintBlock 里 m_charCounter 累加规则
    // （+= rawLen；非空再 +1 分隔符）一一对应
    if (block.type == LayoutBlock::Frontmatter) {
        out += block.frontmatterRawText;
        if (!block.frontmatterRawText.isEmpty())
            out += '\n';
        return;  // frontmatter 无子块，防御式短路
    }

    // 行内文本 - 匹配 paintInlineRuns 的计数：所有 run.text + 无条件分隔换行
    if (!block.inlineRuns.empty()) {
        for (const auto& run : block.inlineRuns) {
            out += run.text;
        }
        out += '\n';
    }

    // 代码块 - 匹配 paintBlock 的逐行计数，跳过 split 产生的尾部空元素
    if (!block.codeText.isEmpty()) {
        const QStringList lines = block.codeText.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (i == lines.size() - 1 && lines[i].isEmpty())
                break;
            out += lines[i];
            out += '\n';
        }
    }

    // 递归处理子块
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

    // 合并重叠的标记区域
    QVector<QPair<int,int>> merged;
    bool inserted = false;
    for (const auto& hl : m_highlights) {
        if (hl.second < s || hl.first > e) {
            merged.append(hl);  // 不重叠，保留
        } else {
            // 重叠：扩展新标记的范围
            s = qMin(s, hl.first);
            e = qMax(e, hl.second);
        }
    }
    merged.append({s, e});
    m_highlights = merged;
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

// [Spec 模块-preview/08 INV-5、§4 接口] 标记会话级序列化
// 格式：
//   magic    : quint32 = 0x534D4D4B ("SMMK")
//   version  : quint8  = 1
//   count    : quint32
//   entries  : (qint32 startOffset, qint32 endOffset) × count
QByteArray PreviewWidget::serializeMarkings() const
{
    QByteArray buf;
    QDataStream out(&buf, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_12);
    out << quint32(0x534D4D4B);
    out << quint8(1);
    out << quint32(m_highlights.size());
    for (const auto& hl : m_highlights) {
        out << qint32(hl.first) << qint32(hl.second);
    }
    return buf;
}

// [Spec 模块-preview/08 INV-5、§4 接口] 标记会话级反序列化
// 采用"延迟应用"：先放进 m_pendingMarkings，由最近一次 updateAst 末尾的
// applyPendingMarkings() 兑现。如果 AST 已就绪则在此立即应用一次。
void PreviewWidget::deserializeMarkings(const QByteArray& data)
{
    if (data.isEmpty()) {
        m_pendingMarkings.clear();
        return;
    }
    m_pendingMarkings = data;
    applyPendingMarkings();
}

// [Spec 模块-preview/08 INV-5、§8.3] 把 m_pendingMarkings 应用到 m_highlights。
// 一次性消耗：成功后清空 m_pendingMarkings，避免后续编辑反复"恢复"成旧值。
//
// [关键修正 2026-05-06] AST 守卫**必须在消耗 m_pendingMarkings 之前**：
// MainWindow::restoreSession 调 deserializeMarkings 时 ParseScheduler 是异步的，
// m_currentAst 可能还为 nullptr。如果此时就消耗 m_pendingMarkings 写入 m_highlights，
// 紧接着 astReady → updateAst 会先 clear() m_highlights，再调用本函数——但
// m_pendingMarkings 已空 → no-op → 标记丢失。
//
// 修正后：AST 没就绪时直接早返回不消耗 m_pendingMarkings；保留到 updateAst 末尾
// （那时 m_currentAst 已就绪）才解码、写入、消耗。lazy tab 路径同理。
//
// 字符偏移漂移容忍：start<0 / end<=start / 起点已超出文档长度的条目静默丢弃，
// 终点超出文档长度时夹紧到末尾。格式不匹配的字节流整体丢弃。
void PreviewWidget::applyPendingMarkings()
{
    if (m_pendingMarkings.isEmpty()) return;
    // AST 没就绪 → 保留 m_pendingMarkings，等 updateAst 末尾再调用
    if (!m_currentAst) return;

    QDataStream in(m_pendingMarkings);
    in.setVersion(QDataStream::Qt_5_12);
    quint32 magic = 0;
    quint8 version = 0;
    quint32 count = 0;
    in >> magic >> version >> count;

    // 格式不匹配：直接丢弃，不影响当前会话
    if (in.status() != QDataStream::Ok || magic != 0x534D4D4B || version != 1) {
        m_pendingMarkings.clear();
        return;
    }

    QVector<QPair<int,int>> restored;
    restored.reserve(static_cast<int>(count));
    const int textLen = m_plainText.length();
    for (quint32 i = 0; i < count; ++i) {
        qint32 s = -1, e = -1;
        in >> s >> e;
        if (in.status() != QDataStream::Ok) break;
        if (s < 0 || e <= s) continue;
        if (textLen > 0 && s >= textLen) continue;
        if (textLen > 0 && e > textLen) e = textLen;
        restored.append({s, e});
    }

    m_highlights = restored;
    m_pendingMarkings.clear();
    updateTocHighlights();
    viewport()->update();
}

void PreviewWidget::clearHighlightsInSection(int sectionIdx)
{
    if (sectionIdx < 0 || sectionIdx >= m_headingCharOffsets.size()) return;

    int secStart = m_headingCharOffsets[sectionIdx];
    int secEnd = (sectionIdx + 1 < m_headingCharOffsets.size())
                 ? m_headingCharOffsets[sectionIdx + 1]
                 : m_plainText.length();

    // 移除所有与该章节有交集的标记
    QVector<QPair<int,int>> remaining;
    for (const auto& hl : m_highlights) {
        if (hl.first >= secEnd || hl.second <= secStart) {
            remaining.append(hl);  // 不在该章节内
        }
    }
    m_highlights = remaining;
    updateTocHighlights();
    viewport()->update();
}

void PreviewWidget::updateTocHighlights()
{
    QSet<int> highlighted;

    // 对每个高亮范围，找到所有跨越的章节标题
    for (const auto& hl : m_highlights) {
        int hlStart = hl.first;
        int hlEnd = hl.second;

        for (int i = 0; i < m_headingCharOffsets.size(); ++i) {
            int headStart = m_headingCharOffsets[i];
            int headEnd = (i + 1 < m_headingCharOffsets.size())
                          ? m_headingCharOffsets[i + 1]
                          : m_plainText.length();
            // 标记范围与章节范围有交集则高亮该章节
            if (hlStart < headEnd && hlEnd > headStart) {
                highlighted.insert(i);
            }
        }
    }

    m_tocHighlighted = highlighted;
    emit tocHighlightChanged(highlighted);
}

void PreviewWidget::buildHeadingCharOffsets()
{
    m_headingCharOffsets.clear();

    // 遍历布局，记录每个 Heading 块的字符起始位置。
    // **关键：累加规则必须与 extractBlockText 字节级一致**，否则 m_headingCharOffsets
    // 与 m_plainText 字符流错位，TOC 高亮按章节范围判定时会指向错误章节。
    // 历史 bug（plan #14 子场景 3，2026-05-06 修复）：旧实现未处理 Frontmatter 块的
    // rawText 累加，但 extractBlockText 会把 rawText 写入 m_plainText——含 frontmatter
    // 的文档下所有章节 char offset 全部偏移 |rawText|+1，TOC 高亮章节归属错位。
    std::function<void(const LayoutBlock&, int&)> collectHeadings =
        [&](const LayoutBlock& block, int& charIdx) {
            // [子场景 3 修复] Frontmatter 必须按 extractBlockText 同款规则累加：rawText + \n
            if (block.type == LayoutBlock::Frontmatter) {
                charIdx += block.frontmatterRawText.length();
                if (!block.frontmatterRawText.isEmpty()) charIdx++;
                return;  // frontmatter 无子块，与 extractBlockText 防御式短路对齐
            }

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

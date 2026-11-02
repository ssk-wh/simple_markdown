// src/viewer/ViewerWidget.cpp
//
// Spec: specs/模块-viewer/04-查看器控件.md
// Invariants enforced here: INV-VIEW-NO-LAYOUT-IN-PAINT, INV-VIEW-ANCHOR, INV-SEL-CHUNK-LOCAL
// Last synced: 2026-11-02
#include "ViewerWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFile>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <climits>

#include "FontDefaults.h"
#include "ImageCache.h"
#include "MarkdownAst.h"
#include "PerfProbe.h"
#include "PreviewPainter.h"
#include "TextHitTest.h"
#include "SearchBar.h"

namespace viewer {

namespace {
constexpr int kPadX = 20;                  // 左右内边距（与旧预览一致）
constexpr size_t kMaxCachedChunks = 160;   // 布局缓存上限
constexpr int kWholeChunk = INT_MAX / 2;   // 「到 chunk 末尾」哨兵
}  // namespace

ViewerWidget::ViewerWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new PreviewLayout();
    m_layout->setFont(font_defaults::defaultPreviewFont());
    m_painter = new PreviewPainter();
    m_painter->setLayout(m_layout);
    m_imageCache = new ImageCache(this);
    m_painter->setImageCache(m_imageCache);
    m_layout->setImageCache(m_imageCache);
    connect(m_imageCache, &ImageCache::imageReady, this, &ViewerWidget::onImageReady);

    m_loader = new DocumentLoader(this);
    connect(m_loader, &DocumentLoader::batchesAvailable, this, &ViewerWidget::onBatches);

    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    setFocusPolicy(Qt::ClickFocus);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &ViewerWidget::onScrollBarValue);
    connect(verticalScrollBar(), &QScrollBar::sliderPressed, this, [this]() { m_model.setCalibrationFrozen(true); });
    connect(verticalScrollBar(), &QScrollBar::sliderReleased, this, [this]() {
        m_model.setCalibrationFrozen(false);
        prepare();
    });

    // 测量回调：只负责布局并返回高度，绝不触碰绘制
    m_model.setMeasurer([this](size_t i) { return measureChunk(i); });
    m_model.setInitialPxPerLine(QFontMetricsF(font_defaults::defaultPreviewFont()).height() * 1.5);

    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(40);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this]() {
        const qreal w = contentWidth();
        if (!qFuzzyCompare(w, m_layoutWidth)) {
            m_layoutWidth = w;
            m_layout->setViewportWidth(w);
            invalidateAllLayouts();
        }
        prepare();
    });
    m_prefetchTimer.setSingleShot(true);
    m_prefetchTimer.setInterval(30);
    connect(&m_prefetchTimer, &QTimer::timeout, this, &ViewerWidget::prefetch);
    m_tocTimer.setSingleShot(true);
    m_tocTimer.setInterval(150);
    connect(&m_tocTimer, &QTimer::timeout, this, [this]() { emit tocEntriesChanged(m_tocEntries); });
    m_positionTimer.setSingleShot(true);
    m_positionTimer.setInterval(400);
    connect(&m_positionTimer, &QTimer::timeout, this, [this]() { emit positionChanged(); });

    m_wheelTimer.setInterval(15);
    connect(&m_wheelTimer, &QTimer::timeout, this, &ViewerWidget::tickWheel);
    m_autoScrollTimer.setInterval(30);
    connect(&m_autoScrollTimer, &QTimer::timeout, this, &ViewerWidget::autoScrollTick);

    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(120);
    connect(&m_searchDebounce, &QTimer::timeout, this, &ViewerWidget::startSearch);

    m_highlightAnim = new QVariantAnimation(this);
    m_highlightAnim->setDuration(1400);
    m_highlightAnim->setStartValue(1.0);
    m_highlightAnim->setEndValue(0.0);
    connect(m_highlightAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_targetOpacity = v.toReal();
        viewport()->update();
    });
    connect(m_highlightAnim, &QVariantAnimation::finished, this, [this]() {
        m_targetLine = -1;
        m_targetOpacity = 0;
        viewport()->update();
    });
}

ViewerWidget::~ViewerWidget()
{
    // m_loader 是子对象，析构时会 cancel worker（不等待）
    delete m_painter;
    delete m_layout;
}

// ---------------------------------------------------------------- 加载

void ViewerWidget::loadFile(const QString& path)
{
    m_path = path;
    m_error.clear();
    m_buffer.reset();
    m_watermark = 0;
    m_chunks.clear();
    m_prologues.clear();
    m_refDefs.clear();
    m_headings.clear();
    m_tocEntries.clear();
    m_lineCount = 0;
    m_loadDone = false;
    m_loading = true;
    m_firstContentEmitted = false;
    m_firstPaintMarked = false;
    m_firstContentMs = -1;
    m_visible.clear();
    m_cache.clear();
    m_model.reset();
    m_selA = m_selB = SelPos();
    m_segments.clear();
    clearSearchState();
    m_pendingJumpLine = -1;
    m_loadTimer.start();
    emit tocEntriesChanged(m_tocEntries);
    emit loadStateChanged();
    m_loader->start(path, m_scanOptions);
    viewport()->update();
}

int ViewerWidget::readingLine() const
{
    // 加载尚未完成时，待恢复的位置才是「真正的阅读位置」（此时 topSourceLine 恒为 0）
    return m_pendingJumpLine >= 0 ? m_pendingJumpLine : topSourceLine();
}

void ViewerWidget::reload()
{
    if (m_path.isEmpty()) return;
    const int line = readingLine();
    const QString p = m_path;
    loadFile(p);
    m_pendingJumpLine = line;
    m_pendingJumpHighlight = false;
}

void ViewerWidget::pumpForTest()
{
    onBatches();
}

void ViewerWidget::onBatches()
{
    SM_PERF_SCOPE("viewer.onBatches");
    const auto batches = m_loader->takeBatches();
    if (batches.empty()) return;
    bool tocChanged = false;
    bool anyChunks = false;
    for (const auto& b : batches) {
        if (!m_buffer && m_loader->state()) m_buffer = m_loader->state()->buffer;
        if (!b.error.isEmpty()) {
            m_error = b.error;
            m_loading = false;
            m_loadDone = true;
            continue;
        }
        m_watermark = b.watermark;
        for (const auto& c : b.chunks) {
            m_chunks.push_back(c);
            m_model.appendChunk(c.lineCount);
            anyChunks = true;
        }
        for (const auto& p : b.newPrologues) m_prologues.push_back(p);
        if (!b.refDefs.isEmpty()) m_refDefs = b.refDefs;
        for (const auto& h : b.headings) {
            m_headings.push_back(h);
            TocEntry e;
            e.title = h.title;
            e.level = h.level;
            e.sourceLine = static_cast<int>(h.line);
            m_tocEntries.push_back(e);
            tocChanged = true;
        }
        m_lineCount = b.lineCount;
        if (b.done) {
            m_loadDone = true;
            m_loading = false;
        }
    }
    if (tocChanged || m_loadDone) {
        // 目录更新节流，避免大文件扫描期间每批都重建目录面板
        if (m_loadDone) { m_tocTimer.stop(); emit tocEntriesChanged(m_tocEntries); }
        else if (!m_tocTimer.isActive()) m_tocTimer.start();
    }
    if (m_loadDone) {
        emit loadStateChanged();
        if (!m_searchText.isEmpty() && isSearchBarVisible()) startSearch();   // 加载期间的搜索只覆盖了已读部分
    }
    if (anyChunks || !m_error.isEmpty()) {
        applyPendingJump();
        prepare();
    }
}

QString ViewerWidget::readAllText() const
{
    if (!m_buffer) return QString();
    const char* d = m_buffer->constData();
    const qint64 n = static_cast<qint64>(std::min<uint64_t>(m_watermark, static_cast<uint64_t>(m_buffer->size())));
    qint64 start = 0;
    if (n >= 3 && static_cast<uint8_t>(d[0]) == 0xEF && static_cast<uint8_t>(d[1]) == 0xBB
        && static_cast<uint8_t>(d[2]) == 0xBF) start = 3;
    return QString::fromUtf8(d + start, static_cast<int>(n - start));
}

// ---------------------------------------------------------------- 外观

void ViewerWidget::setTheme(const Theme& theme)
{
    m_theme = theme;
    if (m_searchBar) m_searchBar->setTheme(theme);
    m_layout->setTheme(theme);
    m_painter->setTheme(theme);
    invalidateAllLayouts();
    prepare();
}

void ViewerWidget::setDocumentDir(const QString& dir)
{
    m_imageCache->setDocumentDir(dir);
}

void ViewerWidget::setLineSpacingFactor(qreal factor)
{
    m_layout->setLineSpacingFactor(factor);
}

void ViewerWidget::rebuildLayout()
{
    m_lastDpr = 0;  // 强制下次 ensureMetrics 重新读取度量
    invalidateAllLayouts();
    prepare();
}

qreal ViewerWidget::contentWidth() const
{
    return std::max<qreal>(100.0, viewport()->width() - 2 * kPadX);
}

qreal ViewerWidget::lineHeightPx() const
{
    return QFontMetricsF(m_layout->baseFont()).height() * 1.5;
}

void ViewerWidget::ensureMetrics()
{
    const qreal dpr = viewport()->devicePixelRatioF();
    bool changed = m_layout->updateMetrics(viewport());
    if (!qFuzzyCompare(dpr, m_lastDpr)) {
        m_lastDpr = dpr;
        changed = true;
    }
    if (m_layoutWidth < 0) {
        m_layoutWidth = contentWidth();
        m_layout->setViewportWidth(m_layoutWidth);
        changed = true;
    }
    if (changed) invalidateAllLayouts();
}

void ViewerWidget::invalidateAllLayouts()
{
    m_cache.clear();
    m_model.invalidateAll();
}

// ---------------------------------------------------------------- 布局/测量

double ViewerWidget::measureChunk(size_t i)
{
    return ensureLayout(i).layout.height;
}

ViewerWidget::CacheEntry& ViewerWidget::ensureLayout(size_t i)
{
    auto it = m_cache.find(i);
    if (it != m_cache.end()) {
        it->second.lastUse = ++m_useTick;
        return it->second;
    }
    // INV-VIEW-NO-LAYOUT-IN-PAINT：绘制期绝不允许新建布局/改动缓存与滚动模型
    Q_ASSERT_X(!m_inPaint, "ViewerWidget::ensureLayout", "layout requested inside paintEvent");
    if (m_inPaint) {
        static CacheEntry s_empty;
        s_empty.layout = PreviewLayout::ChunkLayoutResult();
        s_empty.layout.root.type = LayoutBlock::Document;
        return s_empty;
    }
    SM_PERF_SCOPE("viewer.layoutChunk");
    CacheEntry entry;
    if (i < m_chunks.size() && m_buffer) {
        const Chunk& c = m_chunks[i];
        const AssembledChunk a = ChunkScanner::assembleChunk(m_buffer->constData(), c, m_prologues, m_refDefs);
        AstNodePtr root = m_parser.parseChunk(a.text, static_cast<int>(c.lineStart) - a.lineBias, i == 0);
        entry.layout = m_layout->layoutChunk(*root, i == 0);
    } else {
        entry.layout.root.type = LayoutBlock::Document;
        entry.layout.height = m_layout->codeLineHeight();
    }
    entry.lastUse = ++m_useTick;
    const double h = entry.layout.height;
    auto res = m_cache.emplace(i, std::move(entry));
    // 缓存被淘汰后重新布局：高度可能与记录不同，写回模型（测量路径下 isMeasured=false，由模型自己写回）
    if (m_model.isMeasured(i) && std::abs(m_model.chunkHeight(i) - h) > 0.01)
        m_model.setMeasuredHeight(i, h);
    return res.first->second;
}

void ViewerWidget::prepare()
{
    if (m_inPrepare || m_inPaint) return;   // INV-VIEW-NO-LAYOUT-IN-PAINT
    QScopedValueRollback<bool> guard(m_inPrepare, true);
    SM_PERF_SCOPE("viewer.prepare");
    ensureMetrics();
    m_model.setViewportHeight(viewport()->height());
    for (int pass = 0; pass < 3; ++pass) {
        m_visible = m_model.visible(0.0);
        bool changed = false;
        for (const auto& v : m_visible) {
            const bool had = m_cache.count(v.index) > 0;
            ensureLayout(v.index);
            if (!had && std::abs(m_model.chunkHeight(v.index) - v.height) > 0.01) changed = true;
        }
        if (!changed) break;
    }
    if (!m_searchText.isEmpty() && !m_searchHits.empty()) {
        if (m_searchRendered.size() > 512) m_searchRendered.clear();   // 简单上限，防无界增长
        for (const auto& v : m_visible) renderedHitsFor(v.index);
    }
    syncScrollBar();
    evictCache();
    if (!m_firstContentEmitted && !m_visible.empty()) {
        m_firstContentEmitted = true;
        m_firstContentMs = m_loadTimer.isValid() ? m_loadTimer.elapsed() : 0;
        emit firstContentReady();
    }
    viewport()->update();
    schedulePrefetch();
}

void ViewerWidget::syncScrollBar()
{
    QScrollBar* bar = verticalScrollBar();
    const double maxTop = m_model.maxTopY();
    // 文档总高超过 ~1e9 像素（约 4000 万行）时，滚动条按比例缩放，保证能拖到真正的末尾
    constexpr double kMaxBarUnits = 1.0e9;
    m_barScale = maxTop > kMaxBarUnits ? maxTop / kMaxBarUnits : 1.0;
    const int maxV = static_cast<int>(std::min<double>(maxTop / m_barScale, kMaxBarUnits));
    const int page = std::max(1, static_cast<int>(viewport()->height() / m_barScale));
    QScopedValueRollback<bool> g(m_syncing, true);
    bar->setRange(0, maxV);
    bar->setPageStep(page);
    bar->setSingleStep(std::max(1, static_cast<int>(lineHeightPx() * 3 / m_barScale)));
    bar->setValue(static_cast<int>(std::min<double>(m_model.topY() / m_barScale, static_cast<double>(maxV))));
}

void ViewerWidget::evictCache()
{
    if (m_cache.size() <= kMaxCachedChunks) return;
    // 保留可视 chunk 及距离锚点最近的若干个，其余按距离从远到近淘汰
    const size_t anchor = m_model.anchorChunk();
    std::vector<std::pair<size_t, size_t>> byDist;  // (距离, 索引)
    byDist.reserve(m_cache.size());
    for (const auto& kv : m_cache) {
        const size_t d = kv.first > anchor ? kv.first - anchor : anchor - kv.first;
        byDist.emplace_back(d, kv.first);
    }
    std::sort(byDist.begin(), byDist.end(), std::greater<>());
    size_t toRemove = m_cache.size() - kMaxCachedChunks * 3 / 4;
    for (const auto& p : byDist) {
        if (toRemove == 0) break;
        bool visible = false;
        for (const auto& v : m_visible) if (v.index == p.second) { visible = true; break; }
        if (visible) continue;
        m_cache.erase(p.second);
        --toRemove;
    }
}

void ViewerWidget::schedulePrefetch()
{
    m_prefetchTimer.start();
}

void ViewerWidget::prefetch()
{
    // 空闲时预热视口上下各一屏：滚动进入时无需现场布局
    if (m_chunks.empty() || m_inPrepare || m_inPaint) return;
    QScopedValueRollback<bool> guard(m_inPrepare, true);
    const size_t anchor = m_model.anchorChunk();
    const double vh = viewport()->height();
    double acc = 0;
    for (size_t i = anchor + 1; i < m_chunks.size() && acc < vh; ++i) {
        acc += ensureLayout(i).layout.height;
        if (!m_model.isMeasured(i)) m_model.setMeasuredHeight(i, m_cache[i].layout.height);
    }
    acc = 0;
    for (size_t i = anchor; i-- > 0 && acc < vh;) {
        acc += ensureLayout(i).layout.height;
        if (!m_model.isMeasured(i)) m_model.setMeasuredHeight(i, m_cache[i].layout.height);
    }
    syncScrollBar();
}

void ViewerWidget::onImageReady(const QString& url)
{
    // 只失效引用该图片的 chunk：下次 prepare 重新布局，高度变化由锚点机制补偿
    bool any = false;
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        const auto& urls = it->second.layout.imageUrls;
        if (std::find(urls.begin(), urls.end(), url) != urls.end()) {
            m_model.invalidate(it->first);
            it = m_cache.erase(it);
            any = true;
        } else {
            ++it;
        }
    }
    if (any) QTimer::singleShot(0, this, [this]() { prepare(); });
}

// ---------------------------------------------------------------- 滚动

void ViewerWidget::onScrollBarValue(int v)
{
    if (m_syncing) return;
    m_model.setTopY(static_cast<double>(v) * m_barScale);
    prepare();
    m_positionTimer.start();
}

void ViewerWidget::scrollContentsBy(int, int) {}

void ViewerWidget::wheelEvent(QWheelEvent* e)
{
    double dy = 0;
    if (!e->pixelDelta().isNull()) {
        dy = -e->pixelDelta().y();
        m_wheelRemaining = 0;
        m_model.scrollBy(dy);
        prepare();
        m_positionTimer.start();
    } else {
        // 一格滚轮 = 3 行；累积到动画剩余量中，指数衰减逼近，形成顺滑滚动
        dy = -(e->angleDelta().y() / 120.0) * lineHeightPx() * 3.0;
        startWheelAnimation(dy);
    }
    e->accept();
}

void ViewerWidget::startWheelAnimation(double dy)
{
    // 反向滚动立即清空剩余量，保证跟手
    if ((dy > 0) != (m_wheelRemaining > 0)) m_wheelRemaining = 0;
    m_wheelRemaining += dy;
    if (!m_wheelTimer.isActive()) m_wheelTimer.start();
}

void ViewerWidget::tickWheel()
{
    if (std::abs(m_wheelRemaining) < 0.5) {
        m_wheelRemaining = 0;
        m_wheelTimer.stop();
        return;
    }
    double step = m_wheelRemaining * 0.28;
    if (std::abs(step) < 1.0) step = m_wheelRemaining > 0 ? 1.0 : -1.0;
    if (std::abs(step) > std::abs(m_wheelRemaining)) step = m_wheelRemaining;
    m_wheelRemaining -= step;
    m_model.scrollBy(step);
    prepare();
    m_positionTimer.start();
}

void ViewerWidget::scrollToTop()
{
    m_wheelRemaining = 0;
    m_model.scrollToStart();
    prepare();
    m_positionTimer.start();
}

void ViewerWidget::scrollToBottom()
{
    m_wheelRemaining = 0;
    m_model.scrollToEnd();
    prepare();
    m_positionTimer.start();
}

int ViewerWidget::chunkForLine(int line) const
{
    if (m_chunks.empty()) return -1;
    // 最后一个 lineStart <= line 的 chunk
    size_t lo = 0, hi = m_chunks.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (static_cast<int>(m_chunks[mid].lineStart) <= line) lo = mid + 1;
        else hi = mid;
    }
    return lo == 0 ? 0 : static_cast<int>(lo - 1);
}

void ViewerWidget::applyPendingJump()
{
    if (m_pendingJumpLine < 0) return;
    // 目标行尚未被扫描到：等后续批次
    const bool covered = !m_chunks.empty() &&
        (static_cast<int>(m_chunks.back().lineStart + m_chunks.back().lineCount) > m_pendingJumpLine || m_loadDone);
    if (!covered) return;
    const int line = m_pendingJumpLine;
    const bool hl = m_pendingJumpHighlight;
    m_pendingJumpLine = -1;
    scrollToSourceLine(line, hl);
}

void ViewerWidget::scrollToSourceLine(int line, bool highlight)
{
    if (m_chunks.empty()) {
        m_pendingJumpLine = line;
        m_pendingJumpHighlight = highlight;
        return;
    }
    const int lastLine = static_cast<int>(m_chunks.back().lineStart + m_chunks.back().lineCount);
    if (line >= lastLine && !m_loadDone) {
        m_pendingJumpLine = line;
        m_pendingJumpHighlight = highlight;
        return;
    }
    m_wheelRemaining = 0;
    ensureMetrics();
    m_model.setViewportHeight(viewport()->height());
    const int idx = std::max(0, chunkForLine(line));
    // 精确偏移：目标 chunk 布局后，取包含该行的顶层块的 y
    CacheEntry& e = ensureLayout(static_cast<size_t>(idx));
    double offset = 0;
    for (const auto& b : e.layout.root.children) {
        if (b.sourceStartLine <= line && line <= b.sourceEndLine) {
            offset = std::max(0.0, b.bounds.y() - 6.0);
            break;
        }
        if (b.sourceStartLine > line) break;
        offset = std::max(0.0, b.bounds.y() - 6.0);   // 落在两块之间则取前一块
    }
    m_model.jumpTo(static_cast<size_t>(idx), offset);
    if (highlight) {
        m_targetLine = line;
        m_highlightAnim->stop();
        m_highlightAnim->start();
    }
    prepare();
    m_positionTimer.start();
}

int ViewerWidget::topSourceLine() const
{
    if (m_chunks.empty()) return 0;
    const size_t idx = std::min(m_model.anchorChunk(), m_chunks.size() - 1);
    const Chunk& c = m_chunks[idx];
    const double h = std::max(1.0, m_model.chunkHeight(idx));
    const double frac = std::min(1.0, std::max(0.0, m_model.anchorOffset() / h));
    return static_cast<int>(c.lineStart) + static_cast<int>(frac * c.lineCount);
}

// ---------------------------------------------------------------- 绘制

const QVector<TextSegment>& ViewerWidget::segmentsForTest() const
{
    return m_segments;
}

void ViewerWidget::paintEvent(QPaintEvent*)
{
    SM_PERF_SCOPE("viewer.paintEvent");
    QScopedValueRollback<bool> painting(m_inPaint, true);
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), m_theme.previewBg);

    if (!m_error.isEmpty()) {
        p.setPen(m_theme.previewFg);
        p.drawText(viewport()->rect().adjusted(40, 40, -40, -40),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_error);
        return;
    }
    if (m_visible.empty()) {
        if (m_loading) {
            p.setPen(m_theme.previewFg);
            p.drawText(viewport()->rect(), Qt::AlignCenter, tr("Loading…"));
        }
        return;
    }

    m_painter->beginFrame();
    p.translate(kPadX, 0);
    m_painter->setTargetLineHighlight(m_targetLine, m_targetOpacity);
    m_painter->setHighlights({});
    m_painter->setSearchHighlights({});
    m_painter->setCurrentSearchHit(-1);

    SelPos lo = m_selA, hi = m_selB;
    const bool hasSel = lo.valid() && hi.valid() && !(lo == hi);
    if (hi < lo) std::swap(lo, hi);

    const qreal cw = m_layoutWidth > 0 ? m_layoutWidth : contentWidth();
    const qreal vh = viewport()->height();
    for (const auto& v : m_visible) {
        auto it = m_cache.find(v.index);
        if (it == m_cache.end()) continue;   // 缺失只会发生在极端淘汰，下一次 prepare 补齐
        int s = -1, e = -1;
        if (hasSel && static_cast<int>(v.index) >= lo.chunk && static_cast<int>(v.index) <= hi.chunk) {
            s = (static_cast<int>(v.index) == lo.chunk) ? lo.ch : 0;
            e = (static_cast<int>(v.index) == hi.chunk) ? hi.ch : kWholeChunk;
        }
        m_painter->setSelection(s, e);
        if (!m_searchText.isEmpty() && !m_searchHits.empty()) {
            // 当前 chunk 的渲染文本命中（按需计算并缓存；只对可视 chunk 计算）
            QVector<QPair<int, int>> ranges;
            if (const auto* rh = renderedHitsCached(v.index)) {
                ranges.reserve(static_cast<int>(rh->size()));
                for (const auto& r : *rh) ranges.append(qMakePair(r.first, r.second));
            }
            m_painter->setSearchHighlights(ranges);
            m_painter->setCurrentSearchHit(static_cast<int>(v.index) == m_searchCurChunk ? m_searchCurInChunk : -1);
        } else {
            m_painter->setSearchHighlights({});
            m_painter->setCurrentSearchHit(-1);
        }
        m_painter->paintChunk(&p, it->second.layout.root, static_cast<int>(v.index), v.screenY, vh, cw);
    }
    m_segments = m_painter->textSegments();
    if (!m_firstPaintMarked) {
        m_firstPaintMarked = true;
        core::startupMark("viewer first content painted");
    }
}

void ViewerWidget::resizeEvent(QResizeEvent* e)
{
    QAbstractScrollArea::resizeEvent(e);
    if (m_searchBar && m_searchBar->isVisible())
        m_searchBar->move(width() - m_searchBar->width() - 20, 10);
    if (e->oldSize().isValid() && e->oldSize() == e->size()) return;
    if (m_layoutWidth < 0) {
        prepare();   // 首次：立即布局，不去抖
        return;
    }
    if (e->size().width() != e->oldSize().width()) {
        m_resizeTimer.start();     // 宽度变化 → 去抖后整体重排
    } else {
        prepare();                 // 仅高度变化：可视范围变化，不必重排
    }
}

void ViewerWidget::changeEvent(QEvent* e)
{
    QAbstractScrollArea::changeEvent(e);
    if (e->type() == QEvent::ScreenChangeInternal || e->type() == QEvent::PaletteChange)
        m_lastDpr = 0;
}

// ---------------------------------------------------------------- 选区 / 命中

ViewerWidget::SelPos ViewerWidget::hitTest(const QPointF& vp, bool strict, bool* hitSegment) const
{
    SelPos none;
    if (hitSegment) *hitSegment = false;
    if (m_visible.empty()) return none;
    const QPointF pt(vp.x() - kPadX, vp.y());
    // 先确定鼠标所在 chunk（越界时取最近的可视 chunk）
    const ScrollModel::VisibleChunk* target = &m_visible.front();
    for (const auto& v : m_visible) {
        if (pt.y() >= v.screenY) target = &v;
        if (pt.y() < v.screenY + v.height) break;
    }
    QVector<TextSegment> mine;
    for (const auto& s : m_segments)
        if (s.chunk == static_cast<int>(target->index)) mine.append(s);
    if (mine.isEmpty()) return none;
    for (const auto& s : mine) {
        if (s.rect.contains(pt)) { if (hitSegment) *hitSegment = true; break; }
    }
    if (strict && hitSegment && !*hitSegment) return none;
    SelPos r;
    r.chunk = static_cast<int>(target->index);
    r.ch = TextHitTest::textIndexForSegments(mine, pt, viewport());
    return r;
}

bool ViewerWidget::hasSelection() const
{
    return m_selA.valid() && m_selB.valid() && !(m_selA == m_selB);
}

void ViewerWidget::clearSelection()
{
    m_selA = m_selB = SelPos();
    viewport()->update();
}

void ViewerWidget::selectAll()
{
    if (m_chunks.empty()) return;
    m_selA = SelPos{0, 0};
    m_selB = SelPos{static_cast<int>(m_chunks.size()) - 1, kWholeChunk};
    viewport()->update();
}

QString ViewerWidget::chunkText(size_t i)
{
    QString out;
    PreviewPainter::extractBlockText(ensureLayout(i).layout.root, out);
    return out;
}

QString ViewerWidget::chunkTextNoCache(size_t i)
{
    // 与 ensureLayout 同样的解析+布局，但结果用完即弃：不占 m_cache、不回写滚动模型
    if (i >= m_chunks.size() || !m_buffer) return QString();
    auto it = m_cache.find(i);
    QString out;
    if (it != m_cache.end()) {
        PreviewPainter::extractBlockText(it->second.layout.root, out);
        return out;
    }
    const Chunk& c = m_chunks[i];
    const AssembledChunk a = ChunkScanner::assembleChunk(m_buffer->constData(), c, m_prologues, m_refDefs);
    AstNodePtr root = m_parser.parseChunk(a.text, static_cast<int>(c.lineStart) - a.lineBias, i == 0);
    const auto res = m_layout->layoutChunk(*root, i == 0);
    PreviewPainter::extractBlockText(res.root, out);
    return out;
}

QString ViewerWidget::selectedText()
{
    if (!hasSelection()) return QString();
    SelPos lo = m_selA, hi = m_selB;
    if (hi < lo) std::swap(lo, hi);
    QString out;
    const bool big = (hi.chunk - lo.chunk) > 200;
    if (big) QApplication::setOverrideCursor(Qt::WaitCursor);
    ensureMetrics();
    QElapsedTimer pump;
    pump.start();
    for (int c = lo.chunk; c <= hi.chunk && c < static_cast<int>(m_chunks.size()); ++c) {
        // 超大范围（全选 50MB 文档）会持续较久：定期处理非输入事件，避免界面「未响应」
        if (big && pump.elapsed() > 100) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            pump.restart();
        }
        const QString t = chunkTextNoCache(static_cast<size_t>(c));
        const int s = (c == lo.chunk) ? std::min(lo.ch, static_cast<int>(t.size())) : 0;
        const int e = (c == hi.chunk) ? std::min(hi.ch, static_cast<int>(t.size())) : static_cast<int>(t.size());
        if (e > s) out += t.mid(s, e - s);
    }
    if (big) QApplication::restoreOverrideCursor();
    // 复制结果不带最后一个块分隔换行
    while (out.endsWith('\n')) out.chop(1);
    return out;
}

void ViewerWidget::copySelection()
{
    const QString t = selectedText();
    if (!t.isEmpty()) QApplication::clipboard()->setText(t);
}

void ViewerWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        setFocus();
        m_wheelRemaining = 0;
        const QPointF pt(e->pos());
        if (e->modifiers() & Qt::ControlModifier) {
            for (const auto& seg : m_segments) {
                if (seg.linkUrl.isEmpty()) continue;
                if (seg.rect.contains(QPointF(pt.x() - kPadX, pt.y()))) {
                    emit linkClicked(seg.linkUrl);
                    return;
                }
            }
        }
        m_selecting = true;
        m_selA = m_selB = hitTest(pt, false);
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(e);
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* e)
{
    m_lastMousePos = e->pos();
    if (m_selecting && (e->buttons() & Qt::LeftButton)) {
        const int y = e->pos().y();
        const int vh = viewport()->height();
        if (y < 0 || y > vh) {
            if (!m_autoScrollTimer.isActive()) m_autoScrollTimer.start();
        } else {
            m_autoScrollTimer.stop();
        }
        const SelPos p = hitTest(QPointF(e->pos()), false);
        if (p.valid()) m_selB = p;
        viewport()->update();
        return;
    }
    // 悬停链接：手型光标 + 目标地址提示
    QString hover;
    const QPointF pt(e->pos().x() - kPadX, e->pos().y());
    for (const auto& seg : m_segments) {
        if (!seg.linkUrl.isEmpty() && seg.rect.contains(pt)) { hover = seg.linkUrl; break; }
    }
    viewport()->setCursor(hover.isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
    if (hover != m_hoverLink) {
        m_hoverLink = hover;
        if (!hover.isEmpty()) QToolTip::showText(e->globalPos(), hover, viewport());
        else QToolTip::hideText();
    }
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_selecting = false;
        m_autoScrollTimer.stop();
    }
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void ViewerWidget::autoScrollTick()
{
    if (!m_selecting) { m_autoScrollTimer.stop(); return; }
    const int y = m_lastMousePos.y();
    const int vh = viewport()->height();
    double dy = 0;
    if (y < 0) dy = y / 2.0 - 4;
    else if (y > vh) dy = (y - vh) / 2.0 + 4;
    if (dy == 0) { m_autoScrollTimer.stop(); return; }
    m_model.scrollBy(dy);
    prepare();
    // 重绘产生新 segments 后再命中，保证选区终点跟随滚动
    viewport()->repaint();
    const SelPos p = hitTest(QPointF(m_lastMousePos), false);
    if (p.valid()) m_selB = p;
    viewport()->update();
}

void ViewerWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QAbstractScrollArea::mouseDoubleClickEvent(e); return; }
    bool hit = false;
    const SelPos p = hitTest(QPointF(e->pos()), true, &hit);
    if (!hit || !p.valid()) return;
    const QString text = chunkText(static_cast<size_t>(p.chunk));
    const QPair<int, int> r = TextHitTest::findWordBoundaryFor(text, p.ch);
    if (r.first < 0 || r.second <= r.first) return;
    m_selA = SelPos{p.chunk, r.first};
    m_selB = SelPos{p.chunk, r.second};
    m_selecting = false;
    viewport()->update();
}

void ViewerWidget::keyPressEvent(QKeyEvent* e)
{
    const double page = std::max(40.0, viewport()->height() * 0.9);
    const double line = lineHeightPx();
    auto go = [this](double dy) { m_wheelRemaining = 0; m_model.scrollBy(dy); prepare(); m_positionTimer.start(); };
    if (e->key() == Qt::Key_Escape && isSearchBarVisible()) { hideSearchBar(); return; }
    if (e->matches(QKeySequence::Copy)) { copySelection(); return; }
    if (e->matches(QKeySequence::Find)) { showSearchBar(); return; }
    if (e->matches(QKeySequence::SelectAll)) { selectAll(); return; }
    switch (e->key()) {
    case Qt::Key_Down:     go(line * 2); return;
    case Qt::Key_Up:       go(-line * 2); return;
    case Qt::Key_PageDown: go(page); return;
    case Qt::Key_PageUp:   go(-page); return;
    case Qt::Key_Space:    go((e->modifiers() & Qt::ShiftModifier) ? -page : page); return;
    case Qt::Key_Home:     scrollToTop(); return;
    case Qt::Key_End:      scrollToBottom(); return;
    default: break;
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void ViewerWidget::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    QAction* copy = menu.addAction(tr("Copy"));
    copy->setEnabled(hasSelection());
    connect(copy, &QAction::triggered, this, &ViewerWidget::copySelection);
    QAction* all = menu.addAction(tr("Select All"));
    connect(all, &QAction::triggered, this, &ViewerWidget::selectAll);
    menu.exec(e->globalPos());
}

// ---------------------------------------------------------------- 查找

bool ViewerWidget::searchCaseSensitive() const
{
    if (m_searchCaseOverride) return m_searchCaseForTest;
    return m_searchBar && m_searchBar->isCaseSensitive();
}

QString ViewerWidget::allChunkTextForTest()
{
    ensureMetrics();
    QString all;
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        all += chunkText(i);
        if (m_cache.size() > 64) m_cache.clear();   // 控制内存；测试只关心文本
    }
    return all;
}

void ViewerWidget::searchForTest(const QString& text, bool caseSensitive)
{
    m_searchCaseOverride = true;
    m_searchCaseForTest = caseSensitive;
    m_searchText = text;
    startSearch();
    // 同步等待后台线程完成（最多 10s）
    QElapsedTimer t;
    t.start();
    while (m_searchTask && t.elapsed() < 10000) QApplication::processEvents(QEventLoop::AllEvents, 10);
}

void ViewerWidget::showSearchBar()
{
    if (!m_searchBar) {
        m_searchBar = new SearchBar(this);
        m_searchBar->setTheme(m_theme);
        m_searchBar->hide();
        connect(m_searchBar, &SearchBar::searchTextChanged, this, &ViewerWidget::onSearchTextChanged);
        connect(m_searchBar, &SearchBar::findNext, this, [this](const QString&) {
            if (m_searchHits.empty()) return;
            gotoSearchHit(m_searchCur + 1, +1);
        });
        connect(m_searchBar, &SearchBar::findPrev, this, [this](const QString&) {
            if (m_searchHits.empty()) return;
            gotoSearchHit(m_searchCur - 1, -1);
        });
        connect(m_searchBar, &SearchBar::closed, this, [this]() {
            clearSearchState();
            viewport()->update();
            setFocus();
        });
    }
    m_searchBar->showSearch();
    m_searchBar->move(width() - m_searchBar->width() - 20, 10);
    m_searchBar->raise();
}

void ViewerWidget::hideSearchBar()
{
    if (m_searchBar && m_searchBar->isVisible()) m_searchBar->hideBar();
}

bool ViewerWidget::isSearchBarVisible() const
{
    return m_searchBar && m_searchBar->isVisible();
}

void ViewerWidget::clearSearchState()
{
    delete m_searchTask;
    m_searchTask = nullptr;
    m_searchText.clear();
    m_searchHits.clear();
    m_searchRendered.clear();
    m_searchCur = -1;
    m_searchCurChunk = -1;
    m_searchCurInChunk = -1;
}

void ViewerWidget::onSearchTextChanged(const QString& text)
{
    m_searchText = text;
    m_searchDebounce.start();
}

void ViewerWidget::startSearch()
{
    delete m_searchTask;
    m_searchTask = nullptr;
    m_searchHits.clear();
    m_searchRendered.clear();
    m_searchCur = -1;
    m_searchCurChunk = -1;
    m_searchCurInChunk = -1;
    if (m_searchText.isEmpty() || !m_buffer) {
        if (m_searchBar) m_searchBar->updateMatchInfo(0, 0);
        viewport()->update();
        return;
    }
    // 源文本（UTF-8）上的候选搜索；水位线以下才是已写完的数据
    m_searchTask = new BufferSearchTask(m_buffer, m_watermark, m_searchText.toUtf8(), searchCaseSensitive(), this);
    connect(m_searchTask, &BufferSearchTask::finished, this, &ViewerWidget::onSearchFinished);
    m_searchTask->start();
}

void ViewerWidget::onSearchFinished()
{
    if (!m_searchTask) return;
    m_searchHits = m_searchTask->takeHits();
    m_searchTask->deleteLater();
    m_searchTask = nullptr;
    // 从当前视口顶部之后的第一个命中开始
    const uint64_t topByte = m_chunks.empty() ? 0 : m_chunks[std::min(m_model.anchorChunk(), m_chunks.size() - 1)].byteStart;
    int start = 0;
    while (start < static_cast<int>(m_searchHits.size()) && m_searchHits[static_cast<size_t>(start)] < topByte) ++start;
    if (start >= static_cast<int>(m_searchHits.size())) start = 0;
    if (m_searchBar) m_searchBar->updateMatchInfo(m_searchHits.empty() ? -1 : 0, static_cast<int>(m_searchHits.size()));
    if (m_searchHits.empty()) { viewport()->update(); return; }
    gotoSearchHit(start, +1);
    if (m_searchBar) m_searchBar->keepFocus();
}

const std::vector<std::pair<int, int>>* ViewerWidget::renderedHitsCached(size_t chunk) const
{
    auto it = m_searchRendered.find(chunk);
    return it == m_searchRendered.end() ? nullptr : &it->second;
}

const std::vector<std::pair<int, int>>& ViewerWidget::renderedHitsFor(size_t chunk)
{
    auto it = m_searchRendered.find(chunk);
    if (it != m_searchRendered.end()) return it->second;
    std::vector<std::pair<int, int>> hits;
    const QString text = chunkText(chunk);
    const Qt::CaseSensitivity cs = searchCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int from = 0;
    while (!m_searchText.isEmpty()) {
        const int pos = text.indexOf(m_searchText, from, cs);
        if (pos < 0) break;
        hits.emplace_back(pos, m_searchText.size());
        from = pos + std::max(1, m_searchText.size());
    }
    return m_searchRendered.emplace(chunk, std::move(hits)).first->second;
}

// hitIndex：候选序号（越界回绕）；direction：+1 向后找下一个、-1 向前找上一个（跳过渲染文本中无命中的候选）
void ViewerWidget::gotoSearchHit(int hitIndex, int direction)
{
    const int total = static_cast<int>(m_searchHits.size());
    if (total == 0 || m_chunks.empty()) return;
    for (int tries = 0; tries < std::min(total, 200); ++tries) {
        int idx = ((hitIndex % total) + total) % total;
        const uint64_t off = m_searchHits[static_cast<size_t>(idx)];
        // 命中所在 chunk（byteStart 升序二分）
        size_t lo = 0, hi = m_chunks.size();
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (m_chunks[mid].byteStart <= off) lo = mid + 1; else hi = mid;
        }
        const size_t chunk = lo == 0 ? 0 : lo - 1;
        // 该候选是本 chunk 内第几个候选
        int k = 0;
        for (int j = idx - 1; j >= 0 && m_searchHits[static_cast<size_t>(j)] >= m_chunks[chunk].byteStart; --j) ++k;
        const auto& rendered = renderedHitsFor(chunk);
        if (!rendered.empty()) {
            const int inChunk = std::min(k, static_cast<int>(rendered.size()) - 1);
            m_searchCur = idx;
            m_searchCurChunk = static_cast<int>(chunk);
            m_searchCurInChunk = inChunk;
            // 定位：渲染命中所在块的 Y
            ensureMetrics();
            m_model.setViewportHeight(viewport()->height());
            CacheEntry& e = ensureLayout(chunk);
            qreal y = 0;
            int counter = 0;
            if (!PreviewPainter::locateCharY(e.layout.root, rendered[static_cast<size_t>(inChunk)].first, counter, 0.0, y))
                y = 0;
            // 命中块放在视口上 1/4 处
            const double offset = std::max(0.0, static_cast<double>(y) - viewport()->height() * 0.25);
            m_wheelRemaining = 0;
            m_model.jumpTo(chunk, offset);
            prepare();
            m_positionTimer.start();
            if (m_searchBar) m_searchBar->updateMatchInfo(idx, total);
            return;
        }
        hitIndex = idx + direction;   // 渲染文本无此命中（仅出现在标记/URL 中），跳过
    }
    if (m_searchBar) m_searchBar->updateMatchInfo(-1, total);
}

}  // namespace viewer

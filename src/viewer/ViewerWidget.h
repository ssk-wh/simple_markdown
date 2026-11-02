// src/viewer/ViewerWidget.h
//
// Spec: specs/模块-viewer/04-查看器控件.md
// Invariants enforced here: INV-VIEW-NO-LAYOUT-IN-PAINT, INV-VIEW-ANCHOR, INV-SEL-CHUNK-LOCAL
// Last synced: 2026-11-02
#pragma once

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ChunkScanner.h"
#include "DocumentLoader.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "ScrollModel.h"
#include "Theme.h"
#include "TocPanel.h"

class ImageCache;
class PreviewPainter;
class SearchBar;

namespace viewer {

// 只读 Markdown 查看器：分块（chunk）+ 虚拟化滚动 + 异步加载。
// 与旧 PreviewWidget 的区别：从不对整篇文档建立 AST/布局，只处理可视 chunk。
class ViewerWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit ViewerWidget(QWidget* parent = nullptr);
    ~ViewerWidget() override;

    // ---- 加载 ----
    void loadFile(const QString& path);
    // 重新加载并尽量保持阅读位置（外部修改后静默刷新）
    void reload();
    QString filePath() const { return m_path; }
    bool isLoading() const { return m_loading; }
    QString errorString() const { return m_error; }
    // 读取全文（解码 UTF-8）——仅供统计/导出/打印使用，大文件上会较慢
    QString readAllText() const;
    uint64_t sourceBytes() const { return m_buffer ? static_cast<uint64_t>(m_buffer->size()) : 0; }
    uint32_t lineCount() const { return m_lineCount; }
    size_t chunkCount() const { return m_chunks.size(); }

    // ---- 外观 ----
    PreviewLayout* previewLayout() const { return m_layout; }
    void setTheme(const Theme& theme);
    void setDocumentDir(const QString& dir);
    void setLineSpacingFactor(qreal factor);
    // 字体/行距等布局参数变化后调用：失效全部布局并重绘，保持阅读位置
    void rebuildLayout();

    // ---- 导航 ----
    const QVector<TocEntry>& tocEntries() const { return m_tocEntries; }
    void scrollToSourceLine(int line, bool highlight = true);
    void smoothScrollToSourceLine(int line) { scrollToSourceLine(line, true); }
    // 当前视口顶部对应的源文件 0 基行号（会话恢复/记住阅读位置用）
    int topSourceLine() const;
    void scrollToTop();
    void scrollToBottom();

    // ---- 选区与复制 ----
    bool hasSelection() const;
    void selectAll();
    void clearSelection();
    void copySelection();
    QString selectedText();

    // ---- 查找（Spec 模块-viewer/05）----
    void showSearchBar();
    void hideSearchBar();
    bool isSearchBarVisible() const;

    // ---- 诊断/测试 ----
    size_t cachedChunkCount() const { return m_cache.size(); }
    const ScrollModel& scrollModel() const { return m_model; }
    qint64 firstContentMs() const { return m_firstContentMs; }
    // 同步处理加载批次（测试用：不依赖事件循环投递）
    void pumpForTest();
    // 强制同步完成一次 prepare（测试用）
    void prepareForTest() { prepare(); }
    const QVector<TextSegment>& segmentsForTest() const;

signals:
    void tocEntriesChanged(const QVector<TocEntry>& entries);
    void loadStateChanged();          // 开始/完成/失败
    void firstContentReady();         // 首屏内容已可绘制
    void linkClicked(const QString& url);
    void positionChanged();           // 滚动位置变化（调用方自行防抖保存）

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void changeEvent(QEvent* e) override;

private slots:
    void onBatches();
    void onScrollBarValue(int v);
    void onImageReady(const QString& url);

private:
    struct CacheEntry {
        PreviewLayout::ChunkLayoutResult layout;
        quint64 lastUse = 0;
    };
    struct SelPos {
        int chunk = -1;
        int ch = 0;
        bool valid() const { return chunk >= 0; }
        bool operator<(const SelPos& o) const { return chunk != o.chunk ? chunk < o.chunk : ch < o.ch; }
        bool operator==(const SelPos& o) const { return chunk == o.chunk && ch == o.ch; }
    };

    // 解析 + 布局一个 chunk（主线程，毫秒级）
    CacheEntry& ensureLayout(size_t chunkIndex);
    double measureChunk(size_t chunkIndex);
    // 把滚动模型的状态同步到可视列表、滚动条，并请求重绘。绝不在 paintEvent 内调用。
    void prepare();
    void syncScrollBar();
    void evictCache();
    void invalidateAllLayouts();
    void ensureMetrics();
    void schedulePrefetch();
    void prefetch();
    void applyPendingJump();
    void flushTocSoon();
    int chunkForLine(int line) const;
    qreal contentWidth() const;
    qreal lineHeightPx() const;
    SelPos hitTest(const QPointF& vp, bool strict, bool* hitSegment = nullptr) const;
    QString chunkText(size_t chunkIndex);
    void startWheelAnimation(double dy);
    void tickWheel();
    void autoScrollTick();

    // 数据
    DocumentLoader* m_loader = nullptr;
    std::shared_ptr<QByteArray> m_buffer;
    uint64_t m_watermark = 0;
    std::vector<Chunk> m_chunks;
    std::vector<std::string> m_prologues;
    QByteArray m_refDefs;
    std::vector<HeadingEntry> m_headings;
    QVector<TocEntry> m_tocEntries;
    uint32_t m_lineCount = 0;
    QString m_path;
    QString m_error;
    bool m_loading = false;
    bool m_loadDone = false;

    // 渲染
    MarkdownParser m_parser;
    PreviewLayout* m_layout = nullptr;
    PreviewPainter* m_painter = nullptr;
    ImageCache* m_imageCache = nullptr;
    Theme m_theme;
    ScrollModel m_model;
    std::vector<ScrollModel::VisibleChunk> m_visible;
    std::unordered_map<size_t, CacheEntry> m_cache;
    quint64 m_useTick = 0;
    bool m_inPrepare = false;
    bool m_inPaint = false;
    bool m_syncing = false;
    qreal m_layoutWidth = -1;
    qreal m_lastDpr = 0;
    QTimer m_resizeTimer;
    QTimer m_prefetchTimer;
    QTimer m_tocTimer;
    QTimer m_positionTimer;
    QElapsedTimer m_loadTimer;
    qint64 m_firstContentMs = -1;
    bool m_firstContentEmitted = false;
    QVector<TextSegment> m_segments;   // 上一帧绘制产生的 segments（命中测试用）

    // 跳转
    int m_pendingJumpLine = -1;
    bool m_pendingJumpHighlight = false;
    int m_targetLine = -1;
    qreal m_targetOpacity = 0.0;
    QVariantAnimation* m_highlightAnim = nullptr;

    // 平滑滚轮
    QTimer m_wheelTimer;
    double m_wheelRemaining = 0;

    // 选区
    SelPos m_selA;
    SelPos m_selB;
    bool m_selecting = false;
    QTimer m_autoScrollTimer;
    QPoint m_lastMousePos;
    QString m_hoverLink;

    // 查找
    SearchBar* m_searchBar = nullptr;
};

}  // namespace viewer

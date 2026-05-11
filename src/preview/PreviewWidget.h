#pragma once

#include <QAbstractScrollArea>
#include <QThread>
#include <QTimer>
#include <memory>
#include <QPair>
#include <QVector>
#include <QSet>
#include "Theme.h"

class AstNode;
class PreviewLayout;
class PreviewPainter;
class ImageCache;
struct TocEntry;
class QPropertyAnimation;
class SearchBar;
class SearchWorker;

class PreviewWidget : public QAbstractScrollArea {
    Q_OBJECT
    Q_PROPERTY(qreal highlightOpacity READ highlightOpacity WRITE setHighlightOpacity)
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    PreviewLayout* previewLayout() const;
    void scrollToSourceLine(int line);
    void smoothScrollToSourceLine(int line);
    void setTheme(const Theme& theme);
    void setDocumentDir(const QString& dir);
    void setWordWrap(bool enabled);
    bool wordWrap() const { return m_wordWrap; }
    // [Spec 模块-preview/02 INV-13] 正文行高乘数转发，仅作用于段落 / List / Table 行高
    void setLineSpacingFactor(qreal factor);
    void rebuildLayout();
    void clearHighlightsInSection(int sectionIdx);

    qreal highlightOpacity() const { return m_highlightOpacity; }
    void setHighlightOpacity(qreal opacity) { m_highlightOpacity = opacity; }

    const QVector<TocEntry>& tocEntries() const { return m_tocEntries; }
    const QSet<int>& tocHighlightedIndices() const { return m_tocHighlighted; }

    // [Spec 模块-preview/08-内容标记 INV-5、T-4、§4 接口]
    // 标记的会话级持久化序列化/反序列化。
    // 反序列化采用"延迟应用"策略：先缓存到 m_pendingMarkings，
    // 在最近一次 updateAst() 之后由 applyPendingMarkings() 兑现，
    // 以跨过 updateAst 中的 m_highlights.clear() 动作。
    // 越界（start<0 或 end<=start 或 start 超出当前文档长度）的标记静默丢弃，
    // 对应 Spec §8.3「字符偏移漂移」的尽力而为策略。
    QByteArray serializeMarkings() const;
    void deserializeMarkings(const QByteArray& data);

    // [Spec 模块-preview/11-预览区查找] 弹出查找栏并聚焦输入框（仅查找，不显示替换行）
    void showSearchBar();
    // [Spec 模块-preview/11 INV-9] 互斥：MainWindow 路由切换到编辑器搜索时调用，
    // 关闭预览侧搜索栏并清空搜索高亮
    void hideSearchBar();
    // 查询当前是否有搜索栏可见（供 MainWindow 决定 Ctrl+F 路由）
    bool isSearchBarVisible() const;

public slots:
    void updateAst(std::shared_ptr<AstNode> root);
    void refreshPreview();

signals:
    void tocEntriesChanged(const QVector<TocEntry>& entries);
    void tocHighlightChanged(const QSet<int>& indices);
    void openInBrowserRequested();
    // [Spec 模块-preview/09-链接点击与导航] Ctrl+click 链接
    void linkClicked(const QString& url);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updateScrollBars();
    QString extractPlainText() const;
    void extractBlockText(const struct LayoutBlock& block, QString& out) const;
    int textIndexAtPoint(const QPointF& point) const;
    void copySelection();
    void copyAsHtml();
    void openInBrowser();
    void addHighlight();
    void clearHighlights();
    void updateTocHighlights();
    // [Spec 模块-preview/08 INV-5] 把 m_pendingMarkings 中暂存的标记字节流应用到 m_highlights。
    // 在 deserializeMarkings 调用时和每次 updateAst 末尾被调用，一次性消耗。
    void applyPendingMarkings();
    void buildHeadingCharOffsets();
    void updateTocEntries();
    void onScrollAnimationValueChanged(const QVariant &value);
    void onScrollAnimationFinished();

    Theme m_theme;
    PreviewLayout* m_layout = nullptr;
    PreviewPainter* m_painter = nullptr;
    ImageCache* m_imageCache = nullptr;
    std::shared_ptr<AstNode> m_currentAst;
    bool m_wordWrap = true;

    // 文本选区
    QString m_plainText;
    int m_selStart = -1;
    int m_selEnd = -1;
    bool m_selecting = false;
    qreal m_lastDevicePixelRatio = 0;

    // TOC 数据（发信号给外部 TocPanel）
    QVector<TocEntry> m_tocEntries;
    QSet<int> m_tocHighlighted;

    // 标记高亮
    QVector<QPair<int,int>> m_highlights;
    QVector<int> m_headingCharOffsets;
    // [Spec 模块-preview/08 INV-5] 待应用的标记字节流（来自会话恢复），
    // 在 updateAst 后由 applyPendingMarkings() 一次性消耗。
    QByteArray m_pendingMarkings;

    // TOC 跳转动画
    QPropertyAnimation* m_scrollAnimation = nullptr;
    int m_targetSourceLine = -1;
    qreal m_highlightOpacity = 0.0;

    // [Spec 模块-preview/11-预览区查找]
    SearchBar* m_searchBar = nullptr;
    QThread m_searchThread;
    SearchWorker* m_searchWorker = nullptr;
    QTimer m_searchDebounce;
    int m_searchRequestId = 0;
    QVector<QPair<int,int>> m_searchHits;  // (offset, length)
    int m_currentSearchIndex = -1;
    QString m_currentSearchText;

    void onSearchTextChanged(const QString& text);
    void onSearchResultsReady(QVector<QPair<int,int>> matches, int requestId);
    void findNextHit(const QString& text);
    void findPrevHit(const QString& text);
    void scrollToCharOffset(int offset);

    // [Spec 模块-preview/11 INV-11] 输入后立即回车的同步搜索兜底——避免依赖
    // 100ms debounce + 异步 worker；同步重搜后让异步路径 requestId 自然过期
    void syncRecomputeSearchHits(const QString& text);

    // [Spec 模块-preview/11 INV-12] 位置语义跳转——基于当前视口顶部 scrollY 选下一个/
    // 上一个命中：forward=true 找第一个 hit.y > scrollY；都不满足回绕到首项 / 末项。
    // 返回值为 m_searchHits 内的索引，调用方负责更新 m_currentSearchIndex 并滚动。
    int pickHitIndexByScroll(bool forward) const;
};

#pragma once

#include <QAbstractScrollArea>
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

    // TOC 跳转动画
    QPropertyAnimation* m_scrollAnimation = nullptr;
    int m_targetSourceLine = -1;
    qreal m_highlightOpacity = 0.0;
};

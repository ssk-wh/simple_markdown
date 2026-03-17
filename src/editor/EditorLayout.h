#pragma once
#include <QObject>
#include <QFont>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QPointF>
#include <QRectF>
#include <vector>
#include <memory>
#include "Selection.h"
#include "SyntaxHighlighter.h"

class Document;

class EditorLayout : public QObject {
    Q_OBJECT
public:
    explicit EditorLayout(QObject* parent = nullptr);
    ~EditorLayout();

    // 配置
    void setFont(const QFont& font);
    QFont font() const;
    void setTabStopWidth(int spaces);
    void setWrapWidth(qreal width);  // 0 = 不折行

    // 关联文档
    void setDocument(Document* doc);

    // 全量重建
    void rebuild();

    // 增量更新：[startLine, endLine] 范围的行重算布局
    void updateLines(int startLine, int endLine);

    // 坐标转换
    TextPosition hitTest(const QPointF& point) const;
    QPointF positionToPoint(TextPosition pos) const;
    QRectF cursorRect(TextPosition pos) const;

    // 行几何查询
    qreal lineY(int line) const;
    qreal lineHeight(int line) const;
    qreal totalHeight() const;
    int lineAtY(qreal y) const;
    int lineCount() const;

    // 获取行布局（供 Painter 绘制用）
    QTextLayout* layoutForLine(int line) const;

    // 默认行高（未创建 QTextLayout 的行使用这个估计值）
    qreal defaultLineHeight() const;

private:
    struct LineInfo {
        mutable std::unique_ptr<QTextLayout> layout;
        mutable qreal height = 0;
        mutable bool dirty = true;
    };

    Document* m_doc = nullptr;
    QFont m_font;
    QFontMetricsF m_fontMetrics;
    qreal m_wrapWidth = 0;
    int m_tabStopSpaces = 4;
    qreal m_defaultLineHeight = 0;
    qreal m_charWidth = 0;

    std::vector<LineInfo> m_lines;
    mutable std::vector<qreal> m_lineYCache;
    mutable bool m_yCacheDirty = true;

    void ensureLayout(int line) const;
    void ensureYCache() const;
    void invalidateYCache();

    mutable SyntaxHighlighter m_highlighter;
};

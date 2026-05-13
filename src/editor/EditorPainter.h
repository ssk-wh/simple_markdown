#pragma once
#include <QtGlobal>
#include <QString>
#include <QVector>
#include <QPair>
#include "Selection.h"
#include "Theme.h"

class QPainter;
class EditorLayout;
class Document;

class EditorPainter {
public:
    EditorPainter();

    void setTheme(const Theme& theme);
    const Theme& theme() const { return m_theme; }
    void setSelectionColor(const QColor& color);

    // [plan A8 2026-05-13] 单行绘制 API：在调用方提供的 cursorY 处画文本区装饰 + 文本。
    // 调用方（EditorWidget::paintEvent）按本地 cy 推进，行号和文本共享同一 cy → 必然对齐。
    // 包含：当前行高亮（if line==cursorPos.line && !hasSelection）、搜索匹配高亮、文本（含选区）
    void paintLine(QPainter* painter, EditorLayout* layout, Document* doc,
                   int line, qreal cy, qreal lineH,
                   int gutterWidth, qreal viewWidth, qreal scrollX,
                   TextPosition cursorPos, bool hasSelection,
                   TextPosition selStartPos, TextPosition selEndPos,
                   const QVector<QPair<int,int>>& searchMatches,
                   int currentMatchIndex);

    // [plan A8] 光标绘制（基于 cursorRect，与 per-line loop 独立）
    void paintCursor(QPainter* painter, EditorLayout* layout,
                     TextPosition cursorPos, qreal scrollY, int gutterWidth, qreal scrollX);

    // [plan A8] IME 预编辑文本（基于 cursorRect）
    void paintIme(QPainter* painter, EditorLayout* layout,
                  TextPosition cursorPos, qreal scrollY, int gutterWidth, qreal scrollX,
                  const QString& preeditString);

    // 旧 API 保留作 backward-compat（暂未删除，避免影响潜在调用方）
    void paint(QPainter* painter, EditorLayout* layout, Document* doc,
               int firstLine, int lastLine,
               int gutterWidth, qreal scrollY, qreal scrollX = 0,
               bool cursorVisible = false,
               TextPosition cursorPos = {0, 0},
               const QString& preeditString = QString(),
               const QVector<QPair<int,int>>& searchMatches = {},
               int currentMatchIndex = -1);

private:
    Theme m_theme;
    QColor m_selectionColor = QColor(0, 51, 153, 100);
};

#include "EditorPainter.h"
#include "EditorLayout.h"
#include "Document.h"

#include <QPainter>
#include <QTextLayout>

EditorPainter::EditorPainter()
    : m_theme(Theme::light())
{
}

void EditorPainter::setTheme(const Theme& theme)
{
    m_theme = theme;
}

void EditorPainter::setSelectionColor(const QColor& color)
{
    m_selectionColor = color;
}

// [plan A8 2026-05-13] 单行绘制：调用方按 per-line loop 推进 cursorY，本函数在 cy 处画该行
// 文本区装饰（当前行高亮、搜索高亮）+ 文本（含选区）。lineH 由调用方传入（layout->lineHeight 精算）。
void EditorPainter::paintLine(QPainter* p, EditorLayout* layout, Document* doc,
                              int line, qreal cy, qreal lineH,
                              int gutterWidth, qreal viewWidth, qreal scrollX,
                              TextPosition cursorPos, bool hasSelection,
                              TextPosition selStartPos, TextPosition selEndPos,
                              const QVector<QPair<int,int>>& searchMatches,
                              int currentMatchIndex)
{
    const qreal margin = 8;
    const qreal textLeft = gutterWidth + margin - scrollX;

    // 当前行高亮（仅当此行是光标所在行且无选区）——只画文本区，不画 gutter 区域
    if (!hasSelection && line == cursorPos.line) {
        p->fillRect(QRectF(gutterWidth, cy, viewWidth - gutterWidth, lineH),
                    m_theme.editorCurrentLine);
    }

    QTextLayout* tl = layout->layoutForLine(line);
    if (!tl) return;

    // 搜索匹配高亮（该 line 上的所有 match）
    for (int i = 0; i < searchMatches.size(); ++i) {
        const auto& match = searchMatches[i];
        int matchLine = doc->offsetToLine(match.first);
        if (matchLine != line) continue;
        if (tl->lineCount() == 0) continue;

        int lineStartOffset = doc->lineToOffset(matchLine);
        int colStart = match.first - lineStartOffset;
        int colEnd = colStart + match.second;

        QColor highlightColor = (i == currentMatchIndex)
                                ? m_theme.editorSearchMatchCurrent
                                : m_theme.editorSearchMatch;

        // 遍历视觉行，找到匹配所在的视觉行
        for (int vi = 0; vi < tl->lineCount(); ++vi) {
            QTextLine vline = tl->lineAt(vi);
            int lineStart = vline.textStart();
            int lineEnd = lineStart + vline.textLength();

            int hlStart = qMax(colStart, lineStart);
            int hlEnd = qMin(colEnd, lineEnd);
            if (hlStart >= hlEnd) continue;

            qreal x1 = vline.cursorToX(hlStart);
            qreal x2 = vline.cursorToX(hlEnd);
            qreal vy = cy + vline.y();

            p->fillRect(QRectF(textLeft + x1, vy, x2 - x1, vline.height()),
                        highlightColor);
        }
    }

    // 文本绘制（通过 QTextLayout::draw 实现选区高亮）
    p->setPen(m_theme.editorFg);
    p->setFont(layout->font());

    QVector<QTextLayout::FormatRange> selections;
    if (hasSelection && line >= selStartPos.line && line <= selEndPos.line) {
        int lineLen = doc->lineText(line).length();
        int selStart = (line == selStartPos.line) ? selStartPos.column : 0;
        int selEnd = (line == selEndPos.line) ? selEndPos.column : lineLen;
        if (selStart < selEnd) {
            QTextLayout::FormatRange fmt;
            fmt.start = selStart;
            fmt.length = selEnd - selStart;
            fmt.format.setBackground(m_selectionColor);
            selections.append(fmt);
        }
    }

    tl->draw(p, QPointF(textLeft, cy), selections);
}

// [plan A8] 光标绘制——基于 cursorRect（lineY 路径），与 per-line loop 独立
void EditorPainter::paintCursor(QPainter* p, EditorLayout* layout,
                                TextPosition cursorPos, qreal scrollY,
                                int gutterWidth, qreal scrollX)
{
    const qreal margin = 8;
    const qreal textLeft = gutterWidth + margin - scrollX;
    QRectF cr = layout->cursorRect(cursorPos);
    cr.moveLeft(cr.x() + textLeft);
    cr.moveTop(cr.y() - scrollY);
    p->fillRect(cr, m_theme.editorCursor);
}

// [plan A8] IME 预编辑文本绘制——基于 cursorRect
void EditorPainter::paintIme(QPainter* p, EditorLayout* layout,
                             TextPosition cursorPos, qreal scrollY,
                             int gutterWidth, qreal scrollX,
                             const QString& preeditString)
{
    if (preeditString.isEmpty()) return;

    const qreal margin = 8;
    const qreal textLeft = gutterWidth + margin - scrollX;
    QRectF cr = layout->cursorRect(cursorPos);
    qreal px = cr.x() + textLeft;
    qreal py = cr.y() - scrollY;

    p->save();
    QFont preeditFont = layout->font();
    p->setFont(preeditFont);
    p->setPen(m_theme.editorFg);

    QFontMetricsF fm(preeditFont);
    qreal textWidth = fm.horizontalAdvance(preeditString);
    p->fillRect(QRectF(px, py, textWidth, cr.height()), m_theme.editorPreeditBg);
    p->drawText(QPointF(px, py + fm.ascent()), preeditString);

    p->setPen(QPen(m_theme.editorFg, 1, Qt::DashLine));
    p->drawLine(QPointF(px, py + cr.height() - 1),
                QPointF(px + textWidth, py + cr.height() - 1));
    p->restore();
}

void EditorPainter::paint(QPainter* painter, EditorLayout* layout, Document* doc,
                          int firstLine, int lastLine,
                          int gutterWidth, qreal scrollY, qreal scrollX,
                          bool cursorVisible,
                          TextPosition cursorPos,
                          const QString& preeditString,
                          const QVector<QPair<int,int>>& searchMatches,
                          int currentMatchIndex)
{
    const qreal margin = 8;
    qreal viewWidth = painter->clipBoundingRect().width();

    // 背景
    painter->fillRect(painter->clipBoundingRect(), m_theme.editorBg);

    // [plan A7 2026-05-13] 预热视口内行的 ensureLayout —— 触发 EditorLayout::ensureLayout
    // 末尾的 invalidateYCache，使后续所有 lineY 调用都拿到 ensureYCache 重建后的精算值。
    // 之前的 bug 链路：lineHeight 估算路径让 yCache 偏离 paint 实际位置 → 选区拖动 hitTest
    // 用估算 yCache 找行 → 与视觉位置不一致。预热后 yCache 与 paint 视觉完全一致。
    // 也涵盖光标 / 搜索高亮 / IME 等所有用 lineY 的路径——一处预热，全局一致（CLAUDE.md
    // 反模式 B「同一语义两份独立代码」反向修复）。
    for (int line = firstLine; line <= lastLine && line < layout->lineCount(); ++line) {
        layout->layoutForLine(line);
    }

    // 当前行高亮
    if (!doc->selection().hasSelection()) {
        qreal cy = layout->lineY(cursorPos.line) - scrollY;
        qreal ch = layout->lineHeight(cursorPos.line);
        painter->fillRect(QRectF(gutterWidth, cy, viewWidth, ch), m_theme.editorCurrentLine);
    }

    qreal textLeft = gutterWidth + margin - scrollX;

    // 搜索匹配高亮
    for (int i = 0; i < searchMatches.size(); ++i) {
        const auto& match = searchMatches[i];
        int matchLine = doc->offsetToLine(match.first);
        if (matchLine < firstLine || matchLine > lastLine) continue;

        QTextLayout* tl = layout->layoutForLine(matchLine);
        if (!tl || tl->lineCount() == 0) continue;

        int lineStartOffset = doc->lineToOffset(matchLine);
        int colStart = match.first - lineStartOffset;
        int colEnd = colStart + match.second;

        qreal baseY = layout->lineY(matchLine) - scrollY;

        // 当前匹配项使用不同颜色
        QColor highlightColor = (i == currentMatchIndex)
                                ? m_theme.editorSearchMatchCurrent
                                : m_theme.editorSearchMatch;

        // 遍历视觉行，找到匹配所在的视觉行
        for (int vi = 0; vi < tl->lineCount(); ++vi) {
            QTextLine vline = tl->lineAt(vi);
            int lineStart = vline.textStart();
            int lineEnd = lineStart + vline.textLength();

            int hlStart = qMax(colStart, lineStart);
            int hlEnd = qMin(colEnd, lineEnd);
            if (hlStart >= hlEnd) continue;

            qreal x1 = vline.cursorToX(hlStart);
            qreal x2 = vline.cursorToX(hlEnd);
            qreal vy = baseY + vline.y();

            painter->fillRect(QRectF(textLeft + x1, vy,
                                      x2 - x1, vline.height()),
                              highlightColor);
        }
    }

    // 文本绘制（通过 QTextLayout::draw 实现选区高亮）
    painter->setPen(m_theme.editorFg);
    painter->setFont(layout->font());

    bool hasSelection = doc->selection().hasSelection();
    TextPosition selStartPos, selEndPos;
    if (hasSelection) {
        selStartPos = doc->selection().range().start();
        selEndPos = doc->selection().range().end();
    }

    // [plan A7 2026-05-13] paint 入口已预热视口内行 ensureLayout（见函数顶部），yCache
    // 重建时已用精算 height，lineY(line) 与 paint 实际位置完全一致——不需要本地 cursor Y
    // 推进逻辑。回退到原 lineY(line) - scrollY 模式，与 EditorLayout::hitTest /
    // positionToPoint / cursorRect 共享同一坐标系，避免选区错位回归。
    for (int line = firstLine; line <= lastLine && line < layout->lineCount(); ++line) {
        QTextLayout* tl = layout->layoutForLine(line);
        if (!tl) continue;

        qreal y = layout->lineY(line) - scrollY;

        // 构建选区 FormatRange 让 QTextLayout::draw 自己绘制选区背景
        QVector<QTextLayout::FormatRange> selections;
        if (hasSelection && line >= selStartPos.line && line <= selEndPos.line) {
            int lineLen = doc->lineText(line).length();
            int selStart = (line == selStartPos.line) ? selStartPos.column : 0;
            int selEnd = (line == selEndPos.line) ? selEndPos.column : lineLen;
            if (selStart < selEnd) {
                QTextLayout::FormatRange fmt;
                fmt.start = selStart;
                fmt.length = selEnd - selStart;
                fmt.format.setBackground(m_selectionColor);
                selections.append(fmt);
            }
        }

        tl->draw(painter, QPointF(textLeft, y), selections);
    }

    // 输入法预编辑文本绘制
    if (!preeditString.isEmpty()) {
        QRectF cr = layout->cursorRect(cursorPos);
        qreal px = cr.x() + textLeft;
        qreal py = cr.y() - scrollY;

        painter->save();
        QFont preeditFont = layout->font();
        painter->setFont(preeditFont);
        painter->setPen(m_theme.editorFg);

        QFontMetricsF fm(preeditFont);
        qreal textWidth = fm.horizontalAdvance(preeditString);
        painter->fillRect(QRectF(px, py, textWidth, cr.height()), m_theme.editorPreeditBg);

        painter->drawText(QPointF(px, py + fm.ascent()), preeditString);

        painter->setPen(QPen(m_theme.editorFg, 1, Qt::DashLine));
        painter->drawLine(QPointF(px, py + cr.height() - 1),
                          QPointF(px + textWidth, py + cr.height() - 1));
        painter->restore();
    }

    // 光标绘制
    if (cursorVisible) {
        QRectF cr = layout->cursorRect(cursorPos);
        cr.moveLeft(cr.x() + textLeft);
        cr.moveTop(cr.y() - scrollY);
        painter->fillRect(cr, m_theme.editorCursor);
    }
}

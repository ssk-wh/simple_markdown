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

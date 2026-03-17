#include "EditorPainter.h"
#include "EditorLayout.h"
#include "Document.h"

#include <QPainter>
#include <QTextLayout>

EditorPainter::EditorPainter() = default;

void EditorPainter::paint(QPainter* painter, EditorLayout* layout, Document* doc,
                          int firstLine, int lastLine,
                          int gutterWidth, qreal scrollY,
                          bool cursorVisible,
                          TextPosition cursorPos,
                          const QString& preeditString)
{
    const qreal margin = 8;
    qreal viewWidth = painter->clipBoundingRect().width();

    // Background
    painter->fillRect(painter->clipBoundingRect(), Qt::white);

    // 当前行高亮 / 选区绘制
    bool hasSelection = doc->selection().hasSelection();
    if (!hasSelection) {
        // 当前行浅色背景
        qreal cy = layout->lineY(cursorPos.line) - scrollY;
        qreal ch = layout->lineHeight(cursorPos.line);
        painter->fillRect(QRectF(gutterWidth, cy, viewWidth, ch), QColor("#F5F5F5"));
    } else {
        // 选区蓝色背景
        TextPosition startPos = doc->selection().range().start();
        TextPosition endPos = doc->selection().range().end();
        int startLine = qMax(startPos.line, firstLine);
        int endLine = qMin(endPos.line, lastLine);

        for (int line = startLine; line <= endLine && line < layout->lineCount(); ++line) {
            QTextLayout* tl = layout->layoutForLine(line);
            if (!tl) continue;

            qreal lineTop = layout->lineY(line) - scrollY;
            int lineLen = doc->lineText(line).length();

            int selStart = (line == startPos.line) ? startPos.column : 0;
            int selEnd = (line == endPos.line) ? endPos.column : lineLen;

            if (selStart >= selEnd && line != endPos.line) {
                selEnd = lineLen + 1;  // 选中换行符，整行宽度
            }

            qreal x1 = tl->lineAt(0).cursorToX(selStart);
            qreal x2 = (selEnd > lineLen) ? viewWidth : tl->lineAt(0).cursorToX(selEnd);

            painter->fillRect(QRectF(gutterWidth + margin + x1, lineTop,
                                      x2 - x1, layout->lineHeight(line)),
                              QColor(181, 213, 255));  // #B5D5FF
        }
    }

    // Text
    painter->setPen(Qt::black);
    painter->setFont(layout->font());

    for (int line = firstLine; line <= lastLine && line < layout->lineCount(); ++line) {
        QTextLayout* tl = layout->layoutForLine(line);
        if (!tl) continue;

        qreal y = layout->lineY(line) - scrollY;
        tl->draw(painter, QPointF(gutterWidth + margin, y));  // 8px left margin
    }

    // Preedit 文本绘制
    if (!preeditString.isEmpty()) {
        QRectF cr = layout->cursorRect(cursorPos);
        qreal px = cr.x() + gutterWidth + margin;
        qreal py = cr.y() - scrollY;

        painter->save();
        QFont preeditFont = layout->font();
        painter->setFont(preeditFont);
        painter->setPen(Qt::black);

        QFontMetricsF fm(preeditFont);
        qreal textWidth = fm.horizontalAdvance(preeditString);
        painter->fillRect(QRectF(px, py, textWidth, cr.height()), QColor(255, 255, 200));

        painter->drawText(QPointF(px, py + fm.ascent()), preeditString);

        painter->setPen(QPen(Qt::black, 1, Qt::DashLine));
        painter->drawLine(QPointF(px, py + cr.height() - 1),
                          QPointF(px + textWidth, py + cr.height() - 1));
        painter->restore();
    }

    // 光标绘制
    if (cursorVisible) {
        QRectF cr = layout->cursorRect(cursorPos);
        cr.moveLeft(cr.x() + gutterWidth + 8);  // 加 gutter 和 margin 偏移
        cr.moveTop(cr.y() - scrollY);
        painter->fillRect(cr, Qt::black);
    }
}

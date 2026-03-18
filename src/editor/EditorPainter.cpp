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

void EditorPainter::paint(QPainter* painter, EditorLayout* layout, Document* doc,
                          int firstLine, int lastLine,
                          int gutterWidth, qreal scrollY,
                          bool cursorVisible,
                          TextPosition cursorPos,
                          const QString& preeditString,
                          const QVector<QPair<int,int>>& searchMatches)
{
    const qreal margin = 8;
    qreal viewWidth = painter->clipBoundingRect().width();

    // Background
    painter->fillRect(painter->clipBoundingRect(), m_theme.editorBg);

    // 当前行高亮（选区绘制已移到 EditorWidget::paintEvent 中）
    if (!doc->selection().hasSelection()) {
        qreal cy = layout->lineY(cursorPos.line) - scrollY;
        qreal ch = layout->lineHeight(cursorPos.line);
        painter->fillRect(QRectF(gutterWidth, cy, viewWidth, ch), m_theme.editorCurrentLine);
    }

    // 搜索匹配高亮
    for (auto& match : searchMatches) {
        int matchLine = doc->offsetToLine(match.first);
        if (matchLine < firstLine || matchLine > lastLine) continue;

        QTextLayout* tl = layout->layoutForLine(matchLine);
        if (!tl || tl->lineCount() == 0) continue;

        int lineStartOffset = doc->lineToOffset(matchLine);
        int colStart = match.first - lineStartOffset;
        int colEnd = colStart + match.second;

        qreal y = layout->lineY(matchLine) - scrollY;
        qreal x1 = tl->lineAt(0).cursorToX(colStart);
        qreal x2 = tl->lineAt(0).cursorToX(colEnd);

        painter->fillRect(QRectF(gutterWidth + margin + x1, y,
                                  x2 - x1, layout->lineHeight(matchLine)),
                          m_theme.editorSearchMatch);
    }

    // Text
    painter->setPen(m_theme.editorFg);
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
        cr.moveLeft(cr.x() + gutterWidth + 8);  // 加 gutter 和 margin 偏移
        cr.moveTop(cr.y() - scrollY);
        painter->fillRect(cr, m_theme.editorCursor);
    }
}

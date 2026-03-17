#include "EditorLayout.h"
#include "Document.h"
#include <QTextOption>
#include <algorithm>

EditorLayout::EditorLayout(QObject* parent)
    : QObject(parent)
    , m_fontMetrics(QFont())
{
    // Set a sensible default font
    QFont defaultFont("Courier New", 12);
    defaultFont.setStyleHint(QFont::Monospace);
    setFont(defaultFont);
}

EditorLayout::~EditorLayout() = default;

void EditorLayout::setFont(const QFont& font)
{
    m_font = font;
    m_fontMetrics = QFontMetricsF(m_font);
    m_defaultLineHeight = m_fontMetrics.lineSpacing();
    m_charWidth = m_fontMetrics.averageCharWidth();

    if (m_doc)
        rebuild();
}

QFont EditorLayout::font() const
{
    return m_font;
}

void EditorLayout::setTabStopWidth(int spaces)
{
    m_tabStopSpaces = spaces;
    if (m_doc)
        rebuild();
}

void EditorLayout::setWrapWidth(qreal width)
{
    m_wrapWidth = width;
    if (m_doc)
        rebuild();
}

void EditorLayout::setDocument(Document* doc)
{
    m_doc = doc;
    if (m_doc)
        rebuild();
}

void EditorLayout::rebuild()
{
    m_lines.clear();
    if (!m_doc) {
        invalidateYCache();
        return;
    }

    int count = m_doc->lineCount();
    m_lines.resize(count);
    // All lines start as dirty by default (LineInfo default)
    m_highlighter.setLineCount(count);
    invalidateYCache();
}

void EditorLayout::updateLines(int startLine, int endLine)
{
    if (!m_doc)
        return;

    int docLines = m_doc->lineCount();
    int oldSize = static_cast<int>(m_lines.size());

    if (docLines != oldSize) {
        m_lines.resize(docLines);
        // New lines are dirty by default
    }

    m_highlighter.invalidateFromLine(startLine);

    int end = std::min(endLine, static_cast<int>(m_lines.size()) - 1);
    for (int i = startLine; i <= end; ++i) {
        m_lines[i].layout.reset();
        m_lines[i].height = 0;
        m_lines[i].dirty = true;
    }

    invalidateYCache();
}

void EditorLayout::ensureLayout(int line) const
{
    if (line < 0 || line >= static_cast<int>(m_lines.size()))
        return;

    auto& info = m_lines[line];
    if (!info.dirty)
        return;

    QString text = m_doc->lineText(line);

    auto tl = std::make_unique<QTextLayout>(text, m_font);
    QTextOption option;
    option.setTabStopDistance(m_charWidth * m_tabStopSpaces);
    if (m_wrapWidth <= 0) {
        option.setWrapMode(QTextOption::NoWrap);
    } else {
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    }
    tl->setTextOption(option);

    tl->beginLayout();
    qreal y = 0;
    while (true) {
        QTextLine tline = tl->createLine();
        if (!tline.isValid())
            break;
        if (m_wrapWidth > 0)
            tline.setLineWidth(m_wrapWidth);
        tline.setPosition(QPointF(0, y));
        y += tline.height();
    }
    // 语法高亮
    QVector<HighlightToken> tokens = m_highlighter.highlightLine(line, text);
    QVector<QTextLayout::FormatRange> formats;
    formats.reserve(tokens.size());
    for (const auto& token : tokens) {
        QTextLayout::FormatRange fr;
        fr.start = token.start;
        fr.length = token.length;
        fr.format = token.format;
        formats.append(fr);
    }
    tl->setFormats(formats);

    tl->endLayout();

    info.height = y;
    info.layout = std::move(tl);
    info.dirty = false;
}

void EditorLayout::ensureYCache() const
{
    if (!m_yCacheDirty)
        return;

    int count = static_cast<int>(m_lines.size());
    m_lineYCache.resize(count);
    if (count > 0) {
        m_lineYCache[0] = 0;
        for (int i = 1; i < count; ++i) {
            m_lineYCache[i] = m_lineYCache[i - 1] + lineHeight(i - 1);
        }
    }
    m_yCacheDirty = false;
}

void EditorLayout::invalidateYCache()
{
    m_yCacheDirty = true;
}

TextPosition EditorLayout::hitTest(const QPointF& point) const
{
    if (m_lines.empty())
        return {0, 0};

    int line = lineAtY(point.y());
    ensureLayout(line);

    auto* tl = m_lines[line].layout.get();
    if (!tl)
        return {line, 0};

    qreal localY = point.y() - lineY(line);
    int tlLine = 0;
    for (int i = 0; i < tl->lineCount(); ++i) {
        QTextLine textLine = tl->lineAt(i);
        if (localY < textLine.y() + textLine.height()) {
            tlLine = i;
            break;
        }
        tlLine = i;
    }

    int col = tl->lineAt(tlLine).xToCursor(point.x());
    return {line, col};
}

QPointF EditorLayout::positionToPoint(TextPosition pos) const
{
    if (m_lines.empty())
        return QPointF(0, 0);

    int line = std::clamp(pos.line, 0, static_cast<int>(m_lines.size()) - 1);
    ensureLayout(line);

    auto* tl = m_lines[line].layout.get();
    if (!tl)
        return QPointF(0, lineY(line));

    // Find which QTextLine contains this column
    int col = pos.column;
    for (int i = 0; i < tl->lineCount(); ++i) {
        QTextLine textLine = tl->lineAt(i);
        if (col >= textLine.textStart() && col <= textLine.textStart() + textLine.textLength()) {
            qreal x = textLine.cursorToX(col);
            qreal y = lineY(line) + textLine.y();
            return QPointF(x, y);
        }
    }

    // Fallback: use last text line
    if (tl->lineCount() > 0) {
        QTextLine lastLine = tl->lineAt(tl->lineCount() - 1);
        qreal x = lastLine.cursorToX(col);
        qreal y = lineY(line) + lastLine.y();
        return QPointF(x, y);
    }

    return QPointF(0, lineY(line));
}

QRectF EditorLayout::cursorRect(TextPosition pos) const
{
    QPointF p = positionToPoint(pos);
    qreal h = m_defaultLineHeight;

    // Try to get actual line height from layout
    if (pos.line >= 0 && pos.line < static_cast<int>(m_lines.size())) {
        ensureLayout(pos.line);
        auto* tl = m_lines[pos.line].layout.get();
        if (tl && tl->lineCount() > 0) {
            h = tl->lineAt(0).height();
        }
    }

    return QRectF(p.x(), p.y(), 2, h);
}

qreal EditorLayout::lineY(int line) const
{
    if (line < 0 || line >= static_cast<int>(m_lines.size()))
        return 0;
    ensureYCache();
    return m_lineYCache[line];
}

qreal EditorLayout::lineHeight(int line) const
{
    if (line < 0 || line >= static_cast<int>(m_lines.size()))
        return m_defaultLineHeight;

    auto& info = m_lines[line];
    if (!info.dirty && info.layout)
        return info.height;

    return m_defaultLineHeight;
}

qreal EditorLayout::totalHeight() const
{
    if (m_lines.empty())
        return m_defaultLineHeight;

    ensureYCache();
    return m_lineYCache.back() + lineHeight(static_cast<int>(m_lines.size()) - 1);
}

int EditorLayout::lineAtY(qreal y) const
{
    if (m_lines.empty())
        return 0;

    if (y < 0)
        return 0;

    ensureYCache();

    // Binary search: find the last line whose Y <= y
    auto it = std::upper_bound(m_lineYCache.begin(), m_lineYCache.end(), y);
    int idx = static_cast<int>(std::distance(m_lineYCache.begin(), it)) - 1;

    if (idx < 0)
        return 0;
    if (idx >= static_cast<int>(m_lines.size()))
        return static_cast<int>(m_lines.size()) - 1;

    return idx;
}

int EditorLayout::lineCount() const
{
    return static_cast<int>(m_lines.size());
}

QTextLayout* EditorLayout::layoutForLine(int line) const
{
    if (line < 0 || line >= static_cast<int>(m_lines.size()))
        return nullptr;

    ensureLayout(line);
    return m_lines[line].layout.get();
}

qreal EditorLayout::defaultLineHeight() const
{
    return m_defaultLineHeight;
}

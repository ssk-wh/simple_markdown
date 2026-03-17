#include "EditorWidget.h"
#include "EditorLayout.h"
#include "EditorPainter.h"
#include "EditorInput.h"
#include "Document.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFontDatabase>

EditorWidget::EditorWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new EditorLayout(this);
    m_painter = new EditorPainter();
    m_input = new EditorInput(this);

    // Use system monospace font
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    m_layout->setFont(font);

    // Viewport settings
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);

    // Create default document
    m_doc = new Document(this);
    m_layout->setDocument(m_doc);

    connect(m_doc, &Document::textChanged, this, &EditorWidget::onTextChanged);

    // 光标闪烁
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this]() {
        m_cursorVisible = !m_cursorVisible;
        viewport()->update();
    });
    m_cursorBlinkTimer.start(500);

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
}

EditorWidget::~EditorWidget()
{
    delete m_painter;
    delete m_input;
}

void EditorWidget::setDocument(Document* doc)
{
    if (m_doc) {
        disconnect(m_doc, &Document::textChanged, this, &EditorWidget::onTextChanged);
    }

    m_doc = doc;

    if (m_doc) {
        connect(m_doc, &Document::textChanged, this, &EditorWidget::onTextChanged);
    }

    m_layout->setDocument(m_doc);
    m_layout->rebuild();
    updateScrollBars();
    viewport()->update();
}

Document* EditorWidget::document() const
{
    return m_doc;
}

EditorLayout* EditorWidget::editorLayout() const
{
    return m_layout;
}

int EditorWidget::gutterWidth() const
{
    return m_gutterWidth;
}

void EditorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing);

    int first = firstVisibleLine();
    int last = lastVisibleLine();
    qreal sy = scrollY();

    // Draw gutter background
    painter.fillRect(0, 0, m_gutterWidth, viewport()->height(), QColor("#F0F0F0"));

    // Draw line numbers
    painter.setPen(QColor("#999999"));
    QFont gutterFont = m_layout->font();
    gutterFont.setPointSize(gutterFont.pointSize() - 1);
    painter.setFont(gutterFont);
    for (int line = first; line <= last && line < m_layout->lineCount(); ++line) {
        qreal y = m_layout->lineY(line) - sy;
        painter.drawText(QRectF(0, y, m_gutterWidth - 8, m_layout->lineHeight(line)),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(line + 1));
    }

    // Draw separator line
    painter.setPen(QColor("#E0E0E0"));
    painter.drawLine(m_gutterWidth - 1, 0, m_gutterWidth - 1, viewport()->height());

    // Draw text area
    painter.save();
    painter.setClipRect(m_gutterWidth, 0, viewport()->width() - m_gutterWidth, viewport()->height());

    m_painter->paint(&painter, m_layout, m_doc, first, last,
                     m_gutterWidth, sy,
                     m_cursorVisible && hasFocus(),
                     m_doc->selection().cursorPosition(),
                     m_preeditString);
    painter.restore();
}

void EditorWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void EditorWidget::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    viewport()->update();
}

void EditorWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_input->keyPressEvent(event)) {
        m_cursorVisible = true;
        m_cursorBlinkTimer.start(500);  // 重置闪烁
        ensureCursorVisible();
        viewport()->update();
    } else {
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void EditorWidget::focusInEvent(QFocusEvent* event)
{
    m_cursorVisible = true;
    m_cursorBlinkTimer.start(500);
    viewport()->update();
    QAbstractScrollArea::focusInEvent(event);
}

void EditorWidget::focusOutEvent(QFocusEvent* event)
{
    m_cursorBlinkTimer.stop();
    m_cursorVisible = false;
    viewport()->update();
    QAbstractScrollArea::focusOutEvent(event);
}

static bool isWordChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

TextPosition EditorWidget::pixelToTextPosition(const QPoint& pos) const {
    qreal x = pos.x() - m_gutterWidth - 8;  // 减去 gutter 和 margin
    qreal y = pos.y() + scrollY();
    return m_layout->hitTest(QPointF(x, y));
}

void EditorWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        TextPosition pos = pixelToTextPosition(event->pos());
        bool shift = event->modifiers() & Qt::ShiftModifier;
        if (shift) {
            m_doc->selection().extendSelection(pos);
        } else {
            m_doc->selection().setCursorPosition(pos);
        }
        m_mousePressed = true;
        m_cursorVisible = true;
        m_cursorBlinkTimer.start(500);
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void EditorWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_mousePressed) {
        TextPosition pos = pixelToTextPosition(event->pos());
        m_doc->selection().extendSelection(pos);
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorWidget::mouseReleaseEvent(QMouseEvent* event) {
    m_mousePressed = false;
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void EditorWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        TextPosition pos = pixelToTextPosition(event->pos());
        QString lineText = m_doc->lineText(pos.line);

        int wordStart = pos.column;
        int wordEnd = pos.column;

        while (wordStart > 0 && isWordChar(lineText[wordStart - 1]))
            wordStart--;
        while (wordEnd < lineText.length() && isWordChar(lineText[wordEnd]))
            wordEnd++;

        m_doc->selection().setSelection({pos.line, wordStart}, {pos.line, wordEnd});
        viewport()->update();
    }
}

void EditorWidget::ensureCursorVisible()
{
    QRectF cr = m_layout->cursorRect(m_doc->selection().cursorPosition());
    qreal cy = cr.y();
    qreal ch = cr.height();
    qreal sy = scrollY();
    qreal vh = viewport()->height();

    if (cy < sy) {
        verticalScrollBar()->setValue((int)cy);
    } else if (cy + ch > sy + vh) {
        verticalScrollBar()->setValue((int)(cy + ch - vh));
    }
}

void EditorWidget::onTextChanged(int offset, int removedLen, int addedLen)
{
    Q_UNUSED(removedLen);

    int startLine = m_doc->offsetToLine(offset);
    int endLine = startLine;
    if (addedLen > 0) {
        endLine = m_doc->offsetToLine(offset + addedLen);
    }
    m_layout->updateLines(startLine, endLine);
    updateScrollBars();
    viewport()->update();
}

void EditorWidget::updateScrollBars()
{
    qreal totalH = m_layout->totalHeight();
    qreal viewH = viewport()->height();
    verticalScrollBar()->setRange(0, qMax(0, (int)(totalH - viewH)));
    verticalScrollBar()->setPageStep((int)viewH);
    verticalScrollBar()->setSingleStep((int)m_layout->defaultLineHeight());
}

void EditorWidget::updateGutterWidth()
{
    int digits = 1;
    int maxLine = m_layout->lineCount();
    while (maxLine >= 10) {
        ++digits;
        maxLine /= 10;
    }
    QFontMetrics fm(m_layout->font());
    m_gutterWidth = fm.horizontalAdvance(QLatin1Char('9')) * (digits + 2) + 8;
}

int EditorWidget::firstVisibleLine() const
{
    return m_layout->lineAtY(scrollY());
}

int EditorWidget::lastVisibleLine() const
{
    return m_layout->lineAtY(scrollY() + viewport()->height());
}

qreal EditorWidget::scrollY() const
{
    return verticalScrollBar()->value();
}

void EditorWidget::inputMethodEvent(QInputMethodEvent* event)
{
    if (!event->commitString().isEmpty()) {
        m_preeditString.clear();
        m_input->insertText(event->commitString());
        ensureCursorVisible();
    }

    m_preeditString = event->preeditString();

    viewport()->update();
    event->accept();
}

QVariant EditorWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;

    case Qt::ImCursorRectangle: {
        QRectF cr = m_layout->cursorRect(m_doc->selection().cursorPosition());
        cr.moveLeft(cr.x() + m_gutterWidth + 8);
        cr.moveTop(cr.y() - scrollY());
        return cr.toRect();
    }

    case Qt::ImSurroundingText:
        return m_doc->lineText(m_doc->selection().cursorPosition().line);

    case Qt::ImCursorPosition:
        return m_doc->selection().cursorPosition().column;

    case Qt::ImFont:
        return m_layout->font();

    default:
        return QAbstractScrollArea::inputMethodQuery(query);
    }
}

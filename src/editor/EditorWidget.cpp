#include "EditorWidget.h"
#include "EditorLayout.h"
#include "EditorPainter.h"
#include "Document.h"

#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QFontDatabase>

EditorWidget::EditorWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new EditorLayout(this);
    m_painter = new EditorPainter();

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
}

EditorWidget::~EditorWidget()
{
    delete m_painter;
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
                     m_gutterWidth, sy);
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

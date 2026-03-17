#include "PreviewWidget.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "ImageCache.h"

#include <QPainter>
#include <QScrollBar>
#include <QResizeEvent>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new PreviewLayout();
    m_layout->setFont(font());

    m_painter = new PreviewPainter();

    m_imageCache = new ImageCache(this);

    viewport()->setAutoFillBackground(true);
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, Qt::white);
    viewport()->setPalette(pal);

    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

PreviewWidget::~PreviewWidget()
{
    delete m_layout;
    delete m_painter;
    // m_imageCache is owned by QObject parent
}

PreviewLayout* PreviewWidget::previewLayout() const
{
    return m_layout;
}

void PreviewWidget::updateAst(std::shared_ptr<AstNode> root)
{
    m_currentAst = std::move(root);

    qreal contentWidth = viewport()->width() - 40; // 20px padding each side
    if (contentWidth < 100) contentWidth = 100;

    m_layout->setViewportWidth(contentWidth);
    m_layout->buildFromAst(m_currentAst);

    updateScrollBars();
    viewport()->update();
}

void PreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), m_theme.previewBg);

    if (!m_currentAst) return;

    qreal scrollY = verticalScrollBar()->value();
    qreal vpHeight = viewport()->height();
    qreal vpWidth = viewport()->width();

    // Apply 20px horizontal padding
    painter.translate(20, 0);

    m_painter->paint(&painter, m_layout->rootBlock(), scrollY, vpHeight, vpWidth - 40);
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);

    if (m_currentAst) {
        qreal contentWidth = viewport()->width() - 40;
        if (contentWidth < 100) contentWidth = 100;
        m_layout->setViewportWidth(contentWidth);
        m_layout->buildFromAst(m_currentAst);
        updateScrollBars();
    }
}

void PreviewWidget::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    viewport()->update();
}

void PreviewWidget::updateScrollBars()
{
    qreal totalH = m_layout->totalHeight();
    qreal vpH = viewport()->height();
    int maxScroll = qMax(0, static_cast<int>(totalH - vpH));
    verticalScrollBar()->setRange(0, maxScroll);
    verticalScrollBar()->setPageStep(static_cast<int>(vpH));
    verticalScrollBar()->setSingleStep(20);
}

void PreviewWidget::setTheme(const Theme& theme)
{
    m_theme = theme;
    m_painter->setTheme(theme);

    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, theme.previewBg);
    viewport()->setPalette(pal);

    viewport()->update();
}

void PreviewWidget::scrollToSourceLine(int line)
{
    qreal y = m_layout->sourceLineToY(line);
    verticalScrollBar()->setValue(static_cast<int>(y));
}

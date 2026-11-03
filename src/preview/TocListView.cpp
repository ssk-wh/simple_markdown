// src/preview/TocListView.cpp
//
// Spec: specs/模块-preview/07-TOC面板.md（虚拟化列表，INV-TOC-VIRTUAL）
// Last synced: 2026-11-02
#include "TocListView.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <algorithm>

#include "TocPanel.h"

namespace {
constexpr int kPadH = 8;
constexpr int kStepIndent = 12;
constexpr int kArrowW = 14;
}  // namespace

TocListView::TocListView(QWidget* parent) : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    verticalScrollBar()->setSingleStep(kRowHeight);
}

void TocListView::setTheme(const Theme& theme)
{
    m_theme = theme;
    // 滚动条外观由主窗口 QSS 统一提供，这里不再单独设置（避免额外 QStyleSheetStyle 实例）
    viewport()->update();
}

void TocListView::setVisibleRows(const QVector<int>& visibleIdx)
{
    m_rows = visibleIdx;
    if (m_hoverRow >= m_rows.size()) m_hoverRow = -1;
    syncScrollBar();
    viewport()->update();
}

void TocListView::syncScrollBar()
{
    QScrollBar* bar = verticalScrollBar();
    const int total = m_rows.size() * kRowHeight + 10;
    bar->setRange(0, std::max(0, total - viewport()->height()));
    bar->setPageStep(viewport()->height());
}

void TocListView::resizeEvent(QResizeEvent* e)
{
    QAbstractScrollArea::resizeEvent(e);
    syncScrollBar();
}

int TocListView::rowAt(int y) const
{
    const int r = (y + verticalScrollBar()->value() - 2) / kRowHeight;
    return (r >= 0 && r < m_rows.size() && y + verticalScrollBar()->value() >= 2) ? r : -1;
}

int TocListView::arrowLeft(int level) const
{
    return kPadH + std::max(0, (level - 1) * kStepIndent);
}

void TocListView::ensureEntryVisible(int entryIdx)
{
    const int r = static_cast<int>(std::lower_bound(m_rows.begin(), m_rows.end(), entryIdx) - m_rows.begin());
    if (r >= m_rows.size() || m_rows[r] != entryIdx) return;
    QScrollBar* bar = verticalScrollBar();
    const int top = r * kRowHeight + 2;
    if (top < bar->value()) bar->setValue(top);
    else if (top + kRowHeight > bar->value() + viewport()->height())
        bar->setValue(top + kRowHeight - viewport()->height());
}

void TocListView::paintEvent(QPaintEvent*)
{
    QPainter p(viewport());
    if (!m_src.entries) return;
    const QVector<TocEntry>& entries = *m_src.entries;
    const int scroll = verticalScrollBar()->value();
    const int first = std::max(0, (scroll - 2) / kRowHeight);
    const int last = std::min<int>(m_rows.size() - 1, (scroll + viewport()->height()) / kRowHeight);

    const QColor fg = m_theme.previewFg;
    const QColor subFg = m_theme.previewImageInfoText;
    const QColor accent = m_theme.accentColor;
    const QColor bulletDefault = m_theme.previewTableBorder;
    const QColor bulletActive = m_theme.isDark ? accent.lighter(130) : accent.darker(120);
    QColor hoverBg = m_theme.isDark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 15);

    p.setRenderHint(QPainter::Antialiasing, true);
    for (int r = first; r <= last; ++r) {
        const int idx = m_rows[r];
        const TocEntry& e = entries[idx];
        const QRect row(4, r * kRowHeight + 2 - scroll, viewport()->width() - 8, kRowHeight - 2);
        const bool active = m_src.highlighted && m_src.highlighted->contains(idx);
        const bool focused = m_src.focusIdx && *m_src.focusIdx == idx;
        if (r == m_hoverRow || focused) {
            p.setPen(Qt::NoPen);
            p.setBrush(hoverBg);
            p.drawRoundedRect(row, 7, 7);
        }
        QFont f = p.font();
        f.setPixelSize(e.level == 1 ? 14 : 13);
        f.setBold(active);
        p.setFont(f);
        const QColor itemFg = e.level <= 2 ? fg : subFg;

        const int left = arrowLeft(e.level);
        const bool hasKids = m_src.hasChild && idx < m_src.hasChild->size() && (*m_src.hasChild)[idx];
        if (hasKids) {
            const bool coll = m_src.collapsed && m_src.collapsed->contains(idx);
            p.setPen(itemFg);
            QFont af = f;
            af.setBold(true);
            af.setPixelSize(12);
            p.setFont(af);
            p.drawText(QRect(left, row.top(), kArrowW, row.height()), Qt::AlignCenter,
                       coll ? QStringLiteral("+") : QStringLiteral("−"));
            p.setFont(f);
        }
        // bullet
        const int bulletSize = e.level == 1 ? 6 : 5;
        const QPointF c(left + kArrowW + 6, row.center().y() + 0.5);
        if (active) {
            QColor g = bulletActive;
            g.setAlpha(72);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(c, bulletSize / 2.0 + 3, bulletSize / 2.0 + 3);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(active ? bulletActive : bulletDefault);
        p.drawEllipse(c, bulletSize / 2.0, bulletSize / 2.0);
        // 文本
        const int textLeft = left + kArrowW + 16;
        const QRect tr(textLeft, row.top(), row.right() - textLeft - 4, row.height());
        p.setPen(itemFg);
        const QString txt = QFontMetrics(f).elidedText(e.title, Qt::ElideRight, tr.width());
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, txt);
    }
}

void TocListView::mouseMoveEvent(QMouseEvent* e)
{
    const int r = rowAt(e->pos().y());
    if (r != m_hoverRow) {
        m_hoverRow = r;
        viewport()->setCursor(r >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        viewport()->update();
    }
}

void TocListView::leaveEvent(QEvent*)
{
    m_hoverRow = -1;
    viewport()->update();
}

void TocListView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const int r = rowAt(e->pos().y());
    if (r < 0 || !m_src.entries) return;
    const int idx = m_rows[r];
    const int level = (*m_src.entries)[idx].level;
    const int left = arrowLeft(level) + 4;
    const bool hasKids = m_src.hasChild && idx < m_src.hasChild->size() && (*m_src.hasChild)[idx];
    if (hasKids && e->pos().x() >= left && e->pos().x() < left + kArrowW + 2) {
        emit arrowClicked(idx);
        return;
    }
    emit entryClicked(idx);
}

void TocListView::contextMenuEvent(QContextMenuEvent* e)
{
    const int r = rowAt(e->pos().y());
    if (r < 0) return;
    emit contextMenuForEntry(m_rows[r], e->globalPos());
}

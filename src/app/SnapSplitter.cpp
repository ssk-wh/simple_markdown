// src/app/SnapSplitter.cpp
//
// Spec: specs/模块-app/13-分隔条吸附刻度.md
// Last synced: 2026-04-15
#include "SnapSplitter.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QEvent>
#include <QChildEvent>
#include <QWidget>
#include <QtMath>

namespace app {

namespace {
// INV-SNAP-THRESHOLD: 5 逻辑像素
constexpr int kSnapThresholdPx = 5;

// INV-SNAP-TARGETS: 25% / 50% / 75%
constexpr double kRatios[3] = {0.25, 0.50, 0.75};
} // namespace

// ---------------------------------------------------------------------------
// SnapOverlay: 半透明覆盖层，仅绘制 3 条垂直虚线
// ---------------------------------------------------------------------------
class SnapOverlay : public QWidget {
public:
    explicit SnapOverlay(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);  // INV-SNAP-TRANSPARENT
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

    void setAccentColor(const QColor& color) {
        m_accent = color;
        if (isVisible()) update();
    }

    void setActiveIndex(int idx) {
        if (idx == m_activeIdx) return;
        m_activeIdx = idx;
        if (isVisible()) update();
    }

    int activeIndex() const { return m_activeIdx; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const int w = width();
        const int h = height();
        if (w <= 0 || h <= 0) return;

        for (int i = 0; i < 3; ++i) {
            const int x = qRound(w * kRatios[i]);
            const bool active = (i == m_activeIdx);

            QColor c = m_accent;
            c.setAlpha(active ? 220 : 110);

            QPen pen(c);
            pen.setStyle(Qt::DashLine);
            pen.setWidth(active ? 2 : 1);
            pen.setDashPattern({4.0, 4.0});
            p.setPen(pen);
            p.drawLine(x, 0, x, h);
        }
    }

private:
    QColor m_accent = QColor(128, 128, 128);
    int m_activeIdx = -1;
};

// ---------------------------------------------------------------------------
// SnapSplitterHandle: 拦截鼠标 press/release，通知 splitter 启停 overlay
// ---------------------------------------------------------------------------
class SnapSplitterHandle : public QSplitterHandle {
public:
    SnapSplitterHandle(Qt::Orientation orientation, SnapSplitter* parent)
        : QSplitterHandle(orientation, parent), m_snap(parent) {}

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && m_snap)
            m_snap->beginDrag();
        QSplitterHandle::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        QSplitterHandle::mouseReleaseEvent(e);
        if (e->button() == Qt::LeftButton && m_snap)
            m_snap->endDrag();
    }

private:
    SnapSplitter* m_snap;
};

// ---------------------------------------------------------------------------
// SnapSplitter
// ---------------------------------------------------------------------------
SnapSplitter::SnapSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent)
{
    connect(this, &QSplitter::splitterMoved,
            this, &SnapSplitter::onSplitterMoved);
}

void SnapSplitter::setAccentColor(const QColor& color)
{
    ensureOverlay();
    m_overlay->setAccentColor(color);
}

QSplitterHandle* SnapSplitter::createHandle()
{
    return new SnapSplitterHandle(orientation(), this);
}

void SnapSplitter::resizeEvent(QResizeEvent* event)
{
    QSplitter::resizeEvent(event);
    if (m_dragging)
        syncOverlayGeometry();
}

bool SnapSplitter::event(QEvent* event)
{
    // 子 pane（editor/preview）变化时，确保 overlay 一直处于 Z 顶层
    if (m_dragging && m_overlay && event->type() == QEvent::ChildAdded) {
        m_overlay->raise();
    }
    return QSplitter::event(event);
}

void SnapSplitter::ensureOverlay()
{
    if (m_overlay) return;
    // 关键：先以 nullptr 父构造，赋值 m_overlay，再 setParent 触发 childEvent。
    // 这样 childEvent 里能通过 child == m_overlay 识别并跳过 QSplitter 的 pane 注册。
    auto* ov = new SnapOverlay(nullptr);
    m_overlay = ov;
    ov->setParent(this);
    ov->setGeometry(rect());
}

void SnapSplitter::childEvent(QChildEvent* event)
{
    // 当新子 widget 正是我们的 overlay 时，绕过 QSplitter::childEvent，
    // 避免它把 overlay 注册成第 3 个 pane 挤占 editor/preview 的空间。
    if (event->added() && event->child() == m_overlay) {
        return;  // overlay 仍保留在 QObject 父子关系中，但 QSplitter 不把它当 pane
    }
    QSplitter::childEvent(event);
}

void SnapSplitter::beginDrag()
{
    ensureOverlay();
    m_dragging = true;
    m_overlay->setActiveIndex(-1);
    syncOverlayGeometry();
    m_overlay->show();
    m_overlay->raise();  // INV-SNAP-VISIBLE-ON-DRAG：顶层显示
}

void SnapSplitter::endDrag()
{
    m_dragging = false;
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->setActiveIndex(-1);
    }
}

void SnapSplitter::syncOverlayGeometry()
{
    if (!m_overlay) return;
    m_overlay->setGeometry(rect());
}

void SnapSplitter::onSplitterMoved(int pos, int index)
{
    // 只处理 editor|preview 之间的 handle（index 1）
    if (index != 1) return;
    if (!m_dragging) return;
    if (m_snapping) return;  // INV-SNAP-NO-RECURSION

    const int W = width();
    if (W <= 0) return;

    // 找到最近的吸附目标
    int hit = -1;
    int bestDelta = kSnapThresholdPx + 1;
    for (int i = 0; i < 3; ++i) {
        const int target = qRound(W * kRatios[i]);
        const int delta = qAbs(pos - target);
        if (delta <= kSnapThresholdPx && delta < bestDelta) {
            bestDelta = delta;
            hit = i;
        }
    }

    if (m_overlay) m_overlay->setActiveIndex(hit);

    if (hit < 0) return;

    const int target = qRound(W * kRatios[hit]);
    const int hw = handleWidth();
    // 左 pane = target；右 pane = W - target - handle
    int leftSize  = target;
    int rightSize = W - target - hw;
    if (rightSize < 0) { rightSize = 0; leftSize = qMax(0, W - hw); }

    m_snapping = true;
    setSizes({leftSize, rightSize});
    m_snapping = false;
}

} // namespace app

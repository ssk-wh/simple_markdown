// src/app/SnapSplitter.cpp
//
// Spec: specs/模块-app/13-分隔条吸附刻度.md
// Last synced: 2026-05-07
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
// SnapSplitterHandle: 拦截鼠标 press/move/release，主动驱动 SnapOverlay 吸附检测
// （OpaqueResize=false 下原本依赖的 splitterMoved 信号在拖拽过程中不 emit，
// 必须自己驱动——见 INV-SNAP-LIVE-HINT-IN-LAZY-MODE）
// ---------------------------------------------------------------------------
class SnapSplitterHandle : public QSplitterHandle {
public:
    SnapSplitterHandle(Qt::Orientation orientation, SnapSplitter* parent)
        : QSplitterHandle(orientation, parent), m_snap(parent) {}

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && m_snap) {
            // [INV-SNAP-LIVE-HINT-IN-LAZY-MODE] 记录鼠标按下时在 handle 内的偏移，
            // mouseMove 用来从鼠标位置反推 handle 左边缘位置（pos 语义与
            // onSplitterMoved 一致：handle 左边缘在 splitter 坐标系下的位置）。
            m_mouseOffsetInHandle = (orientation() == Qt::Horizontal)
                                    ? e->pos().x() : e->pos().y();
            m_snap->beginDrag();
        }
        QSplitterHandle::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        QSplitterHandle::mouseMoveEvent(e);  // base class 画 rubber band
        // [INV-SNAP-LIVE-HINT-IN-LAZY-MODE] 主动驱动吸附检测——不依赖
        // splitterMoved 信号（OpaqueResize=false 下不连续 emit）
        if (!m_snap || !m_snap->isDragging()) return;
        if (!(e->buttons() & Qt::LeftButton)) return;

        // 把 handle 自身坐标系下的鼠标位置 map 到 splitter 坐标系
        const QPoint splitterMousePos = mapToParent(e->pos());
        const int mouseSplitterCoord = (orientation() == Qt::Horizontal)
                                        ? splitterMousePos.x()
                                        : splitterMousePos.y();
        // handle 左边缘 = 鼠标 splitter 坐标 - 鼠标在 handle 内的偏移
        const int handleEdgePos = mouseSplitterCoord - m_mouseOffsetInHandle;
        m_snap->notifyDragHint(handleEdgePos, /*applySnap=*/false);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        QSplitterHandle::mouseReleaseEvent(e);  // base class 一次性 setSizes 到 release 位置
        if (e->button() == Qt::LeftButton && m_snap) {
            // [INV-SNAP-LIVE-HINT-IN-LAZY-MODE] release 位置在阈值内时吸附——
            // 不依赖 splitterMoved 信号路径（OpaqueResize=false 下未必连续 emit）。
            // base class 已把 sizes[0] 设为 release 位置 = handle 左边缘 X
            const QList<int> sizes = m_snap->sizes();
            if (!sizes.isEmpty()) {
                m_snap->notifyDragHint(sizes.first(), /*applySnap=*/true);
            }
            m_snap->endDrag();
        }
    }

    // Spec: specs/模块-app/13-分隔条吸附刻度.md INV-SNAP-DOUBLE-CLICK-RESET
    // 双击 handle：把 editor / preview 重置为 50/50。
    // 既是"拖歪了恢复对称"的快捷方式，也是 INV-SNAP-NO-COLLAPSE 的救急通道
    // （即使 minimumWidth / setChildrenCollapsible 被其它路径绕过，双击始终生效）
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && m_snap && m_snap->orientation() == Qt::Horizontal) {
            const int total = m_snap->width();
            const int hw = m_snap->handleWidth();
            const int each = qMax(0, (total - hw) / 2);
            m_snap->setSizes({ each, each });
            e->accept();
            return;
        }
        QSplitterHandle::mouseDoubleClickEvent(e);
    }

private:
    SnapSplitter* m_snap;
    // [INV-SNAP-LIVE-HINT-IN-LAZY-MODE] 鼠标按下时在 handle 内的偏移（按朝向取 x 或 y），
    // mouseMove 时用来从鼠标位置反推 handle 左边缘——给 SnapSplitter::notifyDragHint
    // 提供与 onSplitterMoved 等价的 pos 语义。
    int m_mouseOffsetInHandle = 0;
};

// ---------------------------------------------------------------------------
// SnapSplitter
// ---------------------------------------------------------------------------
SnapSplitter::SnapSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent)
{
    // Spec: specs/模块-app/13-分隔条吸附刻度.md INV-SNAP-NO-COLLAPSE
    // 禁止任一侧被拖到 0：若允许 collapse，用户把 handle 拖到边缘后
    // editor 或 preview 会消失、handle 贴边几乎不可抓回。
    // 子 pane 的 minimumWidth 由 MainWindow::createTab 设置兜底。
    setChildrenCollapsible(false);

    // [INV-SNAP-LAZY-RESIZE-VISUAL] 拖拽期间不实时调整子 pane 几何，仅 Qt 内置
    // rubber band 在原位置画一条参考虚线。子 pane 在 mouseRelease 时一次性 resize
    // 到目标尺寸——避免拖拽过程中"整个渲染区跟随手柄移动"的视觉混乱
    // （之前实测体验：内容因 INV-SNAP-LAZY-PANE-REBUILD 不重排，但容器宽度变化
    // 让旧布局被裁切/留白，比 lazy resize 视觉更糟）。
    //
    // 与 INV-SNAP-LAZY-PANE-REBUILD 协同链路：
    //   mousePress  → beginDrag (setProperty smSnapDragging=true, show overlay)
    //   mouseMove   → Qt 画 rubber band；子 widget 不收 resizeEvent（OpaqueResize=false）
    //   mouseRelease→ base class setSizes 一次 → 子 widget resizeEvent 守护跳过 rebuild
    //              → endDrag 清 property → emit dragFinished → MainWindow slot 真正 rebuild
    //
    // 副作用：吸附辅助线（SnapOverlay 三条虚线）拖拽过程中显示但不高亮
    // （onSplitterMoved 在 release 时才 emit），吸附行为变成"松手时跳到刻度"。
    setOpaqueResize(false);

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
    // [INV-SNAP-LAZY-PANE-REBUILD] 通过 dynamic property 给子 pane（editor /
    // preview）的 resizeEvent 看：拖拽期间应当跳过全文 buildFromAst /
    // EditorLayout::rebuild，仅更新滚动条范围。子 pane 不知道父类是 SnapSplitter，
    // 用 dynamic property 解耦避免架构反向依赖。
    setProperty("smSnapDragging", true);
    m_overlay->setActiveIndex(-1);
    syncOverlayGeometry();
    m_overlay->show();
    m_overlay->raise();  // INV-SNAP-VISIBLE-ON-DRAG：顶层显示
}

void SnapSplitter::endDrag()
{
    m_dragging = false;
    // [INV-SNAP-LAZY-PANE-REBUILD] 清拖拽态——必须在 emit dragFinished 之前清，
    // 因为 dragFinished 的 slot 里子 pane 会调用 rebuildLayout 等需要"非拖拽态"
    // 的 invariant；若 property 还是 true，子 pane 的 resizeEvent 会再次跳过重排。
    setProperty("smSnapDragging", false);
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->setActiveIndex(-1);
    }
    emit dragFinished();
}

void SnapSplitter::syncOverlayGeometry()
{
    if (!m_overlay) return;
    m_overlay->setGeometry(rect());
}

void SnapSplitter::onSplitterMoved(int pos, int index)
{
    // [INV-SNAP-LIVE-HINT-IN-LAZY-MODE] 在 OpaqueResize=false 下本槽函数不再
    // 在拖拽过程中连续触发——主动路径已搬到 SnapSplitterHandle::mouseMoveEvent
    // 调用 notifyDragHint。这里保留作为兼容路径：若未来某个用法把 splitter
    // 改回 OpaqueResize=true，本槽函数仍能驱动吸附；同时 release 时若 base
    // class emit 了 splitterMoved，本槽也会跑——与 mouseReleaseEvent 中的
    // notifyDragHint(applySnap=true) 重复但无害（同一目标 setSizes 等价）。
    if (index != 1) return;
    if (!m_dragging) return;
    if (m_snapping) return;  // INV-SNAP-NO-RECURSION
    notifyDragHint(pos, /*applySnap=*/true);
}

// [INV-SNAP-LIVE-HINT-IN-LAZY-MODE] 主动驱动的吸附检测路径——
// SnapSplitterHandle::mouseMoveEvent / mouseReleaseEvent 调用，绕过 splitterMoved 信号。
// applySnap=false：mouseMove 期间，仅高亮 SnapOverlay 上对应吸附线
// applySnap=true ：mouseRelease 时，若 hit 命中阈值，setSizes 强制吸附到刻度
void SnapSplitter::notifyDragHint(int pos, bool applySnap)
{
    if (!m_dragging) return;

    const int W = width();
    if (W <= 0) return;

    // 找到最近的吸附目标（与原 onSplitterMoved 算法一致）
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

    if (!applySnap || hit < 0) return;
    if (m_snapping) return;  // INV-SNAP-NO-RECURSION

    const int target = qRound(W * kRatios[hit]);
    const int hw = handleWidth();
    int leftSize = target;
    int rightSize = W - target - hw;
    if (rightSize < 0) { rightSize = 0; leftSize = qMax(0, W - hw); }

    m_snapping = true;
    setSizes({leftSize, rightSize});
    m_snapping = false;
}

} // namespace app

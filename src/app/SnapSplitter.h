// src/app/SnapSplitter.h
//
// Spec: specs/模块-app/13-分隔条吸附刻度.md
// Invariants enforced here:
//   INV-SNAP-THRESHOLD, INV-SNAP-TARGETS, INV-SNAP-VISIBLE-ON-DRAG,
//   INV-SNAP-TRANSPARENT, INV-SNAP-THEME, INV-SNAP-NO-RECURSION,
//   INV-SNAP-LAZY-PANE-REBUILD, INV-SNAP-LAZY-RESIZE-VISUAL
// Last synced: 2026-05-07
#pragma once

#include <QSplitter>
#include <QSplitterHandle>
#include <QColor>

class QWidget;

namespace app {

class SnapOverlay;

// 水平 QSplitter，拖动中间 handle 时显示 1/4、1/2、3/4 吸附辅助线。
// 薄封装 —— 对调用方保持与 QSplitter 相同的 API。
class SnapSplitter : public QSplitter {
    Q_OBJECT
public:
    explicit SnapSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);

    // 由 MainWindow::applyTheme 调用以保持辅助线颜色同步。
    void setAccentColor(const QColor& color);

    // [INV-SNAP-LAZY-PANE-REBUILD] 给子 pane 的 resizeEvent 用，判定是否
    // 当前正处于 handle 拖拽中。子 pane 不直接 include 本头文件，
    // 通过 parentWidget()->property("smSnapDragging").toBool() 查询。
    bool isDragging() const { return m_dragging; }

signals:
    // [INV-SNAP-LAZY-PANE-REBUILD] 拖拽结束（mouseRelease）后 emit 一次。
    // MainWindow::createTab 接到本信号后调用各 pane 完成最终重排，
    // 与 resizeEvent 跳过路径配对。与 splitterMoved 不同，本信号只在
    // 拖拽真正结束时触发一次，不会在拖拽过程中连续 emit。
    void dragFinished();

protected:
    QSplitterHandle* createHandle() override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    // 阻止 QSplitter 把 overlay 当作 pane 注册
    void childEvent(QChildEvent* event) override;

private:
    friend class SnapSplitterHandle;

    void beginDrag();
    void endDrag();
    void onSplitterMoved(int pos, int index);
    void syncOverlayGeometry();
    void ensureOverlay();

    SnapOverlay* m_overlay = nullptr;
    bool m_snapping = false;   // INV-SNAP-NO-RECURSION guard
    bool m_dragging = false;
};

} // namespace app

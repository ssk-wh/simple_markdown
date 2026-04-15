// src/app/SnapSplitter.h
//
// Spec: specs/模块-app/13-分隔条吸附刻度.md
// Invariants enforced here:
//   INV-SNAP-THRESHOLD, INV-SNAP-TARGETS, INV-SNAP-VISIBLE-ON-DRAG,
//   INV-SNAP-TRANSPARENT, INV-SNAP-THEME, INV-SNAP-NO-RECURSION
// Last synced: 2026-04-15
#pragma once

#include <QSplitter>
#include <QSplitterHandle>
#include <QColor>

class QWidget;

namespace app {

class SnapOverlay;

// Horizontal QSplitter with 1/4, 1/2, 3/4 snap guides shown while dragging
// the middle handle. Thin wrapper — identical API to QSplitter for callers.
class SnapSplitter : public QSplitter {
    Q_OBJECT
public:
    explicit SnapSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);

    // Called by MainWindow::applyTheme to keep guide color in sync.
    void setAccentColor(const QColor& color);

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

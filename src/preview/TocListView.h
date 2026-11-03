// src/preview/TocListView.h
//
// Spec: specs/模块-preview/07-TOC面板.md（虚拟化列表，INV-TOC-VIRTUAL）
// Last synced: 2026-11-02
#pragma once

#include <QAbstractScrollArea>
#include <QSet>
#include <QVector>

#include "Theme.h"

struct TocEntry;

// 虚拟化目录列表：只绘制可视行，万级目录项也能即时显示/滚动/点击。
// 数据由 TocPanel 持有，本控件仅持有指针与「可见条目索引表」。
class TocListView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit TocListView(QWidget* parent = nullptr);

    struct Source {
        const QVector<TocEntry>* entries = nullptr;
        const QVector<bool>* hasChild = nullptr;
        const QSet<int>* collapsed = nullptr;
        const QSet<int>* highlighted = nullptr;
        const int* focusIdx = nullptr;
    };
    void setSource(const Source& src) { m_src = src; }
    void setTheme(const Theme& theme);
    // 数据/折叠状态变化后调用：visibleIdx 为当前可见条目的 entry 序号（升序）
    void setVisibleRows(const QVector<int>& visibleIdx);
    // 滚动使 entry 可见（键盘导航用）
    void ensureEntryVisible(int entryIdx);

    static constexpr int kRowHeight = 28;

signals:
    void entryClicked(int entryIdx);
    void arrowClicked(int entryIdx);
    void contextMenuForEntry(int entryIdx, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    int rowAt(int y) const;
    int arrowLeft(int level) const;
    void syncScrollBar();

    Source m_src;
    Theme m_theme;
    QVector<int> m_rows;
    int m_hoverRow = -1;
};

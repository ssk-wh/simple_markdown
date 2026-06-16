// src/app/TabBarWithAdd.h
//
// Spec: specs/模块-app/04-窗口焦点管理.md — INV-6 「+」按钮独立于 Tab 滚动，始终可见可点
// Spec: specs/模块-app/12-主题插件系统.md — 按钮视觉从 Theme 字段派生
// Last synced: 2026-06-16
//
// Tab 栏组合控件（Chrome/Edge 风格）：
//   QHBoxLayout 容器 = QTabBar（stretch=1，保留原生横向滚动）
//                    + QToolButton「+」（固定宽，钉在右端，永不被 Tab 溢出挤走）
// 点击「+」emit addClicked()，宿主连接到 MainWindow::onNewFile。
// 对外通过 bar() 暴露内部 QTabBar，宿主用它做 addTab/setTabText/setCurrentIndex 等。
//
// 设计动机（INV-6）：早期实现把「+」自绘在「最后一个 Tab 右侧」，Tab 多到溢出时
// QTabBar 启用横向滚动，最后一个 Tab 被滚出可视区，「+」按钮也一起被推走 → 用户
// 看不到新建按钮、只能先关 Tab。容器化后按钮是 Tab 区的固定兄弟，与滚动解耦。
#pragma once

#include <QWidget>

class QTabBar;
class QToolButton;
class QColor;

class TabBarWithAdd : public QWidget
{
    Q_OBJECT
public:
    explicit TabBarWithAdd(QWidget* parent = nullptr);

    // 内部真正的 Tab 栏；宿主用它做所有 QTabBar 操作（addTab / setCurrentIndex / ...）。
    QTabBar* bar() const { return m_bar; }

    // 「+」按钮控件（主要供测试断言可见性 / 几何，见 INV-6 / T-11 / T-12）。
    QToolButton* addButton() const { return m_addBtn; }

    // 让「+」按钮跟随主题变色（fg = 图标色，hoverBg = 悬停底色；传无效色用默认派生）。
    void setAddButtonColors(const QColor& fg, const QColor& hoverBg);

signals:
    void addClicked();

private:
    QTabBar* m_bar = nullptr;
    QToolButton* m_addBtn = nullptr;
};

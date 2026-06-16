// src/app/TabBarWithAdd.cpp
//
// Spec: specs/模块-app/04-窗口焦点管理.md — INV-6 「+」按钮独立于 Tab 滚动，始终可见可点
// Spec: specs/模块-app/12-主题插件系统.md — 按钮视觉从 Theme 字段派生
#include "TabBarWithAdd.h"

#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QSizePolicy>
#include <QTabBar>
#include <QToolButton>

namespace {
constexpr int kAddBtnWidth = 32;
constexpr int kAddBtnIconSize = 12;  // 「+」十字的臂长

// 自绘「+」按钮：细十字线（1.4px RoundCap）+ hover 半透明圆角底，
// 复刻早期 Chrome/Edge 风格，与 Tab 栏其他控件风格保持一致（贴合而非突兀）。
// 仅重写绘制、不新增信号槽，故无需 Q_OBJECT；clicked 信号沿用 QToolButton 基类。
class AddTabButton : public QToolButton {
public:
    explicit AddTabButton(QWidget* parent = nullptr) : QToolButton(parent) {}

    void setColors(const QColor& fg, const QColor& hoverBg)
    {
        m_fg = fg;
        m_hoverBg = hoverBg;
        update();
    }

protected:
    void enterEvent(QEvent*) override { update(); }   // hover 进入重绘底色
    void leaveEvent(QEvent*) override { update(); }   // hover 离开清底色

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect r = rect();

        // hover 态：填浅色 rounded rect（与早期自绘按钮一致）。
        if (underMouse() && isEnabled()) {
            QColor bg = m_hoverBg.isValid() ? m_hoverBg : QColor(0, 0, 0, 26);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r.adjusted(4, 4, -4, -4), 4, 4);
        }

        // 画「+」十字。
        const QColor fg = m_fg.isValid() ? m_fg : palette().color(QPalette::WindowText);
        QPen pen(fg);
        pen.setWidthF(1.4);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);

        const QPoint c = r.center();
        const int half = kAddBtnIconSize / 2;
        p.drawLine(c.x() - half, c.y(), c.x() + half, c.y());
        p.drawLine(c.x(), c.y() - half, c.x(), c.y() + half);
    }

private:
    QColor m_fg;
    QColor m_hoverBg;
};
}  // namespace

TabBarWithAdd::TabBarWithAdd(QWidget* parent)
    : QWidget(parent)
    , m_bar(new QTabBar(this))
    , m_addBtn(new AddTabButton(this))
{
    m_bar->setObjectName(QStringLiteral("tabBar"));

    // 「+」按钮：固定宽、扁平、不抢焦点、垂直填满与 Tab 等高。
    m_addBtn->setObjectName(QStringLiteral("tabAddButton"));
    m_addBtn->setFixedWidth(kAddBtnWidth);
    m_addBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setFocusPolicy(Qt::NoFocus);
    m_addBtn->setAutoRaise(true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // INV-6：Tab 区可伸缩并保留原生横向滚动；「+」按钮固定在右端常驻可见，
    // 不再绑定「最后一个 Tab 的右侧」这一会随滚动移动的锚点。
    layout->addWidget(m_bar, /*stretch=*/1);
    layout->addWidget(m_addBtn, /*stretch=*/0);

    connect(m_addBtn, &QToolButton::clicked, this, &TabBarWithAdd::addClicked);

    // 默认色跟随 palette；宿主切主题时再调 setAddButtonColors 覆盖。
    setAddButtonColors(palette().color(QPalette::WindowText), QColor());
}

void TabBarWithAdd::setAddButtonColors(const QColor& fg, const QColor& hoverBg)
{
    const QColor f = fg.isValid() ? fg : palette().color(QPalette::WindowText);
    QColor h = hoverBg;
    if (!h.isValid()) {
        // hover 底色：深色主题用白色半透明，浅色主题用黑色半透明。
        const bool isDark = palette().color(QPalette::Window).lightnessF() < 0.5;
        h = isDark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 26);
    }
    static_cast<AddTabButton*>(m_addBtn)->setColors(f, h);
}

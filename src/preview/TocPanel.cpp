#include "TocPanel.h"

#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QTimer>
#include <QPainter>

void TocPanelBackground::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_border, 1));
    p.setBrush(m_bg);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
}

TocPanel::TocPanel(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::NoFocus);

    m_toggleBtn = new QPushButton(this);
    m_toggleBtn->setFixedSize(28, 28);
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    m_toggleBtn->setToolTip(tr("Table of Contents"));
    m_toggleBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_toggleBtn, &QPushButton::clicked, this, &TocPanel::toggle);

    m_panel = new TocPanelBackground(parent);
    m_panel->setVisible(false);
    m_panel->setFocusPolicy(Qt::NoFocus);
    m_panel->setAttribute(Qt::WA_NoMousePropagation, true);

    m_scrollArea = new QScrollArea(m_panel);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* listWidget = new QWidget();
    m_listLayout = new QVBoxLayout(listWidget);
    m_listLayout->setContentsMargins(8, 6, 8, 6);
    m_listLayout->setSpacing(1);
    m_scrollArea->setWidget(listWidget);

    QVBoxLayout* panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(m_scrollArea);

    resize(28, 28);

    if (parent)
        parent->installEventFilter(this);

    setTheme(Theme::light());
    show();
    raise();
}

void TocPanel::setEntries(const QVector<TocEntry>& entries)
{
    m_entries = entries;
    m_toggleBtn->setVisible(!entries.isEmpty());
    if (m_panelVisible)
        buildList();
}

void TocPanel::setHighlightedEntries(const QSet<int>& indices)
{
    m_highlightedEntries = indices;
    if (m_panelVisible)
        buildList();
}

void TocPanel::setTheme(const Theme& theme)
{
    m_theme = theme;

    QColor btnBg = theme.isDark ? QColor(60, 63, 65) : QColor(255, 255, 255);
    QColor btnBorder = theme.isDark ? QColor(80, 83, 85) : QColor(210, 210, 210);
    QColor btnHover = theme.isDark ? QColor(75, 78, 80) : QColor(235, 240, 245);
    QColor btnFg = theme.isDark ? QColor(200, 200, 200) : QColor(120, 120, 120);

    m_toggleBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; border: 1px solid %2; border-radius: 5px;"
        "  color: %3; font-size: 15px;"
        "}"
        "QPushButton:hover { background: %4; color: %5; }"
    ).arg(btnBg.name(), btnBorder.name(), btnFg.name(),
          btnHover.name(), theme.previewFg.name()));
    m_toggleBtn->setText(QStringLiteral("\u2261"));

    // 面板背景色通过 paintEvent 绘制
    // 纯色背景
    QColor panelBg = theme.isDark ? QColor(50, 53, 56) : QColor(235, 237, 240);
    QColor panelBorder = theme.isDark ? QColor(75, 78, 82) : QColor(200, 203, 208);
    m_panel->setBgColor(panelBg);
    m_panel->setBorderColor(panelBorder);

    // scrollArea 和内部列表背景透明，跟随面板
    m_scrollArea->setStyleSheet(QString(
        "QScrollArea { background: transparent; }"
        "QWidget { background: transparent; }"
        "QScrollBar:vertical { width: 5px; background: transparent; }"
        "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(theme.isDark ? "rgba(255,255,255,50)" : "rgba(0,0,0,30)"));

    if (m_panelVisible)
        buildList();
}

void TocPanel::reposition()
{
    QWidget* p = parentWidget();
    if (!p) return;

    int btnX = p->width() - m_toggleBtn->width() - 10;
    int btnY = 10;
    move(btnX, btnY);

    int panelW = 260;
    int panelY = btnY + m_toggleBtn->height() + 6;
    int maxH = p->height() - panelY - 10;
    // 面板高度自适应内容：每个条目约 30px + 上下 padding 12px
    int contentH = m_entries.size() * 30 + 12;
    int panelH = qBound(60, contentH, maxH);
    int panelX = p->width() - panelW - 10;
    m_panel->setFixedSize(panelW, panelH);
    m_panel->move(panelX, panelY);
}

void TocPanel::toggle()
{
    m_panelVisible = !m_panelVisible;
    if (m_panelVisible) {
        buildList();
        reposition();
        m_panel->raise();
    }
    m_panel->setVisible(m_panelVisible);
}

void TocPanel::hidePanel()
{
    if (m_panelVisible) {
        m_panelVisible = false;
        m_panel->setVisible(false);
    }
}

void TocPanel::buildList()
{
    QLayoutItem* item;
    while ((item = m_listLayout->takeAt(0))) {
        delete item->widget();
        delete item;
    }

    QColor fg = m_theme.isDark ? QColor(210, 210, 210) : QColor(50, 50, 50);
    QColor subFg = m_theme.isDark ? QColor(170, 170, 170) : QColor(90, 90, 90);
    QColor hoverBg = m_theme.isDark ? QColor(65, 68, 72) : QColor(220, 224, 230);
    QColor hoverFg = m_theme.previewLink;

    for (int i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        QPushButton* btn = new QPushButton(entry.title, m_scrollArea->widget());
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFlat(true);

        int indent = (entry.level - 1) * 14;
        int fontSize = entry.level == 1 ? 12 : 11;
        QColor itemFg = entry.level <= 2 ? fg : subFg;

        // 检查是否是标记条目
        QString normalBg = "transparent";
        if (m_highlightedEntries.contains(i)) {
            normalBg = m_theme.previewHighlightToc.name(QColor::HexArgb);
        }

        btn->setStyleSheet(QString(
            "QPushButton {"
            "  padding: 5px 8px 5px %1px;"
            "  color: %2; font-size: %3px;"
            "  border: none; border-radius: 3px; background: %6;"
            "  text-align: left;"
            "}"
            "QPushButton:hover { background: %4; color: %5; }"
        ).arg(8 + indent).arg(itemFg.name()).arg(fontSize)
         .arg(hoverBg.name(), hoverFg.name(), normalBg));

        int sourceLine = entry.sourceLine;
        connect(btn, &QPushButton::clicked, this, [this, sourceLine]() {
            // 延迟执行，避免在信号处理中销毁发送者
            QTimer::singleShot(0, this, [this, sourceLine]() {
                emit headingClicked(sourceLine);
                hidePanel();
            });
        });
        m_listLayout->addWidget(btn);
    }

    reposition();
}

bool TocPanel::eventFilter(QObject* obj, QEvent* event)
{
    // 父控件 resize → 重新定位
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
        return false;
    }

    // 点击面板外 → 收起
    if (m_panelVisible && event->type() == QEvent::MouseButtonPress) {
        QWidget* target = qobject_cast<QWidget*>(obj);
        if (target && target != m_toggleBtn
            && !m_panel->isAncestorOf(target)
            && target != m_panel) {
            hidePanel();
        }
    }

    return QWidget::eventFilter(obj, event);
}

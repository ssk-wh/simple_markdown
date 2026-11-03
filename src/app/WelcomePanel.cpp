// src/app/WelcomePanel.cpp
//
// Spec: specs/模块-app/22-空白引导页.md
// Last synced: 2026-04-27
#include "WelcomePanel.h"

#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

WelcomePanel::WelcomePanel(QWidget* parent)
    : QWidget(parent)
{
    // 整体居中布局：上 stretch + 标题 + 副标题 + 按钮组 + 快捷键提示 + 下 stretch
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(48, 48, 48, 48);
    outer->setSpacing(0);

    outer->addStretch(1);

    m_titleLabel = new QLabel("SimpleMarkdown", this);
    m_titleLabel->setObjectName("welcomeTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.2);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    outer->addWidget(m_titleLabel);

    outer->addSpacing(16);

    m_subtitleLabel = new QLabel(this);
    m_subtitleLabel->setObjectName("welcomeSubtitle");
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    QFont subFont = m_subtitleLabel->font();
    subFont.setPointSizeF(subFont.pointSizeF() * 1.1);
    m_subtitleLabel->setFont(subFont);
    outer->addWidget(m_subtitleLabel);

    outer->addSpacing(36);

    // 按钮组：横向排列居中
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(12);
    btnRow->addStretch(1);

    m_openFileBtn = new QPushButton(this);
    m_openFileBtn->setObjectName("welcomeBtnPrimary");
    m_openFileBtn->setCursor(Qt::PointingHandCursor);
    m_openFileBtn->setMinimumSize(160, 44);
    btnRow->addWidget(m_openFileBtn);

    m_openFolderBtn = new QPushButton(this);
    m_openFolderBtn->setObjectName("welcomeBtnSecondary");
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    m_openFolderBtn->setMinimumSize(160, 44);
    btnRow->addWidget(m_openFolderBtn);

    m_newFileBtn = new QPushButton(this);
    m_newFileBtn->setObjectName("welcomeBtnSecondary");
    m_newFileBtn->setCursor(Qt::PointingHandCursor);
    m_newFileBtn->setMinimumSize(160, 44);
    btnRow->addWidget(m_newFileBtn);

    btnRow->addStretch(1);
    outer->addLayout(btnRow);

    outer->addSpacing(28);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName("welcomeHint");
    m_hintLabel->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_hintLabel);

    outer->addStretch(2);

    // 信号桥接
    connect(m_openFileBtn, &QPushButton::clicked, this, &WelcomePanel::openFileClicked);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &WelcomePanel::openFolderClicked);
    connect(m_newFileBtn, &QPushButton::clicked, this, &WelcomePanel::newFileClicked);

    retranslateUi();
    rebuildStyles();
}

void WelcomePanel::setTheme(const Theme& theme)
{
    m_theme = theme;
    rebuildStyles();
    update();
}

void WelcomePanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void WelcomePanel::retranslateUi()
{
    // INV-EMPTY-I18N
    m_subtitleLabel->setText(tr("Get started by opening a file or folder"));
    m_openFileBtn->setText(tr("Open File..."));
    m_openFolderBtn->setText(tr("Open Folder..."));
    // 只读查看器没有「新建文件」：隐藏该按钮（信号保留以兼容接口）
    m_newFileBtn->hide();
    m_hintLabel->setText(tr("Shortcuts: Ctrl+O Open, Ctrl+B Toggle Sidebar, Ctrl+F Find"));
}

void WelcomePanel::rebuildStyles()
{
    // INV-EMPTY-THEME：颜色派生自 Theme
    const QColor bg = m_theme.previewBg;
    const QColor fg = m_theme.previewFg;
    const QColor accent = m_theme.accentColor;
    // 副标题/提示用相对淡的字色：与 fg 在 bg 上做混合
    auto blend = [](const QColor& a, const QColor& b, qreal t) {
        return QColor(
            int(a.red()   * (1 - t) + b.red()   * t),
            int(a.green() * (1 - t) + b.green() * t),
            int(a.blue()  * (1 - t) + b.blue()  * t)
        );
    };
    const QColor sub = blend(fg, bg, 0.45);
    const QColor btnSecondaryBg = blend(bg, fg, 0.06);

    setStyleSheet(QString(
        "WelcomePanel { background: %1; }"
        "QLabel#welcomeTitle { color: %2; }"
        "QLabel#welcomeSubtitle { color: %3; }"
        "QLabel#welcomeHint { color: %3; font-size: 11pt; }"
        "QPushButton#welcomeBtnPrimary {"
        "  background: %4; color: white; border: none; border-radius: 6px;"
        "  padding: 10px 22px; font-size: 11pt; font-weight: 500;"
        "}"
        "QPushButton#welcomeBtnPrimary:hover { background: %5; }"
        "QPushButton#welcomeBtnSecondary {"
        "  background: %6; color: %2; border: 1px solid %7; border-radius: 6px;"
        "  padding: 10px 22px; font-size: 11pt;"
        "}"
        "QPushButton#welcomeBtnSecondary:hover { border-color: %4; color: %4; }"
    ).arg(bg.name(),
          fg.name(),
          sub.name(),
          accent.name(),
          accent.darker(110).name(),
          btnSecondaryBg.name(),
          blend(fg, bg, 0.7).name()));
}

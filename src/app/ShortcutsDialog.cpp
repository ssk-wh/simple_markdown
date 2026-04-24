//
// Spec: specs/模块-app/07-快捷键弹窗.md
// Invariants enforced here: INV-1, INV-2, INV-3, INV-4
// Depends: specs/横切关注点/30-主题系统.md (INV-1, INV-4)
// Last synced: 2026-04-13

#include "ShortcutsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QPalette>

ShortcutsDialog::ShortcutsDialog(const Theme& theme, QWidget* parent)
    : QDialog(parent)
    , m_theme(theme)
{
    // [Spec 07 INV-1][Spec 30 INV-4] QDialog 的 "background" 样式表属性
    // 默认通过 palette 绘制，样式表不会生效。必须显式开启 WA_StyledBackground，
    // 否则深色主题下窗口会是白底（即使其他子控件变深）。
    setAttribute(Qt::WA_StyledBackground, true);

    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(700, 500);
    setupUI();
    populateShortcuts();
    applyTheme();
}

void ShortcutsDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // 搜索栏
    auto* searchLayout = new QHBoxLayout();
    auto* searchLabel = new QLabel(tr("Search:"));
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(tr("Type to filter shortcuts..."));
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(m_searchEdit);
    mainLayout->addLayout(searchLayout);

    // 快捷键表格
    m_table = new QTableWidget();
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Category"), tr("Action"), tr("Shortcut")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    mainLayout->addWidget(m_table);

    // 关闭按钮
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    // 搜索过滤
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ShortcutsDialog::filterShortcuts);
}

void ShortcutsDialog::populateShortcuts()
{
    m_shortcuts = {
        // 文件操作
        {tr("File"), tr("New File"), "Ctrl+N"},
        {tr("File"), tr("Open File"), "Ctrl+O"},
        {tr("File"), tr("Save"), "Ctrl+S"},
        {tr("File"), tr("Save As"), "Ctrl+Shift+S"},
        {tr("File"), tr("Export HTML"), ""},
        {tr("File"), tr("Export PDF"), ""},
        {tr("File"), tr("Print"), "Ctrl+P"},
        {tr("File"), tr("Close Tab"), "Ctrl+W"},
        {tr("File"), tr("Exit"), "Ctrl+Q"},

        // 编辑操作
        {tr("Edit"), tr("Undo"), "Ctrl+Z"},
        {tr("Edit"), tr("Redo"), "Ctrl+Y"},
        {tr("Edit"), tr("Find"), "Ctrl+F"},
        {tr("Edit"), tr("Replace"), "Ctrl+H"},

        // 视图操作
        {tr("View"), tr("Zoom In"), "Ctrl++"},
        {tr("View"), tr("Zoom Out"), "Ctrl+-"},
        {tr("View"), tr("Reset Zoom"), "Ctrl+0"},
        {tr("View"), tr("Toggle Sidebar"), "Ctrl+B"},
        {tr("View"), tr("Focus Mode"), "F11"},
        {tr("View"), tr("Exit Focus Mode"), "Esc / F11"},
        {tr("View"), tr("Toggle Preview"), ""},
        {tr("View"), tr("Toggle TOC"), ""},

        // 编辑器操作
        {tr("Editor"), tr("Zoom In (Mouse)"), "Ctrl+Wheel Up"},
        {tr("Editor"), tr("Zoom Out (Mouse)"), "Ctrl+Wheel Down"},
    };

    m_table->setRowCount(m_shortcuts.size());
    for (int i = 0; i < m_shortcuts.size(); ++i) {
        const auto& info = m_shortcuts[i];
        m_table->setItem(i, 0, new QTableWidgetItem(info.category));
        m_table->setItem(i, 1, new QTableWidgetItem(info.action));
        m_table->setItem(i, 2, new QTableWidgetItem(info.shortcut));
    }
}

void ShortcutsDialog::setTheme(const Theme& theme)
{
    m_theme = theme;
    applyTheme();
}

void ShortcutsDialog::applyTheme()
{
    // [Spec 07 INV-1][Spec 30 INV-1] 所有颜色从 Theme 结构读取，禁止硬编码。
    // 复用 Theme 中 editor/preview 字段（两者在 Theme::light/dark 已配好），不新增字段。
    //
    // [Spec 30 INV-4 / 已知陷阱] 为什么需要 palette + stylesheet 混合方案？
    //
    //   1) QTableWidget / QLineEdit 是 QAbstractScrollArea 子类，它们的 viewport
    //      使用 QPalette::Base 作为背景绘制。单靠 stylesheet 设置
    //      `QTableWidget { background: ... }` 只会改 frame，viewport 仍是白色。
    //      必须用 setPalette 把 Base / AlternateBase / Text 设成主题色。
    //   2) QHeaderView::section / gridline 等细节无法通过 palette 单独调节，
    //      只能靠 stylesheet 覆盖，所以两者都需要。
    //   3) QString::arg 一次链式多于 9 个占位符时，"%10" 会被当作 "%1"+"0"
    //      误解析。所以所有占位符限制在 %1..%9 内。

    const QColor bg         = m_theme.previewBg;
    const QColor fg         = m_theme.previewFg;
    const QColor inputBg    = m_theme.editorBg;
    const QColor border     = m_theme.previewCodeBorder;
    const QColor tableAltBg = m_theme.previewCodeBg;
    const QColor headerBg   = m_theme.previewTableHeaderBg;
    const QColor gridColor  = m_theme.previewTableBorder;
    QColor selColor         = m_theme.editorSelection;
    selColor.setAlpha(255);  // 去掉 alpha，stylesheet 样式表解析需要不透明色

    // ---- 1) palette：解决 viewport（QAbstractScrollArea 的内容区）背景 ----
    QPalette pal = palette();
    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::WindowText, fg);
    pal.setColor(QPalette::Base, bg);                 // 表格 viewport、输入框背景
    pal.setColor(QPalette::AlternateBase, tableAltBg);// 交替行背景
    pal.setColor(QPalette::Text, fg);                 // 表格文字、输入框文字
    pal.setColor(QPalette::Button, inputBg);
    pal.setColor(QPalette::ButtonText, fg);
    pal.setColor(QPalette::Highlight, selColor);
    pal.setColor(QPalette::HighlightedText, fg);
    pal.setColor(QPalette::ToolTipBase, bg);
    pal.setColor(QPalette::ToolTipText, fg);
    setPalette(pal);
    // 同步给子控件，确保它们不从 QApplication 默认 palette 取值
    if (m_searchEdit) m_searchEdit->setPalette(pal);
    if (m_table) {
        m_table->setPalette(pal);
        if (m_table->horizontalHeader()) m_table->horizontalHeader()->setPalette(pal);
        if (m_table->verticalHeader())   m_table->verticalHeader()->setPalette(pal);
        if (m_table->viewport())         m_table->viewport()->setPalette(pal);
    }

    // ---- 1.5) header 单独 setStyleSheet：QHeaderView 在 windowsvista style 下
    // 用原生 QStyle 绘制 section，不读父 dialog 的 stylesheet 也不完全读 palette。
    // 必须直接对 header widget 自己 setStyleSheet 才能生效。详见 Spec 30 INV-5。
    if (m_table && m_table->horizontalHeader()) {
        const QString headerStyle = QString(
            "QHeaderView { background-color: %1; border: none; }"
            "QHeaderView::section {"
            "  background-color: %1;"
            "  color: %2;"
            "  padding: 6px;"
            "  border: none;"
            "  border-bottom: 1px solid %3;"
            "  border-right: 1px solid %3;"
            "}"
            "QHeaderView::section:last { border-right: none; }"
        ).arg(headerBg.name(), fg.name(), border.name());
        m_table->horizontalHeader()->setStyleSheet(headerStyle);
    }

    // ---- 2) stylesheet：解决 header / gridline / border / 滚动条等细节 ----
    //
    // 占位符严格限制在 %1..%9 之内，避免 QString::arg 处理 %10 的 bug。
    // %1=窗口背景, %2=前景文字, %3=输入框背景, %4=边框, %5=交替行, %6=表头, %7=网格, %8=按钮背景, %9=选中背景
    QString style = QString(
        "QDialog { background: %1; color: %2; }"
        "QLabel { color: %2; background: transparent; }"
        "QLineEdit {"
        "  background: %3;"
        "  color: %2;"
        "  border: 1px solid %4;"
        "  border-radius: 3px;"
        "  padding: 4px 6px;"
        "  selection-background-color: %9;"
        "  selection-color: %2;"
        "}"
        "QTableWidget {"
        "  background: %1;"
        "  alternate-background-color: %5;"
        "  color: %2;"
        "  gridline-color: %7;"
        "  border: 1px solid %4;"
        "  selection-background-color: %9;"
        "  selection-color: %2;"
        "}"
        "QTableWidget QHeaderView::section {"
        "  background: %6;"
        "  color: %2;"
        "  padding: 6px;"
        "  border: none;"
        "  border-bottom: 1px solid %4;"
        "  border-right: 1px solid %4;"
        "}"
        "QTableWidget::item { padding: 6px; color: %2; }"
        "QTableWidget::item:selected { background: %9; color: %2; }"
        "QTableCornerButton::section { background: %6; border: none; }"
        "QHeaderView { background: %6; }"
        "QHeaderView::section {"
        "  background: %6;"
        "  color: %2;"
        "  padding: 6px;"
        "  border: none;"
        "  border-bottom: 1px solid %4;"
        "  border-right: 1px solid %4;"
        "}"
        "QPushButton {"
        "  background: %8;"
        "  color: %2;"
        "  border: 1px solid %4;"
        "  border-radius: 3px;"
        "  padding: 6px 16px;"
        "  min-width: 80px;"
        "}"
        "QPushButton:hover { background: %6; }"
        "QPushButton:pressed { background: %6; }"
        "QAbstractScrollArea {"
        "  background: %1;"
        "  color: %2;"
        "}"
        "QAbstractScrollArea > QWidget > QWidget { background: %1; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical {"
        "  background: %4;"
        "  border-radius: 3px;"
        "  min-height: 30px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal {"
        "  background: %4;"
        "  border-radius: 3px;"
        "  min-width: 30px;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
    ).arg(bg.name(),          // %1 窗口背景
          fg.name(),          // %2 前景文字
          inputBg.name(),     // %3 输入框背景
          border.name(),      // %4 边框、滚动条
          tableAltBg.name(),  // %5 交替行背景
          headerBg.name(),    // %6 表头、hover 背景
          gridColor.name(),   // %7 网格线
          inputBg.name(),     // %8 按钮背景
          selColor.name());   // %9 选中行背景

    setStyleSheet(style);
}

void ShortcutsDialog::filterShortcuts(const QString& text)
{
    QString filter = text.toLower();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool match = false;
        if (filter.isEmpty()) {
            match = true;
        } else {
            for (int col = 0; col < 3; ++col) {
                if (m_table->item(i, col)->text().toLower().contains(filter)) {
                    match = true;
                    break;
                }
            }
        }
        m_table->setRowHidden(i, !match);
    }
}

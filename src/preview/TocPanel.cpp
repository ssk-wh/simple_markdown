// src/preview/TocPanel.cpp
//
// Spec: specs/模块-preview/07-TOC面板.md
// Invariants enforced here:
//   INV-TOC-COLLAPSE      树形折叠 + 持久化
//   INV-TOC-WIDTH-AUTO    preferredWidth 按可见条目测算
//   INV-TOC-VISUAL        hover/active 样式（本轮最小版；TOC-5 做视觉对齐）
//   INV-TOC-THEME-ONLY    所有颜色从 Theme 派生
#include "TocPanel.h"

#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QTimer>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <cmath>
#include <QSettings>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QScreen>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <algorithm>

TocPanel::TocPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 标题
    auto* titleLabel = new QLabel(tr("Contents"), this);
    titleLabel->setObjectName("tocTitle");
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLabel->setContentsMargins(12, 8, 8, 8);
    mainLayout->addWidget(titleLabel);

    // 可滚动的条目列表
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* listWidget = new QWidget();
    m_listLayout = new QVBoxLayout(listWidget);
    m_listLayout->setContentsMargins(8, 2, 8, 8);
    m_listLayout->setSpacing(1);
    m_listLayout->addStretch();
    m_scrollArea->setWidget(listWidget);

    mainLayout->addWidget(m_scrollArea);

    // ---- DocInfoCard（Spec INV-TOC-DOCCARD-NO-REPARSE / COLLAPSE-PERSIST） ----
    m_docCard = new QFrame(this);
    m_docCard->setObjectName("docInfoCard");
    m_docCard->setFrameShape(QFrame::NoFrame);
    auto* docLayout = new QVBoxLayout(m_docCard);
    docLayout->setContentsMargins(12, 8, 12, 12);
    docLayout->setSpacing(6);

    m_docCardToggle = new QToolButton(m_docCard);
    m_docCardToggle->setObjectName("docInfoCardToggle");
    m_docCardToggle->setText(tr("Document Info"));
    m_docCardToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_docCardToggle->setAutoRaise(true);
    m_docCardToggle->setCursor(Qt::PointingHandCursor);
    m_docCardToggle->setFocusPolicy(Qt::NoFocus);
    m_docCardToggle->setCheckable(false);
    connect(m_docCardToggle, &QToolButton::clicked, this, [this]() {
        setDocCardCollapsed(!m_docCardCollapsed);
    });
    docLayout->addWidget(m_docCardToggle);

    m_docCardBody = new QLabel(m_docCard);
    m_docCardBody->setObjectName("docInfoCardBody");
    m_docCardBody->setWordWrap(true);
    m_docCardBody->setTextFormat(Qt::RichText);
    m_docCardBody->setText(tr("No document"));
    docLayout->addWidget(m_docCardBody);

    mainLayout->addWidget(m_docCard);

    // 加载折叠态
    {
        QSettings s;
        m_docCardCollapsed = s.value("toc/docCardCollapsed", false).toBool();
    }
    m_docCardBody->setVisible(!m_docCardCollapsed);

    setFocusPolicy(Qt::StrongFocus);
    setTheme(Theme::light());
}

// ---- DocInfoCard 格式化 ----

static QString formatSize(qint64 bytes)
{
    if (bytes < 0) return QStringLiteral("-");
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

static QString formatMtime(const QDateTime& mtime)
{
    if (!mtime.isValid()) return QObject::tr("-");
    const qint64 sec = mtime.secsTo(QDateTime::currentDateTime());
    if (sec < 60) return QObject::tr("just now");
    if (sec < 3600) return QObject::tr("%1 min ago").arg(sec / 60);
    if (sec < 24 * 3600) return QObject::tr("%1 hr ago").arg(sec / 3600);
    if (sec < 7 * 24 * 3600) return QObject::tr("%1 day(s) ago").arg(sec / (24 * 3600));
    return mtime.toString("yyyy-MM-dd HH:mm");
}

void TocPanel::setDocumentInfo(const DocInfo& info)
{
    m_docInfo = info;
    rebuildDocCard();
}

void TocPanel::setDocCardCollapsed(bool v)
{
    if (m_docCardCollapsed == v) return;
    m_docCardCollapsed = v;
    QSettings s;
    s.setValue("toc/docCardCollapsed", v);
    if (m_docCardBody) m_docCardBody->setVisible(!v);
    rebuildDocCard();
}

void TocPanel::rebuildDocCard()
{
    if (!m_docCardBody || !m_docCardToggle) return;

    const QColor accent = m_theme.accentColor;
    const QColor sub = m_theme.previewImageInfoText;

    // 折叠/展开箭头符号用 + / −（与 TOC 同款，确保字体回退正常）
    const QString arrow = m_docCardCollapsed ? QStringLiteral("+") : QStringLiteral("−");
    m_docCardToggle->setText(QStringLiteral("%1 %2").arg(arrow, tr("Document Info")));

    if (m_docCardCollapsed) {
        // 折叠态：只显示一行简略摘要
        QString line = tr("Words: %1").arg(m_docInfo.wordCount);
        if (m_docInfo.mtime.isValid())
            line += QStringLiteral(" · %1").arg(formatMtime(m_docInfo.mtime));
        m_docCardBody->setText(line);
        m_docCardBody->setVisible(false);
    } else {
        // 展开态：详细信息
        QStringList rows;
        rows << tr("Words: %1").arg(m_docInfo.wordCount)
             << tr("Chars: %1 (no space %2)")
                    .arg(m_docInfo.charCount).arg(m_docInfo.charCountNoSpace)
             << tr("Lines: %1").arg(m_docInfo.lineCount)
             << tr("Size: %1").arg(formatSize(m_docInfo.sizeBytes))
             << tr("Modified: %1").arg(formatMtime(m_docInfo.mtime));
        if (!m_docInfo.frontmatterTitle.isEmpty())
            rows << tr("Title: %1").arg(m_docInfo.frontmatterTitle.toHtmlEscaped());
        if (!m_docInfo.frontmatterTags.isEmpty())
            rows << tr("Tags: %1").arg(m_docInfo.frontmatterTags.join(QStringLiteral(", ")).toHtmlEscaped());

        QString html = QStringLiteral("<div style='color:%1;'>").arg(sub.name());
        for (const QString& r : rows)
            html += QStringLiteral("<div>%1</div>").arg(r);
        html += QStringLiteral("</div>");
        m_docCardBody->setText(html);
        m_docCardBody->setVisible(true);
    }

    applyDocCardTheme();
    Q_UNUSED(accent);
}

void TocPanel::applyDocCardTheme()
{
    if (!m_docCard) return;
    const QColor bg = m_theme.previewBg;
    const QColor border = m_theme.previewTableBorder;
    const QColor fg = m_theme.previewHeading;
    const QColor sub = m_theme.previewImageInfoText;

    m_docCard->setStyleSheet(QString(
        "QFrame#docInfoCard { background: %1; border-top: 1px solid %2; }"
        "QToolButton#docInfoCardToggle { color: %3; font-size: 12px; font-weight: bold;"
        "                                 background: transparent; border: none; "
        "                                 padding: 2px 0; text-align: left; }"
        "QToolButton#docInfoCardToggle:hover { color: %4; }"
        "QLabel#docInfoCardBody { color: %5; font-size: 11px; background: transparent; }"
    ).arg(bg.name(), border.name(), fg.name(), m_theme.accentColor.name(), sub.name()));
}

// ---- 父子关系辅助 ----

void TocPanel::recomputeVisibility()
{
    const int n = m_entries.size();
    m_parentIndex = QVector<int>(n, -1);
    m_hasChild = QVector<bool>(n, false);
    m_visible = QVector<bool>(n, true);

    // parentIndex：每个 entry 的最近一个 level < 当前的前驱
    QVector<int> stack;  // 栈存 index
    for (int i = 0; i < n; ++i) {
        const int lvl = m_entries[i].level;
        while (!stack.isEmpty() && m_entries[stack.back()].level >= lvl)
            stack.pop_back();
        m_parentIndex[i] = stack.isEmpty() ? -1 : stack.back();
        if (m_parentIndex[i] >= 0)
            m_hasChild[m_parentIndex[i]] = true;
        stack.append(i);
    }

    // visible：对每个 entry，沿 parent 链回溯，若任一祖先在 m_collapsed，则隐藏
    for (int i = 0; i < n; ++i) {
        int p = m_parentIndex[i];
        bool hidden = false;
        while (p >= 0) {
            if (m_collapsed.contains(p)) {
                hidden = true;
                break;
            }
            p = m_parentIndex[p];
        }
        m_visible[i] = !hidden;
    }
}

bool TocPanel::hasChildren(int idx) const
{
    return idx >= 0 && idx < m_hasChild.size() && m_hasChild[idx];
}

bool TocPanel::isChildOf(int parentIdx, int childIdx) const
{
    int p = (childIdx >= 0 && childIdx < m_parentIndex.size()) ? m_parentIndex[childIdx] : -1;
    while (p >= 0) {
        if (p == parentIdx) return true;
        p = m_parentIndex[p];
    }
    return false;
}

void TocPanel::toggleCollapse(int idx)
{
    if (!hasChildren(idx)) return;
    if (m_collapsed.contains(idx))
        m_collapsed.remove(idx);
    else
        m_collapsed.insert(idx);
    recomputeVisibility();
    saveCollapsed();
    buildList();
    // [INV-TOC-WIDTH-AUTO] 宽度只在 setEntries / setCurrentFileKey 时重算；
    // toggleCollapse 不触发重算、不发信号（T-WIDTH-NO-JITTER 稳定优先）。
}

// ---- 持久化 ----

QString TocPanel::collapseSettingsKey(const QString& fileKey) const
{
    if (fileKey.isEmpty()) return QString();
    // 用 hash 规避 QSettings key 非法字符
    QByteArray h = QCryptographicHash::hash(fileKey.toUtf8(), QCryptographicHash::Md5).toHex();
    return QStringLiteral("toc/collapse/") + QString::fromLatin1(h);
}

void TocPanel::loadCollapsed()
{
    m_collapsed.clear();
    const QString k = collapseSettingsKey(m_currentFileKey);
    if (k.isEmpty()) return;
    QSettings s;
    const QStringList list = s.value(k).toStringList();
    for (const QString& x : list) {
        bool ok = false;
        int v = x.toInt(&ok);
        if (ok) m_collapsed.insert(v);
    }
}

void TocPanel::saveCollapsed()
{
    const QString k = collapseSettingsKey(m_currentFileKey);
    if (k.isEmpty()) return;
    QStringList list;
    for (int v : m_collapsed) list << QString::number(v);
    std::sort(list.begin(), list.end());
    QSettings s;
    s.setValue(k, list);
}

// ---- 入口 ----

void TocPanel::setCurrentFileKey(const QString& key)
{
    if (m_currentFileKey == key) return;
    m_currentFileKey = key;
    loadCollapsed();
    recomputeVisibility();
    buildList();
    emit preferredWidthChanged(preferredWidth());
}

void TocPanel::setEntries(const QVector<TocEntry>& entries)
{
    m_entries = entries;
    if (m_focusIdx >= m_entries.size()) m_focusIdx = -1;
    recomputeVisibility();
    buildList();
    emit preferredWidthChanged(preferredWidth());
}

void TocPanel::setHighlightedEntries(const QSet<int>& indices)
{
    m_highlightedEntries = indices;
    buildList();
}

void TocPanel::setTheme(const Theme& theme)
{
    m_theme = theme;

    // Spec: specs/模块-app/12-主题插件系统.md INV-1（唯一数据源）
    // TocPanel 的所有颜色必须从 Theme 字段派生，不得硬编码。
    const QColor bg = theme.previewBg;
    const QColor borderColor = theme.previewTableBorder;
    const QColor titleFg = theme.previewHeading;

    setStyleSheet(QString(
        "TocPanel { background: %1; border-left: 1px solid %2; }"
    ).arg(bg.name(), borderColor.name()));

    // 标题样式
    if (auto* t = findChild<QLabel*>("tocTitle"))
        t->setStyleSheet(QString(
            "QLabel { color: %1; font-size: 14px; font-weight: bold; background: transparent; }"
        ).arg(titleFg.name()));

    // scrollArea 透明；滚动条从 theme 派生
    const QString scrollThumb = theme.isDark
        ? QStringLiteral("rgba(255,255,255,50)")
        : QStringLiteral("rgba(0,0,0,30)");
    m_scrollArea->setStyleSheet(QString(
        "QScrollArea { background: transparent; }"
        "QWidget { background: transparent; }"
        "QScrollBar:vertical { width: 5px; background: transparent; }"
        "QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(scrollThumb));

    applyDocCardTheme();
    rebuildDocCard();
    buildList();
}

// ---- 宽度自适应 ----

int TocPanel::preferredWidth() const
{
    // Spec: INV-TOC-WIDTH-AUTO
    // 公式：max(text + indent) + padding + scrollbar + margin，夹在 [min_width, screen/5]
    constexpr int kPaddingLeft = 8;
    constexpr int kPaddingRight = 8;
    constexpr int kStepIndent = 14;
    constexpr int kScrollbarReserved = 9;  // 5 + 4 安全余量
    constexpr int kCardMargin = 16;         // contentsMargins 左右总和
    constexpr int kBulletWidth = 14;        // 折叠箭头占位（TOC-2）
    constexpr int kMinWidth = 120;

    const auto* dev = m_scrollArea && m_scrollArea->widget() ? m_scrollArea->widget() : this;

    int maxText = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (i < m_visible.size() && !m_visible[i]) continue;
        const auto& e = m_entries[i];
        QFont f;
        const int pixSize = e.level == 1 ? 14 : 13;
        f.setPixelSize(pixSize);
        QFontMetricsF fm(f, const_cast<QWidget*>(dev));
        qreal advance = fm.horizontalAdvance(e.title);
        // headless CI（如 Linux GitHub Actions）中字体度量可能返回 0；
        // 此时降级用字符数 × 像素字号 × 0.6 粗估宽度，确保长短标题可区分。
        if (advance <= 0.0 && !e.title.isEmpty()) {
            advance = e.title.size() * pixSize * 0.6;
        }
        int tw = static_cast<int>(std::ceil(advance));
        int indent = (e.level - 1) * kStepIndent;
        maxText = std::max(maxText, tw + indent);
    }
    int content = maxText + kPaddingLeft + kPaddingRight + kBulletWidth + kScrollbarReserved + kCardMargin;

    // 屏幕上限（Qt 5.12 无 QWidget::screen，用 QGuiApplication 兜底）
    // 注：测试/headless 环境下 panel 未 show()，mapToGlobal 行为不确定，screenAt 可能
    // 返回极小虚拟屏幕（甚至小于 kMinWidth*8 = 960px）；此时若按 screen/8 计算 maxW，
    // std::min(content, maxW) 会把内容塌到 maxW，最终 std::max(kMinWidth, maxW) 仍是
    // kMinWidth — 长短标题、折叠/展开全部夹成同值，触发 T-WIDTH-1/2、T-COLLAPSE-5 误报。
    const QScreen* scr = QGuiApplication::screenAt(
        const_cast<TocPanel*>(this)->mapToGlobal(QPoint(0, 0)));
    if (!scr) scr = QGuiApplication::primaryScreen();
    constexpr int kFallbackMaxWidth = 400;  // 屏幕信息不可用时的上限默认值
    int maxW = kFallbackMaxWidth;
    if (scr) maxW = scr->availableGeometry().width() / 8;

    // 保证 upper 至少不低于 kFallbackMaxWidth，避免 maxW 塌穿 kMinWidth 吞掉长短内容差异。
    // 用户可见的 splitter 宽度仍由 MainWindow::applyTocPreferredWidth 按 screen/8 二次
    // 夹紧（INV-TOC-WIDTH-MAX），所以放宽 preferredWidth 上界不会改变主流屏视觉效果。
    int upper = std::max(maxW, kFallbackMaxWidth);
    int w = std::max(kMinWidth, std::min(content, upper));
    return w;
}

// ---- 键盘导航 ----

void TocPanel::keyPressEvent(QKeyEvent* event)
{
    if (m_entries.isEmpty()) { QWidget::keyPressEvent(event); return; }

    auto visibleList = [this]() {
        QVector<int> out;
        for (int i = 0; i < m_entries.size(); ++i)
            if (i >= m_visible.size() || m_visible[i]) out.append(i);
        return out;
    };

    auto list = visibleList();
    int cur = m_focusIdx;
    int pos = list.indexOf(cur);

    switch (event->key()) {
    case Qt::Key_Down: {
        if (pos < 0) m_focusIdx = list.value(0, -1);
        else if (pos + 1 < list.size()) m_focusIdx = list[pos + 1];
        buildList();
        return;
    }
    case Qt::Key_Up: {
        if (pos < 0) m_focusIdx = list.value(list.size() - 1, -1);
        else if (pos - 1 >= 0) m_focusIdx = list[pos - 1];
        buildList();
        return;
    }
    case Qt::Key_Left: {
        // 收起当前节点或跳到父节点
        if (m_focusIdx >= 0) {
            if (hasChildren(m_focusIdx) && !m_collapsed.contains(m_focusIdx)) {
                toggleCollapse(m_focusIdx);
            } else if (m_parentIndex.value(m_focusIdx, -1) >= 0) {
                m_focusIdx = m_parentIndex[m_focusIdx];
                buildList();
            }
        }
        return;
    }
    case Qt::Key_Right: {
        if (m_focusIdx >= 0 && hasChildren(m_focusIdx) && m_collapsed.contains(m_focusIdx)) {
            toggleCollapse(m_focusIdx);
        }
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (m_focusIdx >= 0 && m_focusIdx < m_entries.size())
            emit headingClicked(m_entries[m_focusIdx].sourceLine);
        return;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

// ---- 自绘 bullet 图标（Spec INV-TOC-VISUAL） ----
// 画一个 size×size 的实心圆；active=true 时在外围画半透明光环。
// 使用高 DPR 以在高 DPI 屏幕上不模糊。
static QIcon makeBulletIcon(const QColor& color, int size, bool glow, const QColor& glowColor)
{
    const qreal dpr = qApp->devicePixelRatio();
    const int totalSize = 16;                                   // 固定 icon 画布，预留光晕空间
    const int px = qRound(totalSize * dpr);
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF center(totalSize / 2.0, totalSize / 2.0);
    if (glow) {
        // 柔光环：半径 = size/2 + 3
        QColor g = glowColor;
        g.setAlpha(72);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(center, size / 2.0 + 3, size / 2.0 + 3);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(center, size / 2.0, size / 2.0);
    p.end();
    return QIcon(pm);
}

// ---- 构建列表 ----

void TocPanel::buildList()
{
    // 清空现有条目（保留 stretch）
    while (m_listLayout->count() > 1) {
        QLayoutItem* item = m_listLayout->takeAt(0);
        delete item->widget();
        delete item;
    }

    if (m_visible.size() != m_entries.size()) recomputeVisibility();

    // 颜色全部从 Theme 派生（Spec INV-TOC-THEME-ONLY / INV-1）
    const QColor fg = m_theme.previewFg;
    const QColor subFg = m_theme.previewImageInfoText;       // 次级标题色（H3+）
    // hover：借用 previewHighlightToc 为 active 背景；hover 用更淡的背景（半透明 accent）
    const QColor accent = m_theme.accentColor;
    // 使用统一 hover 背景色
    QString hoverBgStr = m_theme.hoverBgCss();
    // active 底（滚动焦点对应条目）
    const QColor activeBg = m_theme.previewHighlightToc;
    const QColor activeFg = m_theme.previewHeading;

    // bullet：默认用边框色衍生；active 用柠檬黄（浅色主题）或 accent（深色主题）
    // Theme v1 没有独立的 accentSecondary 字段，这里用主题判断作为 fallback。
    // TOC-5 视觉规格，Spec INV-TOC-THEME-ONLY 允许通过 Theme 派生。
    QColor bulletDefault = m_theme.previewTableBorder;
    // 标记圆点使用 accentColor 加深，确保可辨识
    QColor bulletActive = m_theme.isDark ? accent.lighter(130) : accent.darker(120);

    // mock 视觉规格（Spec INV-TOC-VISUAL）
    constexpr int kRowPaddingV = 5;
    constexpr int kRowPaddingH = 8;
    constexpr int kStepIndent = 12;         // lvl2 多 12px 左 padding
    constexpr int kArrowWidth = 14;
    constexpr int kBulletLvl1 = 6;
    constexpr int kBulletLvl2 = 5;
    constexpr int kGap = 2;                  // 条目行间距
    m_listLayout->setSpacing(kGap);

    for (int i = 0; i < m_entries.size(); ++i) {
        if (i < m_visible.size() && !m_visible[i]) continue;
        const auto& entry = m_entries[i];

        // 行容器
        auto* row = new QWidget(m_scrollArea->widget());
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        // 字号：lvl1=14, lvl>=2=13 —— 用 logical px，Qt 自动 DPI 缩放
        // （mock 原 12.5/12 在 96dpi 上偏小，Qt widget 环境中上调 1.5px 更易读）
        const int fontSize = entry.level == 1 ? 14 : 13;
        const QColor itemFg = entry.level <= 2 ? fg : subFg;
        const bool isActive = m_highlightedEntries.contains(i);

        // 折叠箭头按钮（有子才可点）
        const bool hasKids = hasChildren(i);
        const bool isCollapsed = m_collapsed.contains(i);
        auto* arrow = new QToolButton(row);
        arrow->setAutoRaise(true);
        arrow->setFixedSize(kArrowWidth, 18);
        arrow->setCursor(Qt::PointingHandCursor);
        arrow->setFocusPolicy(Qt::NoFocus);
        arrow->setStyleSheet(QString(
            "QToolButton { border: none; background: transparent; color: %1; "
            "              font-size: 12px; font-weight: bold; padding: 0; margin: 0; }"
            "QToolButton:hover { color: %2; }"
        ).arg(itemFg.name(), accent.name()));
        if (hasKids) {
            arrow->setText(isCollapsed ? QStringLiteral("+") : QStringLiteral("−"));
            arrow->setToolTip(isCollapsed ? tr("Expand") : tr("Collapse"));
            const int idx = i;
            connect(arrow, &QToolButton::clicked, this, [this, idx]() { toggleCollapse(idx); });
        } else {
            arrow->setText(QString());
            arrow->setEnabled(false);
        }

        // 条目按钮（带 bullet icon）
        auto* btn = new QPushButton(row);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFlat(true);
        btn->setText(entry.title);

        // bullet：lvl1 6px、lvl>=2 5px
        const int bulletSize = entry.level == 1 ? kBulletLvl1 : kBulletLvl2;
        const QColor bulletCol = isActive ? bulletActive : bulletDefault;
        btn->setIcon(makeBulletIcon(bulletCol, bulletSize, isActive, bulletActive));
        btn->setIconSize(QSize(16, 16));

        // 缩进：lvl1 基础 padding 8；lvl>=2 多 (level-1)*12
        const int padLeft = kRowPaddingH + qMax(0, (entry.level - 1) * kStepIndent);

        // 高亮态（active）：仅圆点变色 + 文字加粗，不加整行背景色
        QColor btnFg = itemFg;
        QString fontWeight = QStringLiteral("normal");
        QString normalBg = QStringLiteral("transparent");
        if (isActive) {
            fontWeight = QStringLiteral("600");
        }

        // 焦点环（键盘）
        // 始终不画边框（避免任何焦点/选中边框）
        QString borderRule = QStringLiteral("none");

        // hover 只改背景不改字色（Spec T-VISUAL-2）
        btn->setStyleSheet(QString(
            "QPushButton {"
            "  padding: %1px %2px %1px %3px;"
            "  color: %4; font-size: %5px; font-weight: %6;"
            "  border: %7; border-radius: 7px; background: %8;"
            "  text-align: left;"
            "}"
            "QPushButton:hover { background: %9; }"
            "QPushButton:focus { outline: none; }"
        ).arg(kRowPaddingV).arg(kRowPaddingH).arg(padLeft)
         .arg(btnFg.name()).arg(fontSize).arg(fontWeight)
         .arg(borderRule, normalBg, hoverBgStr));

        const int sourceLine = entry.sourceLine;
        const int idx = i;
        connect(btn, &QPushButton::clicked, this, [this, sourceLine, idx]() {
            m_focusIdx = idx;
            emit headingClicked(sourceLine);
        });

        // 右键菜单：清除章节标记
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, idx, btn](const QPoint& pos) {
            if (!m_highlightedEntries.contains(idx)) return;
            QMenu menu(btn);
            menu.addAction(tr("Clear Section Marks"), [this, idx]() {
                emit clearSectionMarksRequested(idx);
            });
            menu.exec(btn->mapToGlobal(pos));
        });

        rowLayout->addWidget(arrow, 0);
        rowLayout->addWidget(btn, 1);

        // 插入在 stretch 之前
        m_listLayout->insertWidget(m_listLayout->count() - 1, row);
    }
}

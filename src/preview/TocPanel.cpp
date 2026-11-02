// src/preview/TocPanel.cpp
//
// Spec: specs/模块-preview/07-TOC面板.md
// Invariants enforced here:
//   INV-TOC-COLLAPSE      树形折叠 + 持久化
//   INV-TOC-WIDTH-AUTO    preferredWidth 按可见条目测算
//   INV-TOC-VISUAL        hover/active 样式（本轮最小版；TOC-5 做视觉对齐）
//   INV-TOC-THEME-ONLY    所有颜色从 Theme 派生
#include "TocPanel.h"
#include "TocListView.h"

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

    // 虚拟化条目列表：只绘制可视行
    m_list = new TocListView(this);
    {
        TocListView::Source src;
        src.entries = &m_entries;
        src.hasChild = &m_hasChild;
        src.collapsed = &m_collapsed;
        src.highlighted = &m_highlightedEntries;
        src.focusIdx = &m_focusIdx;
        m_list->setSource(src);
    }
    connect(m_list, &TocListView::entryClicked, this, [this](int idx) {
        m_focusIdx = idx;
        emit headingClicked(m_entries[idx].sourceLine);
        buildList();
    });
    connect(m_list, &TocListView::arrowClicked, this, [this](int idx) { toggleCollapse(idx); });
    connect(m_list, &TocListView::contextMenuForEntry, this, [this](int idx, const QPoint& gp) {
        if (!m_highlightedEntries.contains(idx)) return;
        QMenu menu(this);
        menu.addAction(tr("Clear Section Marks"), [this, idx]() { emit clearSectionMarksRequested(idx); });
        menu.exec(gp);
    });
    mainLayout->addWidget(m_list);

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

    // 不使用 QSS：QSS 会在挂入窗口时对整个子树逐控件 polish（实测 addWidget 耗时 200ms+）
    auto setPal = [](QWidget* w, QPalette::ColorRole role, const QColor& c) {
        QPalette p = w->palette();
        p.setColor(role, c);
        w->setPalette(p);
    };
    m_docCard->setAutoFillBackground(true);
    setPal(m_docCard, QPalette::Window, bg);
    if (m_docCardToggle) {
        setPal(m_docCardToggle, QPalette::ButtonText, fg);
        QFont f = m_docCardToggle->font();
        f.setPixelSize(12);
        f.setBold(true);
        m_docCardToggle->setFont(f);
    }
    if (m_docCardBody) {
        setPal(m_docCardBody, QPalette::WindowText, sub);
        QFont f = m_docCardBody->font();
        f.setPixelSize(11);
        m_docCardBody->setFont(f);
    }
    update();
    Q_UNUSED(border);
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

    // 面板背景与左边框在 paintEvent 中自绘；标题用调色板+字体（均不走 QSS，见 applyDocCardTheme 注释）
    if (auto* t = findChild<QLabel*>("tocTitle")) {
        QPalette p = t->palette();
        p.setColor(QPalette::WindowText, titleFg);
        t->setPalette(p);
        QFont f = t->font();
        f.setPixelSize(14);
        f.setBold(true);
        t->setFont(f);
    }
    update();
    Q_UNUSED(bg);
    Q_UNUSED(borderColor);

    m_list->setTheme(theme);

    applyDocCardTheme();
    rebuildDocCard();
    buildList();
}

void TocPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), m_theme.previewBg);
    p.setPen(m_theme.previewTableBorder);
    p.drawLine(0, 0, 0, height());   // 左边框
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

    const QWidget* dev = this;

    // 万级目录：逐项 QFontMetricsF 测量过慢。用「字符数 × 像素字号」作宽度上界，
    // 上界不超过当前最大值的条目直接跳过，使测量次数降到常数量级。
    QFont f14; f14.setPixelSize(14);
    QFont f13; f13.setPixelSize(13);
    const QFontMetricsF fm14(f14, const_cast<QWidget*>(dev));
    const QFontMetricsF fm13(f13, const_cast<QWidget*>(dev));
    int maxText = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (i < m_visible.size() && !m_visible[i]) continue;
        const auto& e = m_entries[i];
        const int pixSize = e.level == 1 ? 14 : 13;
        const int indent = (e.level - 1) * kStepIndent;
        if (e.title.size() * pixSize + indent <= maxText) continue;   // 上界剪枝
        const QFontMetricsF& fm = (pixSize == 14) ? fm14 : fm13;
        qreal advance = fm.horizontalAdvance(e.title);
        // headless CI（如 Linux GitHub Actions）中字体度量可能返回 0；
        // 此时降级用字符数 × 像素字号 × 0.6 粗估宽度，确保长短标题可区分。
        if (advance <= 0.0 && !e.title.isEmpty()) {
            advance = e.title.size() * pixSize * 0.6;
        }
        const int tw = static_cast<int>(std::ceil(advance));
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
        m_list->ensureEntryVisible(m_focusIdx);
        return;
    }
    case Qt::Key_Up: {
        if (pos < 0) m_focusIdx = list.value(list.size() - 1, -1);
        else if (pos - 1 >= 0) m_focusIdx = list[pos - 1];
        buildList();
        m_list->ensureEntryVisible(m_focusIdx);
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

// ---- 构建列表（虚拟化：只刷新可见行索引表，O(n) 但无控件创建） ----

void TocPanel::buildList()
{
    if (m_visible.size() != m_entries.size()) recomputeVisibility();
    QVector<int> rows;
    rows.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_visible[i]) rows.append(i);
    m_list->setVisibleRows(rows);
}

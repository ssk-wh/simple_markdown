#include "SearchBar.h"

#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QTimer>

static constexpr int kBtnW = 28;
static constexpr int kBtnGap = 2;
static constexpr int kReplBtnW = 42;

SearchBar::SearchBar(QWidget* parent)
    : QWidget(parent)
    , m_theme(Theme::light())
{
    setFixedWidth(420);
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 8, 10, 8);
    mainLayout->setSpacing(6);

    int findBtnsWidth = kBtnW * 6 + kBtnGap * 5;  // 3个导航按钮 + 3个选项按钮

    // 搜索行
    auto* findRow = new QHBoxLayout();
    findRow->setSpacing(6);
    m_findEdit = new QLineEdit();
    m_findEdit->setPlaceholderText(tr("Search..."));
    m_matchInfoLabel = new QLabel();
    m_matchInfoLabel->setFixedWidth(60);
    m_matchInfoLabel->setAlignment(Qt::AlignCenter);

    findRow->addWidget(m_findEdit);
    findRow->addWidget(m_matchInfoLabel);
    findRow->addSpacing(findBtnsWidth);
    mainLayout->addLayout(findRow);

    // 替换行
    m_replaceRow = new QWidget();
    auto* replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(6);
    m_replaceEdit = new QLineEdit();
    m_replaceEdit->setPlaceholderText(tr("Replace..."));
    replaceLayout->addWidget(m_replaceEdit);
    int replBtnsWidth = kReplBtnW * 2 + kBtnGap + 60; // 匹配信息宽度 + 替换按钮
    replaceLayout->addSpacing(replBtnsWidth);
    mainLayout->addWidget(m_replaceRow);
    m_replaceRow->hide();

    // 信号连接
    connect(m_findEdit, &QLineEdit::textChanged, this, &SearchBar::searchTextChanged);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() {
        emit findNext(m_findEdit->text());
    });

    // Escape 关闭——FocusOut 自动隐藏机制已彻底取消（INV-13）：
    // 任何"延迟检查 hasFocus"在密集按钮交互下都存在窗口期 false 触发 hideBar 的隐患，
    // 用户预期"打开搜索栏后保留到主动关闭"。仅保留 Esc 键 / m_btnClose 关闭按钮 /
    // 主动 hideSearchBar API 三条显式关闭路径
    m_findEdit->installEventFilter(this);
    m_replaceEdit->installEventFilter(this);

    applyThemeStyles();
    hide();
}

void SearchBar::applyThemeStyles()
{
    bool dark = m_theme.isDark;

    QColor inputBg = dark ? QColor(60, 60, 60) : QColor(255, 255, 255);
    QColor inputFg = dark ? QColor(220, 220, 220) : QColor(51, 51, 51);
    QColor inputBorder = dark ? QColor(80, 80, 80) : QColor(200, 200, 200);
    QColor labelColor = dark ? QColor(160, 160, 160) : QColor(120, 120, 120);

    QString editStyle = QString(
        "QLineEdit {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 3px;"
        "  padding: 4px 6px;"
        "  font-size: 13px;"
        "}"
        "QLineEdit:focus {"
        "  border-color: %4;"
        "}"
    ).arg(inputBg.name(), inputFg.name(), inputBorder.name(),
          m_theme.accentColor.name());

    m_findEdit->setStyleSheet(editStyle);
    m_replaceEdit->setStyleSheet(editStyle);

    m_matchInfoLabel->setStyleSheet(QString(
        "QLabel { color: %1; font-size: 12px; background: transparent; border: none; }"
    ).arg(labelColor.name()));
}

void SearchBar::setTheme(const Theme& theme)
{
    m_theme = theme;
    applyThemeStyles();
    update();
}

void SearchBar::updateMatchInfo(int currentIndex, int totalMatches)
{
    if (totalMatches == 0) {
        if (m_findEdit->text().isEmpty())
            m_matchInfoLabel->setText("");
        else
            m_matchInfoLabel->setText("0 / 0");
    } else {
        if (currentIndex < 0)
            m_matchInfoLabel->setText(QString("  %1").arg(totalMatches));
        else
            m_matchInfoLabel->setText(QString("%1 / %2").arg(currentIndex + 1).arg(totalMatches));
    }
}

bool SearchBar::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }
        // F3 下一个，Shift+F3 上一个
        if (ke->key() == Qt::Key_F3) {
            if (ke->modifiers() & Qt::ShiftModifier)
                emit findPrev(m_findEdit->text());
            else
                emit findNext(m_findEdit->text());
            return true;
        }
    }

    // [Spec 模块-preview/11 INV-15] 点击外部自动关闭：监听 qApp 的全局 MouseButtonPress，
    // 用全局坐标判断点击位置是否落在 SearchBar 矩形外。不同于已删除的 FocusOut 隐藏
    // （INV-13），坐标判断是同步且确定的，密集按钮点击不会误关
    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QRect myGlobal(mapToGlobal(QPoint(0, 0)), size());
        if (!myGlobal.contains(me->globalPos())) {
            // 延迟到下个事件循环 hideBar——避免在事件分发途中改 widget 显隐状态。
            // 不消费事件：目标 widget 仍正常收到点击（如点编辑器内容能定位光标）
            QTimer::singleShot(0, this, [this]() {
                if (isVisible()) hideBar();
            });
        }
    }

    // [Spec 模块-preview/11 INV-13] FocusOut 自动隐藏机制已删除（2026-05-11 第三轮修复）：
    // 在密集按钮点击场景下任何 FocusOut 延迟检查都不可靠
    return QWidget::eventFilter(obj, event);
}

QString SearchBar::searchText() const
{
    return m_findEdit->text();
}

void SearchBar::keepFocus()
{
    // FocusOut 自动隐藏机制已删除（INV-13），keepFocus 只负责让 m_findEdit 重获焦点，
    // 让用户可继续在搜索框输入或用 F3 / Shift+F3 跳转
    m_findEdit->setFocus();
}

void SearchBar::showSearch()
{
    m_replaceVisible = false;
    m_replaceRow->hide();
    adjustSize();
    QWidget* p = parentWidget();
    move(p->width() - width() - 20, 10);
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    // [INV-15] 装全局 filter 监听外部点击；Qt 对同一 object 重复 install 会自动去重
    qApp->installEventFilter(this);
}

void SearchBar::showReplace()
{
    m_replaceVisible = true;
    m_replaceRow->show();
    adjustSize();
    QWidget* p = parentWidget();
    move(p->width() - width() - 20, 10);
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    // [INV-15] 同 showSearch
    qApp->installEventFilter(this);
}

void SearchBar::hideBar()
{
    // [INV-15] 隐藏前先 remove 全局 filter，避免隐藏过程中再次触发外部点击逻辑
    qApp->removeEventFilter(this);

    // 判定时机必须在 hide() 之前——hide() 后 m_findEdit 跟着隐藏，hasFocus 必然为 false。
    // 仅当焦点仍在搜索栏内部控件时把焦点送回宿主——这是 Esc / 关闭按钮关闭的预期行为。
    // 若焦点已在别处（如 INV-15 点击外部触发：用户点击的新 widget 已 setFocus 自己），
    // 不再抢回——否则会让 Ctrl+F 路由判断的 focusInXxx 在 next loop 仍是旧侧
    // （用户原报告：编辑器 Ctrl+F 后单击预览，再 Ctrl+F 仍在编辑器——预览搜索栏 hideBar
    // 抢回 editor 焦点反过来 mirror 同样问题：preview SearchBar hideBar 抢回 preview 焦点）
    const bool wasFocusInside =
        (m_findEdit && m_findEdit->hasFocus()) ||
        (m_replaceEdit && m_replaceEdit->hasFocus());

    hide();
    emit closed();
    if (wasFocusInside) parentWidget()->setFocus();
}

void SearchBar::updateButtonRects()
{
    int rowH = kBtnW;
    int rightMargin = 10;
    int topMargin = 8;

    // 搜索行按钮：紧贴右侧
    int x = width() - rightMargin - kBtnW;
    int y = topMargin + 1;

    m_btnClose.rect = QRect(x, y, kBtnW, rowH);
    x -= kBtnW + kBtnGap;
    m_btnNext.rect = QRect(x, y, kBtnW, rowH);
    x -= kBtnW + kBtnGap;
    m_btnPrev.rect = QRect(x, y, kBtnW, rowH);

    // 选项按钮：在导航按钮左侧
    x -= kBtnW + kBtnGap;
    m_btnRegex.rect = QRect(x, y, kBtnW, rowH);
    x -= kBtnW + kBtnGap;
    m_btnWholeWord.rect = QRect(x, y, kBtnW, rowH);
    x -= kBtnW + kBtnGap;
    m_btnCaseSensitive.rect = QRect(x, y, kBtnW, rowH);

    // 替换行按钮
    if (m_replaceVisible) {
        int y2 = topMargin + rowH + 6 + 3;
        int rx = width() - rightMargin;
        rx -= kReplBtnW;
        m_btnReplaceAll.rect = QRect(rx, y2, kReplBtnW, rowH);
        rx -= kBtnGap;
        rx -= kReplBtnW;
        m_btnReplace.rect = QRect(rx, y2, kReplBtnW, rowH);
    }
}

SearchBar::ToolButton* SearchBar::hitTest(const QPoint& pos)
{
    if (m_btnPrev.rect.contains(pos)) return &m_btnPrev;
    if (m_btnNext.rect.contains(pos)) return &m_btnNext;
    if (m_btnClose.rect.contains(pos)) return &m_btnClose;
    if (m_btnCaseSensitive.rect.contains(pos)) return &m_btnCaseSensitive;
    if (m_btnWholeWord.rect.contains(pos)) return &m_btnWholeWord;
    if (m_btnRegex.rect.contains(pos)) return &m_btnRegex;
    if (m_replaceVisible) {
        if (m_btnReplace.rect.contains(pos)) return &m_btnReplace;
        if (m_btnReplaceAll.rect.contains(pos)) return &m_btnReplaceAll;
    }
    return nullptr;
}

void SearchBar::mouseMoveEvent(QMouseEvent* event)
{
    updateButtonRects();
    ToolButton* allBtns[] = {&m_btnPrev, &m_btnNext, &m_btnClose, &m_btnReplace, &m_btnReplaceAll,
                             &m_btnCaseSensitive, &m_btnWholeWord, &m_btnRegex};
    bool needUpdate = false;
    for (auto* btn : allBtns) {
        bool h = btn->rect.contains(event->pos());
        if (h != btn->hovered) {
            btn->hovered = h;
            needUpdate = true;
        }
    }
    if (needUpdate) {
        auto* hit = hitTest(event->pos());
        setCursor(hit ? Qt::PointingHandCursor : Qt::ArrowCursor);
        // 按钮 tooltip
        if (hit == &m_btnPrev) setToolTip(tr("Previous Match (Shift+F3)"));
        else if (hit == &m_btnNext) setToolTip(tr("Next Match (F3)"));
        else if (hit == &m_btnClose) setToolTip(tr("Close (Escape)"));
        else if (hit == &m_btnCaseSensitive) setToolTip(tr("Match Case"));
        else if (hit == &m_btnWholeWord) setToolTip(tr("Match Whole Word"));
        else if (hit == &m_btnRegex) setToolTip(tr("Use Regular Expression"));
        else if (hit == &m_btnReplace) setToolTip(tr("Replace"));
        else if (hit == &m_btnReplaceAll) setToolTip(tr("Replace All"));
        else setToolTip(QString());
        update();
    }
}

void SearchBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    updateButtonRects();
    auto* btn = hitTest(event->pos());
    if (btn) {
        btn->pressed = true;
        update();
    }
}

void SearchBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    updateButtonRects();
    auto* btn = hitTest(event->pos());

    // 重置所有按下状态
    ToolButton* allBtns[] = {&m_btnPrev, &m_btnNext, &m_btnClose, &m_btnReplace, &m_btnReplaceAll,
                             &m_btnCaseSensitive, &m_btnWholeWord, &m_btnRegex};
    for (auto* b : allBtns) b->pressed = false;

    if (btn == &m_btnPrev) emit findPrev(m_findEdit->text());
    else if (btn == &m_btnNext) emit findNext(m_findEdit->text());
    else if (btn == &m_btnClose) hideBar();
    else if (btn == &m_btnReplace) emit replaceNext(m_findEdit->text(), m_replaceEdit->text());
    else if (btn == &m_btnReplaceAll) emit replaceAll(m_findEdit->text(), m_replaceEdit->text());
    else if (btn == &m_btnCaseSensitive) {
        m_caseSensitive = !m_caseSensitive;
        emit searchTextChanged(m_findEdit->text());
    }
    else if (btn == &m_btnWholeWord) {
        m_wholeWord = !m_wholeWord;
        emit searchTextChanged(m_findEdit->text());
    }
    else if (btn == &m_btnRegex) {
        m_regex = !m_regex;
        emit searchTextChanged(m_findEdit->text());
    }

    update();
}

void SearchBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    updateButtonRects();

    bool dark = m_theme.isDark;

    // 面板背景：跟随主题，稍重
    QColor bgColor = dark ? m_theme.editorGutterBg.darker(110) : m_theme.editorGutterBg;
    bgColor.setAlpha(245);
    QColor borderColor = dark ? QColor(64, 64, 64, 200) : QColor(180, 180, 180, 200);

    p.setPen(QPen(borderColor, 1));
    p.setBrush(bgColor);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 5, 5);

    // 按钮样式
    QColor iconColor = dark ? QColor(220, 220, 220) : QColor(80, 80, 80);
    QColor hoverBg = dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 20);
    QColor pressBg = dark ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 40);

    auto drawBtn = [&](const ToolButton& btn) {
        if (btn.pressed) {
            p.setPen(Qt::NoPen);
            p.setBrush(pressBg);
            p.drawRoundedRect(btn.rect, 4, 4);
        } else if (btn.hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(hoverBg);
            p.drawRoundedRect(btn.rect, 4, 4);
        }
    };

    auto drawIcon = [&](const ToolButton& btn, auto drawFunc) {
        drawBtn(btn);
        p.setPen(QPen(iconColor, 1.5));
        p.setBrush(Qt::NoBrush);
        drawFunc(btn.rect);
    };

    // 上一个按钮：<
    drawIcon(m_btnPrev, [&](const QRect& r) {
        int cx = r.center().x(), cy = r.center().y();
        QPolygonF arrow;
        arrow << QPointF(cx + 3, cy - 5) << QPointF(cx - 3, cy) << QPointF(cx + 3, cy + 5);
        p.drawPolyline(arrow);
    });

    // 下一个按钮：>
    drawIcon(m_btnNext, [&](const QRect& r) {
        int cx = r.center().x(), cy = r.center().y();
        QPolygonF arrow;
        arrow << QPointF(cx - 3, cy - 5) << QPointF(cx + 3, cy) << QPointF(cx - 3, cy + 5);
        p.drawPolyline(arrow);
    });

    // 关闭按钮：X
    drawIcon(m_btnClose, [&](const QRect& r) {
        int cx = r.center().x(), cy = r.center().y();
        int s = 4;
        p.drawLine(cx - s, cy - s, cx + s, cy + s);
        p.drawLine(cx + s, cy - s, cx - s, cy + s);
    });

    // 区分大小写按钮：Aa
    QColor accentBg = m_theme.accentColor;
    accentBg.setAlpha(dark ? 100 : 80);
    auto drawOptionBtn = [&](const ToolButton& btn, bool active) {
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(accentBg);
            p.drawRoundedRect(btn.rect.adjusted(2, 2, -2, -2), 4, 4);
        }
        drawBtn(btn);
    };

    drawOptionBtn(m_btnCaseSensitive, m_caseSensitive);
    p.setPen(iconColor);
    QFont smallFont = font();
    smallFont.setPointSize(9);
    p.setFont(smallFont);
    p.drawText(m_btnCaseSensitive.rect, Qt::AlignCenter, "Aa");

    // 全词匹配按钮：|a|
    drawOptionBtn(m_btnWholeWord, m_wholeWord);
    p.drawText(m_btnWholeWord.rect, Qt::AlignCenter, "|a|");

    // 正则表达式按钮：.*
    drawOptionBtn(m_btnRegex, m_regex);
    p.drawText(m_btnRegex.rect, Qt::AlignCenter, ".*");

    // 替换行按钮（文字按钮）
    if (m_replaceVisible) {
        QFont btnFont = font();
        btnFont.setPointSize(9);
        p.setFont(btnFont);

        auto drawTextBtn = [&](const ToolButton& btn, const QString& text) {
            drawBtn(btn);
            p.setPen(iconColor);
            p.drawText(btn.rect, Qt::AlignCenter, text);
        };

        drawTextBtn(m_btnReplace, "替换");
        drawTextBtn(m_btnReplaceAll, "全部");
    }
}

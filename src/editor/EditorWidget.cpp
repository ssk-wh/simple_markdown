#include "EditorWidget.h"
#include "EditorLayout.h"
#include "EditorPainter.h"
#include "EditorInput.h"
#include "SearchBar.h"
#include "SearchWorker.h"
#include "Document.h"
#include "Theme.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFontDatabase>

EditorWidget::EditorWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    m_layout = new EditorLayout(this);
    m_painter = new EditorPainter();
    m_input = new EditorInput(this);

    // Use system monospace font
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    m_layout->setFont(font);

    // Viewport settings
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Create default document
    m_doc = new Document(this);
    m_layout->setDocument(m_doc);

    // 默认启用换行
    m_wordWrap = true;

    connect(m_doc, &Document::textChanged, this, &EditorWidget::onTextChanged);

    // 光标闪烁
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this]() {
        m_cursorVisible = !m_cursorVisible;
        viewport()->update();
    });
    m_cursorBlinkTimer.start(500);

    // 拖选自动滚动
    m_dragScrollTimer.setInterval(50);
    connect(&m_dragScrollTimer, &QTimer::timeout, this, [this]() {
        QPoint mousePos = viewport()->mapFromGlobal(QCursor::pos());
        int margin = 20;
        int scrollStep = (int)m_layout->defaultLineHeight();

        if (mousePos.y() < margin) {
            // 向上滚动
            int delta = qBound(1, (margin - mousePos.y()) / 5, 5);
            verticalScrollBar()->setValue(verticalScrollBar()->value() - scrollStep * delta);
        } else if (mousePos.y() > viewport()->height() - margin) {
            // 向下滚动
            int delta = qBound(1, (mousePos.y() - viewport()->height() + margin) / 5, 5);
            verticalScrollBar()->setValue(verticalScrollBar()->value() + scrollStep * delta);
        }

        // 更新选区到当前鼠标位置
        TextPosition pos = pixelToTextPosition(mousePos);
        m_doc->selection().extendSelection(pos);
        viewport()->update();
    });

    // 搜索栏
    m_searchBar = new SearchBar(this);
    connect(m_searchBar, &SearchBar::findNext, this, &EditorWidget::findNext);
    connect(m_searchBar, &SearchBar::findPrev, this, &EditorWidget::findPrev);
    connect(m_searchBar, &SearchBar::replaceNext, this, &EditorWidget::doReplaceNext);
    connect(m_searchBar, &SearchBar::replaceAll, this, &EditorWidget::doReplaceAll);
    connect(m_searchBar, &SearchBar::searchTextChanged, this, &EditorWidget::onSearchTextChanged);
    connect(m_searchBar, &SearchBar::closed, this, [this]() {
        m_searchMatches.clear();
        m_currentSearchText.clear();
        m_currentMatchIndex = -1;
        viewport()->update();
    });

    // 搜索线程
    m_searchWorker = new SearchWorker();
    m_searchWorker->moveToThread(&m_searchThread);
    connect(&m_searchThread, &QThread::finished, m_searchWorker, &QObject::deleteLater);
    qRegisterMetaType<QVector<QPair<int,int>>>("QVector<QPair<int,int>>");
    connect(m_searchWorker, &SearchWorker::searchFinished,
            this, &EditorWidget::onSearchResultsReady);
    m_searchThread.start();

    // 搜索防抖
    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(100);
    connect(&m_searchDebounce, &QTimer::timeout, this, [this]() {
        if (m_currentSearchText.isEmpty()) {
            m_searchMatches.clear();
            m_currentMatchIndex = -1;
            updateSearchBarMatchInfo();
            viewport()->update();
            m_searchBar->keepFocus();
            return;
        }
        QString fullText = m_doc->text();
        int reqId = ++m_searchRequestId;
        QMetaObject::invokeMethod(m_searchWorker, "search",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, m_currentSearchText),
                                  Q_ARG(QString, fullText),
                                  Q_ARG(int, reqId));
    });

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
}

EditorWidget::~EditorWidget()
{
    m_searchThread.quit();
    m_searchThread.wait();
    delete m_painter;
    delete m_input;
}

void EditorWidget::setDocument(Document* doc)
{
    if (m_doc) {
        disconnect(m_doc, &Document::textChanged, this, &EditorWidget::onTextChanged);
    }

    m_doc = doc;

    if (m_doc) {
        connect(m_doc, &Document::textChanged, this, &EditorWidget::onTextChanged);
    }

    m_layout->setDocument(m_doc);
    m_layout->rebuild();
    updateScrollBars();
    viewport()->update();
}

Document* EditorWidget::document() const
{
    return m_doc;
}

EditorLayout* EditorWidget::editorLayout() const
{
    return m_layout;
}

void EditorWidget::setTheme(const Theme& theme)
{
    m_theme = theme;
    m_painter->setTheme(theme);
    m_layout->setTheme(theme);
    m_searchBar->setTheme(theme);
    viewport()->update();
}

void EditorWidget::setWordWrap(bool enabled)
{
    m_wordWrap = enabled;
    if (enabled) {
        qreal textAreaWidth = viewport()->width() - m_gutterWidth - 16;
        m_layout->setWrapWidth(textAreaWidth > 50 ? textAreaWidth : 50);
    } else {
        m_layout->setWrapWidth(0);
    }

    updateScrollBars();
    ensureCursorVisible();
    viewport()->update();
}

void EditorWidget::setLineSpacing(qreal factor)
{
    m_layout->setLineSpacingFactor(factor);
    updateScrollBars();
    viewport()->update();
}

qreal EditorWidget::lineSpacing() const
{
    return m_layout->lineSpacingFactor();
}

int EditorWidget::gutterWidth() const
{
    return m_gutterWidth;
}

void EditorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // 检测 DPI 变化（切换屏幕时触发重建）
    qreal currentDpr = viewport()->devicePixelRatioF();
    if (!qFuzzyCompare(currentDpr, m_lastDevicePixelRatio)) {
        m_lastDevicePixelRatio = currentDpr;
        if (m_layout && m_doc) {
            m_layout->setFont(font());
            if (m_wordWrap) {
                qreal textAreaWidth = viewport()->width() - m_gutterWidth - 16;
                m_layout->setWrapWidth(textAreaWidth > 50 ? textAreaWidth : 50);
            }
            updateScrollBars();
        }
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing);

    int first = firstVisibleLine();
    int last = lastVisibleLine();
    qreal sy = scrollY();

    // Draw gutter background
    painter.fillRect(0, 0, m_gutterWidth, viewport()->height(), m_theme.editorGutterBg);

    // Draw line numbers
    painter.setPen(m_theme.editorLineNumber);
    QFont gutterFont = m_layout->font();
    gutterFont.setPointSize(gutterFont.pointSize() - 1);
    painter.setFont(gutterFont);
    for (int line = first; line <= last && line < m_layout->lineCount(); ++line) {
        qreal y = m_layout->lineY(line) - sy;
        painter.drawText(QRectF(0, y, m_gutterWidth - 8, m_layout->lineHeight(line)),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(line + 1));
    }

    // Draw separator line
    painter.setPen(m_theme.editorGutterLine);
    painter.drawLine(m_gutterWidth - 1, 0, m_gutterWidth - 1, viewport()->height());

    // Draw text area
    painter.save();
    painter.setClipRect(m_gutterWidth, 0, viewport()->width() - m_gutterWidth, viewport()->height());

    // 获取系统强调色作为选区颜色
    QColor selColor = palette().highlight().color();
    if (!selColor.isValid() || selColor == Qt::white)
        selColor = QColor(0, 51, 153);
    selColor.setAlpha(100);

    m_painter->setSelectionColor(selColor);
    m_painter->paint(&painter, m_layout, m_doc, first, last,
                     m_gutterWidth, sy, scrollX(),
                     m_cursorVisible && hasFocus(),
                     m_doc->selection().cursorPosition(),
                     m_preeditString,
                     m_searchMatches);
    painter.restore();
}

void EditorWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);

    // 自动换行：wrapWidth = 可用文本区域宽度
    if (m_wordWrap) {
        qreal textAreaWidth = viewport()->width() - m_gutterWidth - 16;
        m_layout->setWrapWidth(textAreaWidth > 50 ? textAreaWidth : 50);
    }

    updateScrollBars();

    // 搜索栏跟随右上角
    if (m_searchBar->isVisible()) {
        m_searchBar->move(width() - m_searchBar->width() - 20, 10);
    }
}

void EditorWidget::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    viewport()->update();
}

void EditorWidget::keyPressEvent(QKeyEvent* event)
{
    // 搜索栏打开时不处理文本编辑按键，避免误操作
    if (m_searchBar->isVisible()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    if (m_input->keyPressEvent(event)) {
        m_cursorVisible = true;
        m_cursorBlinkTimer.start(500);  // 重置闪烁
        ensureCursorVisible();
        viewport()->update();
    } else {
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void EditorWidget::focusInEvent(QFocusEvent* event)
{
    m_cursorVisible = true;
    m_cursorBlinkTimer.start(500);
    viewport()->update();
    QAbstractScrollArea::focusInEvent(event);
}

void EditorWidget::focusOutEvent(QFocusEvent* event)
{
    m_cursorBlinkTimer.stop();
    m_cursorVisible = false;
    viewport()->update();
    QAbstractScrollArea::focusOutEvent(event);
}

static bool isWordChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

TextPosition EditorWidget::pixelToTextPosition(const QPoint& pos) const {
    qreal x = pos.x() - m_gutterWidth - 8 + scrollX();  // 减去 gutter 和 margin，加上水平滚动偏移
    qreal y = pos.y() + scrollY();
    return m_layout->hitTest(QPointF(x, y));
}

void EditorWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        setFocus();
        TextPosition pos = pixelToTextPosition(event->pos());
        bool shift = event->modifiers() & Qt::ShiftModifier;
        if (shift) {
            m_doc->selection().extendSelection(pos);
        } else {
            m_doc->selection().setCursorPosition(pos);
        }
        m_mousePressed = true;
        m_cursorVisible = true;
        m_cursorBlinkTimer.start(500);
        viewport()->update();
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void EditorWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_mousePressed && (event->buttons() & Qt::LeftButton)) {
        TextPosition pos = pixelToTextPosition(event->pos());
        m_doc->selection().extendSelection(pos);

        // 鼠标超出视口边界时启动自动滚动
        int margin = 20;
        bool outsideBounds = event->pos().y() < margin
                          || event->pos().y() > viewport()->height() - margin;
        if (outsideBounds && !m_dragScrollTimer.isActive()) {
            m_dragScrollTimer.start();
        } else if (!outsideBounds && m_dragScrollTimer.isActive()) {
            m_dragScrollTimer.stop();
        }

        ensureCursorVisible();
        viewport()->update();
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorWidget::mouseReleaseEvent(QMouseEvent* event) {
    m_mousePressed = false;
    m_dragScrollTimer.stop();
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void EditorWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        TextPosition pos = pixelToTextPosition(event->pos());
        QString lineText = m_doc->lineText(pos.line);

        int wordStart = pos.column;
        int wordEnd = pos.column;

        while (wordStart > 0 && isWordChar(lineText[wordStart - 1]))
            wordStart--;
        while (wordEnd < lineText.length() && isWordChar(lineText[wordEnd]))
            wordEnd++;

        m_doc->selection().setSelection({pos.line, wordStart}, {pos.line, wordEnd});
        viewport()->update();
    }
}

void EditorWidget::ensureCursorVisible()
{
    QRectF cr = m_layout->cursorRect(m_doc->selection().cursorPosition());
    qreal cy = cr.y();
    qreal ch = cr.height();
    qreal sy = scrollY();
    qreal vh = viewport()->height();

    if (cy < sy) {
        verticalScrollBar()->setValue((int)cy);
    } else if (cy + ch > sy + vh) {
        verticalScrollBar()->setValue((int)(cy + ch - vh));
    }

    // 水平方向（不换行模式）
    if (!m_wordWrap) {
        qreal cx = cr.x();
        qreal sx = scrollX();
        qreal vw = viewport()->width() - m_gutterWidth - 16;

        if (cx < sx) {
            horizontalScrollBar()->setValue((int)cx);
        } else if (cx > sx + vw - 10) {
            horizontalScrollBar()->setValue((int)(cx - vw + 10));
        }
    }
}

void EditorWidget::onTextChanged(int offset, int removedLen, int addedLen)
{
    Q_UNUSED(removedLen);
    Q_UNUSED(addedLen);

    // 简化策略：任何文本变化都重建整个布局
    // 增量更新在复杂场景下（删除换行、跨行替换）容易出 bug
    // 对于常规大小的文件（<10000行），rebuild 很快
    m_layout->rebuild();
    updateScrollBars();
    viewport()->update();
}

void EditorWidget::updateScrollBars()
{
    qreal totalH = m_layout->totalHeight();
    qreal viewH = viewport()->height();
    verticalScrollBar()->setRange(0, qMax(0, (int)(totalH - viewH)));
    verticalScrollBar()->setPageStep((int)viewH);
    verticalScrollBar()->setSingleStep((int)m_layout->defaultLineHeight());

    // 水平滚动条（非换行模式）
    if (!m_wordWrap) {
        qreal textAreaW = viewport()->width() - m_gutterWidth - 16;
        qreal contentW = m_layout->maxLineWidth() + 32;  // 加右侧边距
        int hRange = qMax(0, (int)(contentW - textAreaW));
        horizontalScrollBar()->setRange(0, hRange);
        horizontalScrollBar()->setPageStep((int)textAreaW);
        horizontalScrollBar()->setSingleStep(20);
    } else {
        horizontalScrollBar()->setRange(0, 0);
    }
}

void EditorWidget::updateGutterWidth()
{
    int digits = 1;
    int maxLine = m_layout->lineCount();
    while (maxLine >= 10) {
        ++digits;
        maxLine /= 10;
    }
    QFontMetrics fm(m_layout->font());
    m_gutterWidth = fm.horizontalAdvance(QLatin1Char('9')) * (digits + 2) + 8;
}

int EditorWidget::firstVisibleLine() const
{
    return m_layout->lineAtY(scrollY());
}

int EditorWidget::lastVisibleLine() const
{
    return m_layout->lineAtY(scrollY() + viewport()->height());
}

qreal EditorWidget::scrollY() const
{
    return verticalScrollBar()->value();
}

qreal EditorWidget::scrollX() const
{
    return horizontalScrollBar()->value();
}

void EditorWidget::inputMethodEvent(QInputMethodEvent* event)
{
    if (!event->commitString().isEmpty()) {
        m_preeditString.clear();
        m_input->insertText(event->commitString());
        ensureCursorVisible();
    }

    m_preeditString = event->preeditString();

    viewport()->update();
    event->accept();
}

void EditorWidget::showSearchBar()
{
    m_searchBar->showSearch();
}

void EditorWidget::showReplaceBar()
{
    m_searchBar->showReplace();
}

TextPosition EditorWidget::offsetToTextPos(int offset) const
{
    int line = m_doc->offsetToLine(offset);
    int col = offset - m_doc->lineToOffset(line);
    return {line, col};
}

void EditorWidget::onSearchTextChanged(const QString& text)
{
    m_currentSearchText = text;
    m_currentMatchIndex = -1;
    m_searchDebounce.start();
}

void EditorWidget::onSearchResultsReady(QVector<QPair<int,int>> matches, int requestId)
{
    if (requestId != m_searchRequestId)
        return; // 过期结果，丢弃
    m_searchMatches = std::move(matches);
    m_currentMatchIndex = -1;
    updateSearchBarMatchInfo();
    viewport()->update();
}

void EditorWidget::findNext(const QString& text)
{
    if (text.isEmpty()) return;

    // 如果搜索文本变了，同步搜索一次（用户按 Enter 时）
    if (text != m_currentSearchText || m_searchMatches.isEmpty()) {
        m_currentSearchText = text;
        m_searchMatches.clear();
        QString fullText = m_doc->text();
        int pos = 0;
        while ((pos = fullText.indexOf(text, pos, Qt::CaseInsensitive)) != -1) {
            m_searchMatches.append({pos, text.length()});
            pos += text.length();
        }
    }

    if (m_searchMatches.isEmpty()) {
        m_currentMatchIndex = -1;
        updateSearchBarMatchInfo();
        viewport()->update();
        m_searchBar->keepFocus();
        return;
    }

    int cursorOffset = m_doc->lineToOffset(m_doc->selection().cursorPosition().line)
                     + m_doc->selection().cursorPosition().column;

    for (int i = 0; i < m_searchMatches.size(); ++i) {
        if (m_searchMatches[i].first > cursorOffset) {
            m_currentMatchIndex = i;
            TextPosition start = offsetToTextPos(m_searchMatches[i].first);
            TextPosition end = offsetToTextPos(m_searchMatches[i].first + m_searchMatches[i].second);
            m_doc->selection().setSelection(start, end);
            ensureCursorVisible();
            updateSearchBarMatchInfo();
            viewport()->update();
            m_searchBar->keepFocus();
            return;
        }
    }
    // 回绕到文件开头
    m_currentMatchIndex = 0;
    auto& match = m_searchMatches.first();
    TextPosition start = offsetToTextPos(match.first);
    TextPosition end = offsetToTextPos(match.first + match.second);
    m_doc->selection().setSelection(start, end);
    ensureCursorVisible();
    updateSearchBarMatchInfo();
    viewport()->update();
    m_searchBar->keepFocus();
}

void EditorWidget::findPrev(const QString& text)
{
    if (text.isEmpty()) return;

    if (text != m_currentSearchText || m_searchMatches.isEmpty()) {
        m_currentSearchText = text;
        m_searchMatches.clear();
        QString fullText = m_doc->text();
        int pos = 0;
        while ((pos = fullText.indexOf(text, pos, Qt::CaseInsensitive)) != -1) {
            m_searchMatches.append({pos, text.length()});
            pos += text.length();
        }
    }

    if (m_searchMatches.isEmpty()) {
        m_currentMatchIndex = -1;
        updateSearchBarMatchInfo();
        viewport()->update();
        m_searchBar->keepFocus();
        return;
    }

    int cursorOffset = m_doc->lineToOffset(m_doc->selection().cursorPosition().line)
                     + m_doc->selection().cursorPosition().column;

    // 如果有选区，从选区起始位置往前找
    if (m_doc->selection().hasSelection()) {
        TextPosition startPos = m_doc->selection().range().start();
        cursorOffset = m_doc->lineToOffset(startPos.line) + startPos.column;
    }

    for (int i = m_searchMatches.size() - 1; i >= 0; --i) {
        if (m_searchMatches[i].first < cursorOffset) {
            m_currentMatchIndex = i;
            TextPosition start = offsetToTextPos(m_searchMatches[i].first);
            TextPosition end = offsetToTextPos(m_searchMatches[i].first + m_searchMatches[i].second);
            m_doc->selection().setSelection(start, end);
            ensureCursorVisible();
            updateSearchBarMatchInfo();
            viewport()->update();
            m_searchBar->keepFocus();
            return;
        }
    }
    // 回绕到文件末尾
    m_currentMatchIndex = m_searchMatches.size() - 1;
    auto& match = m_searchMatches.last();
    TextPosition start = offsetToTextPos(match.first);
    TextPosition end = offsetToTextPos(match.first + match.second);
    m_doc->selection().setSelection(start, end);
    ensureCursorVisible();
    updateSearchBarMatchInfo();
    viewport()->update();
    m_searchBar->keepFocus();
}

void EditorWidget::doReplaceNext(const QString& find, const QString& replace)
{
    if (m_doc->selection().hasSelection()) {
        auto range = m_doc->selection().range();
        int startOff = m_doc->lineToOffset(range.start().line) + range.start().column;
        int endOff = m_doc->lineToOffset(range.end().line) + range.end().column;
        QString selected = m_doc->textAt(startOff, endOff - startOff);
        if (selected.compare(find, Qt::CaseInsensitive) == 0) {
            m_doc->replace(startOff, endOff - startOff, replace);
            m_doc->selection().setCursorPosition(offsetToTextPos(startOff + replace.length()));
        }
    }
    findNext(find);
}

void EditorWidget::doReplaceAll(const QString& find, const QString& replace)
{
    if (find.isEmpty()) return;
    QString fullText = m_doc->text();
    QString newText = fullText;
    newText.replace(find, replace, Qt::CaseInsensitive);
    if (newText != fullText) {
        m_doc->remove(0, m_doc->length());
        m_doc->insert(0, newText);
    }
    onSearchTextChanged(find);
}

QVariant EditorWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;

    case Qt::ImCursorRectangle: {
        QRectF cr = m_layout->cursorRect(m_doc->selection().cursorPosition());
        cr.moveLeft(cr.x() + m_gutterWidth + 8 - scrollX());
        cr.moveTop(cr.y() - scrollY());
        return cr.toRect();
    }

    case Qt::ImSurroundingText:
        return m_doc->lineText(m_doc->selection().cursorPosition().line);

    case Qt::ImCursorPosition:
        return m_doc->selection().cursorPosition().column;

    case Qt::ImFont:
        return m_layout->font();

    default:
        return QAbstractScrollArea::inputMethodQuery(query);
    }
}

void EditorWidget::updateSearchBarMatchInfo()
{
    m_searchBar->updateMatchInfo(m_currentMatchIndex, m_searchMatches.size());
}

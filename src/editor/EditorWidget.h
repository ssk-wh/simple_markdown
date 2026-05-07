#pragma once
#include <QAbstractScrollArea>
#include <QTimer>
#include <QThread>
#include <QInputMethodEvent>
#include "Selection.h"
#include "Theme.h"

class Document;
class EditorLayout;
class EditorPainter;
class EditorInput;
class GutterRenderer;
class SearchBar;
class SearchWorker;

class EditorWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit EditorWidget(QWidget* parent = nullptr);
    ~EditorWidget();

    void setDocument(Document* doc);
    Document* document() const;
    EditorLayout* editorLayout() const;

    int gutterWidth() const;

    void setTheme(const Theme& theme);

    void ensureCursorVisible();

    void setWordWrap(bool enabled);
    bool wordWrap() const { return m_wordWrap; }
    void setLineSpacing(qreal factor);
    qreal lineSpacing() const;

    void setTypewriterMode(bool enabled);
    bool typewriterMode() const { return m_typewriterMode; }

    // [Spec 模块-app/13 INV-SNAP-LAZY-PANE-REBUILD] 按当前视口宽度强制重排 wrap。
    // 由 MainWindow::createTab 接收 SnapSplitter::dragFinished 信号后调用，
    // 与 resizeEvent 在拖拽态下跳过 setWrapWidth 的路径配对——保证松手时
    // editor 内容与新宽度对齐，不残留旧 wrap 布局。
    void recomputeWrapForCurrentWidth();

    void showSearchBar();
    void showReplaceBar();
    // [Spec 模块-preview/11 INV-9] 互斥：MainWindow 路由切换到预览搜索时调用，
    // 关闭编辑器侧搜索栏并清空搜索匹配
    void hideSearchBar();

    // 在光标处插入图片的 Markdown 语法
    void insertImageMarkdown(const QString& imagePath);

    const QVector<QPair<int,int>>& searchMatches() const { return m_searchMatches; }

    int firstVisibleLine() const;

signals:
    void cursorPositionChanged(int line, int column);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onTextChanged(int offset, int removedLen, int addedLen);
    void onSearchTextChanged(const QString& text);
    void onSearchResultsReady(QVector<QPair<int,int>> matches, int requestId);
    void findNext(const QString& text);
    void findPrev(const QString& text);
    void doReplaceNext(const QString& find, const QString& replace);
    void doReplaceAll(const QString& find, const QString& replace);

private:
    Document* m_doc = nullptr;
    EditorLayout* m_layout = nullptr;
    EditorPainter* m_painter = nullptr;
    EditorInput* m_input = nullptr;
    SearchBar* m_searchBar = nullptr;
    int m_gutterWidth = 50;

    QTimer m_cursorBlinkTimer;
    QTimer m_dragScrollTimer;
    bool m_cursorVisible = true;
    bool m_mousePressed = false;
    bool m_wordWrap = true;
    bool m_typewriterMode = false;
    QString m_preeditString;

    Theme m_theme;
    QVector<QPair<int,int>> m_searchMatches;  // (偏移量, 长度) 对
    QString m_currentSearchText;
    int m_currentMatchIndex = -1;

    // 搜索线程
    QThread m_searchThread;
    SearchWorker* m_searchWorker = nullptr;
    QTimer m_searchDebounce;
    int m_searchRequestId = 0;

    // 空闲时预加载布局
    QTimer m_idlePreloadTimer;
    int m_lastPreloadLine = -1;

    TextPosition pixelToTextPosition(const QPoint& pos) const;
    TextPosition offsetToTextPos(int offset) const;

    void updateScrollBars();
    void updateGutterWidth();
    int lastVisibleLine() const;
    qreal scrollY() const;
    qreal scrollX() const;

    void updateSearchBarMatchInfo();
    qreal m_lastDevicePixelRatio = 0;
};

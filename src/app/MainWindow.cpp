#include "MainWindow.h"
#include "EditorWidget.h"
#include "PreviewWidget.h"
#include "ParseScheduler.h"
#include "ScrollSync.h"
#include "TocPanel.h"
#include "Document.h"
#include "RecentFiles.h"
#include "ChangelogDialog.h"
#include "ShortcutsDialog.h"
#include "EditorLayout.h"
#include "PreviewLayout.h"
#include "FontDefaults.h"

#include <QSplitter>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QShowEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFileInfo>
#include <QShortcut>
#include <QActionGroup>
#include <QLocalServer>
#include <QSettings>
#include <QLocalSocket>
#include <QScrollBar>
#include <QTimer>
#include <QApplication>
#include <QTabBar>
#include <QToolButton>
#include <QPainter>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextDocument>
#include <QDir>
#include <QPageSize>
#include <QDesktopServices>
#include <QUrl>
#include <QLabel>
#include <QStatusBar>
#include "MarkdownParser.h"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

// HTML 导出模板
static const char* kHtmlTemplate = R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>%1</title>
<style>
body { font-family: -apple-system, "Segoe UI", "Microsoft YaHei", sans-serif; max-width: 900px; margin: 0 auto; padding: 2em; color: #333; line-height: 1.6; }
h1, h2, h3, h4, h5, h6 { margin-top: 1.2em; margin-bottom: 0.4em; color: #111; }
h1 { font-size: 2em; border-bottom: 1px solid #eee; padding-bottom: 0.3em; }
h2 { font-size: 1.5em; border-bottom: 1px solid #eee; padding-bottom: 0.3em; }
code { background: #f5f5f5; padding: 0.2em 0.4em; border-radius: 3px; font-size: 0.9em; }
pre { background: #f5f5f5; padding: 1em; border-radius: 6px; white-space: pre-wrap; word-wrap: break-word; }
pre code { background: none; padding: 0; }
blockquote { margin: 0; padding: 0.5em 1em; border-left: 4px solid #ddd; color: #666; }
table { border-collapse: collapse; width: 100%%; }
th, td { border: 1px solid #ddd; padding: 8px 12px; text-align: left; }
th { background: #f5f5f5; font-weight: 600; }
img { max-width: 100%%; }
a { color: #0366d6; }
del { color: #999; }
hr { border: none; border-top: 1px solid #eee; margin: 2em 0; }
</style></head><body>
%2
</body></html>)";

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("SimpleMarkdown");
    resize(1280, 800);

    m_recentFiles = new RecentFiles(this);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->tabBar()->setDrawBase(false);

    m_tocPanel = new TocPanel(this);
    m_tocPanel->setMinimumWidth(160);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->addWidget(m_tabWidget);
    m_mainSplitter->addWidget(m_tocPanel);
    m_mainSplitter->setStretchFactor(0, 1);  // tabWidget 拉伸
    m_mainSplitter->setStretchFactor(1, 0);  // tocPanel 固定宽度
    m_mainSplitter->setCollapsible(1, false);  // 防止 TOC 面板被折叠
    setCentralWidget(m_mainSplitter);

    // TocPanel 点击 → 跳转到当前 tab 的 preview 对应位置
    connect(m_tocPanel, &TocPanel::headingClicked, this, [this](int sourceLine) {
        auto* tab = currentTab();
        if (tab)
            tab->preview->smoothScrollToSourceLine(sourceLine);
    });

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onCloseTab);
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabChanged);
    // 拖拽重排 tab 时同步 m_tabs 顺序
    connect(m_tabWidget->tabBar(), &QTabBar::tabMoved,
            this, [this](int from, int to) {
        if (from >= 0 && from < m_tabs.size() && to >= 0 && to < m_tabs.size())
            m_tabs.move(from, to);
        saveSessionLater();
    });

    // 切换标签时实时保存会话
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::saveSessionLater);

    // Tab 右键菜单
    m_tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabWidget->tabBar(), &QWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        int idx = m_tabWidget->tabBar()->tabAt(pos);
        if (idx < 0) return;
        QMenu menu(this);
        menu.addAction(tr("Close"), [this, idx]() { onCloseTab(idx); });
        menu.addAction(tr("Close Others"), [this, idx]() {
            for (int i = m_tabs.size() - 1; i >= 0; --i)
                if (i != idx) onCloseTab(i);
        });
        menu.addAction(tr("Close to the Left"), [this, idx]() {
            for (int i = idx - 1; i >= 0; --i)
                onCloseTab(i);
        });
        menu.addAction(tr("Close to the Right"), [this, idx]() {
            for (int i = m_tabs.size() - 1; i > idx; --i)
                onCloseTab(i);
        });
        menu.exec(m_tabWidget->tabBar()->mapToGlobal(pos));
    });

    // 延迟保存定时器（debounce 1秒，避免频繁写磁盘）
    m_saveSessionTimer.setSingleShot(true);
    m_saveSessionTimer.setInterval(1000);
    connect(&m_saveSessionTimer, &QTimer::timeout, this, &MainWindow::saveSettings);

    // 监控文件外部修改
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onFileChangedExternally);

    setupMenuBar();
    // 强制 menuBar 使用样式表绘制，避免原生样式画底部分隔线
    menuBar()->setAttribute(Qt::WA_StyledBackground, true);
    setupStatusBar();
    setupDragDrop();
    loadSettings();

    // 安装全局事件过滤器，用于为弹窗设置深色标题栏
    qApp->installEventFilter(this);
}

void MainWindow::setupMenuBar()
{
    // -- File menu --
    QMenu* fileMenu = menuBar()->addMenu(tr("File"));

    QAction* newAct = fileMenu->addAction(tr("New"), this, &MainWindow::onNewFile, QKeySequence::New);
    Q_UNUSED(newAct);

    QAction* openAct = fileMenu->addAction(tr("Open..."), this, &MainWindow::onOpenFile, QKeySequence::Open);
    Q_UNUSED(openAct);

    QAction* saveAct = fileMenu->addAction(tr("Save"), this, &MainWindow::onSaveFile, QKeySequence::Save);
    Q_UNUSED(saveAct);

    QAction* saveAsAct = fileMenu->addAction(tr("Save As..."), this, &MainWindow::onSaveFileAs, QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_S));
    Q_UNUSED(saveAsAct);

    fileMenu->addSeparator();

    m_recentMenu = fileMenu->addMenu(tr("Recent Files"));
    updateRecentFilesMenu();
    connect(m_recentFiles, &RecentFiles::changed,
            this, &MainWindow::updateRecentFilesMenu);

    fileMenu->addSeparator();

    fileMenu->addAction(tr("Export HTML..."), this, &MainWindow::onExportHtml);
    fileMenu->addAction(tr("Export PDF..."), this, &MainWindow::onExportPdf);
    fileMenu->addAction(tr("Print..."), this, &MainWindow::onPrint, QKeySequence::Print);

    fileMenu->addSeparator();

    fileMenu->addAction(tr("Exit"), this, &QWidget::close, QKeySequence::Quit);

    // -- Edit menu --
    QMenu* editMenu = menuBar()->addMenu(tr("Edit"));

    editMenu->addAction(tr("Undo"), [this]() {
        if (auto* tab = currentTab())
            tab->editor->document()->undo();
    }, QKeySequence::Undo);

    editMenu->addAction(tr("Redo"), [this]() {
        if (auto* tab = currentTab())
            tab->editor->document()->redo();
    }, QKeySequence::Redo);

    editMenu->addSeparator();

    editMenu->addAction(tr("Find..."), [this]() {
        if (auto* tab = currentTab())
            tab->editor->showSearchBar();
    }, QKeySequence::Find);

    editMenu->addAction(tr("Replace..."), [this]() {
        if (auto* tab = currentTab())
            tab->editor->showReplaceBar();
    }, QKeySequence(Qt::CTRL + Qt::Key_H));

    // -- View menu --
    QMenu* viewMenu = menuBar()->addMenu(tr("View"));

    QActionGroup* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    m_followSystemThemeAct = viewMenu->addAction(tr("Follow System"));
    m_followSystemThemeAct->setCheckable(true);
    m_followSystemThemeAct->setChecked(true);
    themeGroup->addAction(m_followSystemThemeAct);

    m_lightThemeAct = viewMenu->addAction(tr("Light Theme"));
    m_lightThemeAct->setCheckable(true);
    themeGroup->addAction(m_lightThemeAct);

    m_darkThemeAct = viewMenu->addAction(tr("Dark Theme"));
    m_darkThemeAct->setCheckable(true);
    themeGroup->addAction(m_darkThemeAct);

    connect(m_followSystemThemeAct, &QAction::triggered, this, [this]() {
        applySystemTheme();
    });
    connect(m_lightThemeAct, &QAction::triggered, this, [this]() {
        applyTheme(Theme::light());
    });
    connect(m_darkThemeAct, &QAction::triggered, this, [this]() {
        applyTheme(Theme::dark());
    });

    viewMenu->addSeparator();

    // 自动换行
    m_wordWrapAct = viewMenu->addAction(tr("Word Wrap"));
    m_wordWrapAct->setCheckable(true);
    m_wordWrapAct->setChecked(true);
    connect(m_wordWrapAct, &QAction::triggered, this, [this]() {
        bool checked = m_wordWrapAct->isChecked();
        for (auto& tab : m_tabs) {
            tab.editor->setWordWrap(checked);
            tab.preview->setWordWrap(checked);
        }
    });

    // 行间距
    QMenu* lineSpacingMenu = viewMenu->addMenu(tr("Line Spacing"));
    QActionGroup* spacingGroup = new QActionGroup(this);
    spacingGroup->setExclusive(true);

    struct SpacingOption { const char* label; qreal factor; };
    SpacingOption spacings[] = {
        {"1.0", 1.0}, {"1.2", 1.2}, {"1.5", 1.5}, {"1.8", 1.8}, {"2.0", 2.0}
    };
    for (auto& opt : spacings) {
        QAction* act = lineSpacingMenu->addAction(tr(opt.label));
        act->setCheckable(true);
        if (qFuzzyCompare(opt.factor, 1.0))
            act->setChecked(true);
        spacingGroup->addAction(act);
        m_spacingActions.append(act);
        qreal factor = opt.factor;
        connect(act, &QAction::triggered, this, [this, factor]() {
            m_lineSpacingFactor = factor;
            for (auto& tab : m_tabs)
                tab.editor->setLineSpacing(factor);
        });
    }

    viewMenu->addSeparator();

    viewMenu->addAction(tr("Zoom In"), this, &MainWindow::zoomIn, QKeySequence(Qt::CTRL + Qt::Key_Equal));
    viewMenu->addAction(tr("Zoom Out"), this, &MainWindow::zoomOut, QKeySequence(Qt::CTRL + Qt::Key_Minus));
    viewMenu->addAction(tr("Reset Zoom"), this, &MainWindow::zoomReset, QKeySequence(Qt::CTRL + Qt::Key_0));

    viewMenu->addSeparator();

    m_focusModeAct = viewMenu->addAction(tr("Presentation Mode"));
    m_focusModeAct->setCheckable(true);
    m_focusModeAct->setChecked(false);
    m_focusModeAct->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_focusModeAct, &QAction::triggered, this, &MainWindow::toggleFocusMode);

    viewMenu->addSeparator();

    m_restoreSessionAct = viewMenu->addAction(tr("Restore Last File"));
    m_restoreSessionAct->setCheckable(true);
    m_restoreSessionAct->setChecked(true);

    // -- Settings menu --
    QMenu* settingsMenu = menuBar()->addMenu(tr("Settings"));

    QMenu* languageMenu = settingsMenu->addMenu(tr("Language"));
    QActionGroup* langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);

    m_zhCNAct = languageMenu->addAction(tr("Chinese (Simplified)"));
    m_zhCNAct->setCheckable(true);
    langGroup->addAction(m_zhCNAct);

    m_enUSAct = languageMenu->addAction(tr("English"));
    m_enUSAct->setCheckable(true);
    langGroup->addAction(m_enUSAct);

    // 默认选中中文
    m_zhCNAct->setChecked(true);

    connect(m_zhCNAct, &QAction::triggered, this, [this]() {
        switchLanguage("zh_CN");
    });
    connect(m_enUSAct, &QAction::triggered, this, [this]() {
        switchLanguage("en_US");
    });

    // -- Help menu --
    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));

    helpMenu->addAction(tr("Keyboard Shortcuts"), this, &MainWindow::onShowShortcuts);
    helpMenu->addSeparator();

    helpMenu->addAction(tr("Update History"), this, [this]() {
        ChangelogDialog dialog(m_currentTheme, this);
        dialog.exec();
    });

    helpMenu->addSeparator();

    helpMenu->addAction(tr("About"), this, [this]() {
        // 每个用户可见字段单独用 tr() 包装，保证翻译粒度正确（符合 Spec INV-2）
        QString about = QString(
            "<h2>SimpleMarkdown %1</h2>"
            "<p>%2</p>"
            "<table>"
            "<tr><td><b>%3</b></td><td>pcfan</td></tr>"
            "<tr><td><b>%4</b></td><td>%5</td></tr>"
            "<tr><td><b>%6</b></td><td>%7</td></tr>"
            "</table>"
            "<p>%8: <a href=\"https://github.com/ssk-wh/simple_markdown\">"
            "https://github.com/ssk-wh/simple_markdown</a></p>"
        ).arg(QApplication::applicationVersion(),
              tr("A lightweight cross-platform Markdown editor."),
              tr("Author:"),
              tr("Build Date:"),
              QString(__DATE__),
              tr("Qt Version:"),
              qVersion(),
              tr("Source"));
        QMessageBox::about(this, tr("About SimpleMarkdown"), about);
    });
}

void MainWindow::setupDragDrop()
{
    setAcceptDrops(true);
}

void MainWindow::newTab()
{
    TabData tab = createTab();
    int index = m_tabWidget->addTab(tab.splitter, tr("Untitled"));
    m_tabs.append(tab);
    setTabCloseButton(index);
    m_tabWidget->setCurrentIndex(index);

    // Insert sample text for new empty tabs
    tab.editor->document()->insert(0,
        "# SimpleMarkdown\n"
        "\n"
        "A **lightweight** cross-platform Markdown editor.\n"
    );
    tab.editor->document()->setModified(false);
    // parseNow will be triggered by textChanged → debounce
}

void MainWindow::restoreSession(const QString& requestedFile)
{
    QSettings s;
    bool restore = s.value("session/restoreLastFile", true).toBool();

    // 命令行指定了文件
    if (!requestedFile.isEmpty()) {
        // 先恢复会话中的其他标签页
        if (restore) {
            int count = s.beginReadArray("session/tabs");
            for (int i = 0; i < count; ++i) {
                s.setArrayIndex(i);
                QString fp = s.value("file").toString();
                if (fp == QFileInfo(requestedFile).absoluteFilePath()) continue;
                if (!fp.isEmpty() && QFileInfo::exists(fp)) {
                    openFile(fp);
                    int idx = m_tabWidget->count() - 1;
                    int es = s.value("editorScroll", 0).toInt();
                    int ehs = s.value("editorHScroll", 0).toInt();
                    int ps = s.value("previewScroll", 0).toInt();
                    int phs = s.value("previewHScroll", 0).toInt();
                    int cLine = s.value("cursorLine", 0).toInt();
                    int cCol = s.value("cursorColumn", 0).toInt();
                    QTimer::singleShot(200, this, [this, idx, es, ehs, ps, phs, cLine, cCol]() {
                        if (idx < m_tabs.size()) {
                            m_tabs[idx].editor->document()->selection().setCursorPosition({cLine, cCol});
                            m_tabs[idx].editor->verticalScrollBar()->setValue(es);
                            m_tabs[idx].editor->horizontalScrollBar()->setValue(ehs);
                            m_tabs[idx].preview->verticalScrollBar()->setValue(ps);
                            m_tabs[idx].preview->horizontalScrollBar()->setValue(phs);
                        }
                    });
                }
            }
            s.endArray();
        }
        // 打开命令行指定的文件并恢复其滚动位置
        openFile(requestedFile);
        if (restore) {
            int count = s.beginReadArray("session/tabs");
            for (int i = 0; i < count; ++i) {
                s.setArrayIndex(i);
                if (s.value("file").toString() == QFileInfo(requestedFile).absoluteFilePath()) {
                    int es = s.value("editorScroll", 0).toInt();
                    int ehs = s.value("editorHScroll", 0).toInt();
                    int ps = s.value("previewScroll", 0).toInt();
                    int phs = s.value("previewHScroll", 0).toInt();
                    int cLine = s.value("cursorLine", 0).toInt();
                    int cCol = s.value("cursorColumn", 0).toInt();
                    QTimer::singleShot(200, this, [this, es, ehs, ps, phs, cLine, cCol]() {
                        if (auto* tab = currentTab()) {
                            tab->editor->document()->selection().setCursorPosition({cLine, cCol});
                            tab->editor->ensureCursorVisible();
                            tab->editor->verticalScrollBar()->setValue(es);
                            tab->editor->horizontalScrollBar()->setValue(ehs);
                            tab->preview->verticalScrollBar()->setValue(ps);
                            tab->preview->horizontalScrollBar()->setValue(phs);
                        }
                    });
                    break;
                }
            }
            s.endArray();
        }
        return;
    }

    // 无命令行文件：恢复整个会话
    if (restore) {
        struct TabState {
            int editorScroll, editorHScroll, previewScroll, previewHScroll;
            int cursorLine, cursorColumn;
        };
        int count = s.beginReadArray("session/tabs");
        QVector<TabState> tabStates;
        bool opened = false;
        for (int i = 0; i < count; ++i) {
            s.setArrayIndex(i);
            QString fp = s.value("file").toString();
            if (!fp.isEmpty() && QFileInfo::exists(fp)) {
                openFile(fp);
                tabStates.append({s.value("editorScroll", 0).toInt(),
                                  s.value("editorHScroll", 0).toInt(),
                                  s.value("previewScroll", 0).toInt(),
                                  s.value("previewHScroll", 0).toInt(),
                                  s.value("cursorLine", 0).toInt(),
                                  s.value("cursorColumn", 0).toInt()});
                opened = true;
            }
        }
        s.endArray();

        if (opened) {
            // 恢复当前标签
            int activeTab = s.value("session/activeTab", 0).toInt();
            if (activeTab >= 0 && activeTab < m_tabWidget->count())
                m_tabWidget->setCurrentIndex(activeTab);

            // 延迟恢复所有标签页的光标位置和滚动位置
            QTimer::singleShot(200, this, [this, tabStates]() {
                for (int i = 0; i < tabStates.size() && i < m_tabs.size(); ++i) {
                    const auto& st = tabStates[i];
                    m_tabs[i].editor->document()->selection().setCursorPosition(
                        {st.cursorLine, st.cursorColumn});
                    m_tabs[i].editor->verticalScrollBar()->setValue(st.editorScroll);
                    m_tabs[i].editor->horizontalScrollBar()->setValue(st.editorHScroll);
                    m_tabs[i].preview->verticalScrollBar()->setValue(st.previewScroll);
                    m_tabs[i].preview->horizontalScrollBar()->setValue(st.previewHScroll);
                }
                // 当前标签页需确保光标可见
                if (auto* tab = currentTab())
                    tab->editor->ensureCursorVisible();
            });
            return;
        }
    }

    newTab();
}

void MainWindow::openFile(const QString& path)
{
    // 检查文件是否已在某个标签页打开
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].editor->document()->filePath() == QFileInfo(path).absoluteFilePath()) {
            m_tabWidget->setCurrentIndex(i);

            // [修复] 如果文件已打开，切换到该标签页后必须提升窗口
            // 场景：用户在浏览器中打开 markdown 文件时，应用加载文件但窗口被其他应用遮挡
            // 解决方案：通过以下三个步骤确保窗口被置于最前：
            // 1. setWindowState - 恢复最小化状态并设置活跃状态
            // 2. raise() - 在 Z 序中将窗口提升到最前
            // 3. activateWindow() - 确保窗口获得键盘焦点
            setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            raise();
            activateWindow();
            return;
        }
    }

    // 创建新标签页并加载文件
    TabData tab = createTab();
    int index = m_tabWidget->addTab(tab.splitter, QFileInfo(path).fileName());
    m_tabs.append(tab);
    setTabCloseButton(index);
    m_tabWidget->setCurrentIndex(index);

    tab.editor->document()->loadFromFile(path);
    tab.scheduler->parseNow();

    m_recentFiles->addFile(path);
    updateTabTitle(index);
    watchFile(QFileInfo(path).absoluteFilePath());

    // [修复] 加载新文件后必须提升窗口，确保用户能看到
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();

    saveSessionLater();
}

MainWindow::TabData MainWindow::createTab()
{
    TabData tab;

    tab.splitter = new QSplitter(Qt::Horizontal);
    tab.editor = new EditorWidget(tab.splitter);
    tab.splitter->addWidget(tab.editor);

    tab.preview = new PreviewWidget(tab.splitter);
    tab.splitter->addWidget(tab.preview);
    tab.splitter->setSizes({640, 640});

    // Parse scheduler: connect document -> preview
    tab.scheduler = new ParseScheduler(tab.splitter);
    tab.scheduler->setDocument(tab.editor->document());
    connect(tab.scheduler, &ParseScheduler::astReady,
            tab.preview, &PreviewWidget::updateAst);

    // Scroll sync: editor -> preview
    tab.scrollSync = new ScrollSync(tab.editor, tab.preview, tab.splitter);

    // Apply current settings
    tab.editor->setTheme(m_currentTheme);
    tab.preview->setTheme(m_currentTheme);
    tab.editor->setWordWrap(m_wordWrapAct && m_wordWrapAct->isChecked());
    tab.preview->setWordWrap(m_wordWrapAct && m_wordWrapAct->isChecked());
    tab.editor->setLineSpacing(m_lineSpacingFactor);

    // TOC 信号：仅当此 tab 是当前活跃 tab 时更新全局 TocPanel
    connect(tab.preview, &PreviewWidget::tocEntriesChanged,
            this, [this, preview = tab.preview](const QVector<TocEntry>& entries) {
        if (currentTab() && currentTab()->preview == preview)
            m_tocPanel->setEntries(entries);
    });
    connect(tab.preview, &PreviewWidget::tocHighlightChanged,
            this, [this, preview = tab.preview](const QSet<int>& indices) {
        if (currentTab() && currentTab()->preview == preview)
            m_tocPanel->setHighlightedEntries(indices);
    });

    // [Spec 模块-preview/09] 预览区链接 Ctrl+click
    connect(tab.preview, &PreviewWidget::linkClicked,
            this, [this, editor = tab.editor](const QString& url) {
        onPreviewLinkClicked(url, editor);
    });

    // 预览区右键菜单：在浏览器中打开
    connect(tab.preview, &PreviewWidget::openInBrowserRequested,
            this, [this]() {
        auto* tab = currentTab();
        if (!tab) return;

        QString body = currentMarkdownToHtml();
        if (body.isEmpty()) return;

        QString baseName = QFileInfo(tab->editor->document()->filePath()).completeBaseName();
        if (baseName.isEmpty()) baseName = "untitled";

        QString html = QString(kHtmlTemplate).arg(baseName.toHtmlEscaped(), body);

        // 创建临时 HTML 文件
        QString tempPath = QDir::temp().filePath(baseName + ".html");
        QFile file(tempPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(html.toUtf8());
            file.close();
            QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
        }
    });

    // TocPanel 点击 → 当前 tab 的 preview 跳转
    // （在 MainWindow 构造中统一连接一次即可，不需要每个 tab 连接）

    // Track modifications for tab title
    connect(tab.editor->document(), &Document::modifiedChanged,
            this, [this](bool) {
        int idx = m_tabWidget->currentIndex();
        if (idx >= 0)
            updateTabTitle(idx);
    });

    // 状态栏信号连接
    connectTabStatusBar(tab);

    return tab;
}

void MainWindow::updateTabTitle(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;

    Document* doc = m_tabs[index].editor->document();
    QString title;
    if (doc->filePath().isEmpty()) {
        title = tr("Untitled");
    } else {
        title = QFileInfo(doc->filePath()).fileName();
    }
    if (doc->isModified())
        title = "* " + title;

    m_tabWidget->setTabText(index, title);
    m_tabWidget->setTabToolTip(index, doc->filePath().isEmpty() ? tr("Untitled") : QFileInfo(doc->filePath()).absoluteFilePath());

    // Update window title
    if (index == m_tabWidget->currentIndex())
        setWindowTitle(title + " - SimpleMarkdown");
}

void MainWindow::updateRecentFilesMenu()
{
    m_recentMenu->clear();
    QStringList files = m_recentFiles->files();
    if (files.isEmpty()) {
        m_recentMenu->addAction(tr("(empty)"))->setEnabled(false);
        return;
    }
    for (const QString& file : files) {
        m_recentMenu->addAction(QDir::toNativeSeparators(file), [this, file]() {
            openFile(file);
        });
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear"), m_recentFiles, &RecentFiles::clear);
}

bool MainWindow::isSystemDarkMode() const
{
#ifdef _WIN32
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                  QSettings::NativeFormat);
    return reg.value("AppsUseLightTheme", 1).toInt() == 0;
#else
    QPalette pal = QApplication::palette();
    return pal.color(QPalette::Window).lightness() < 128;
#endif
}

void MainWindow::applySystemTheme()
{
    applyTheme(isSystemDarkMode() ? Theme::dark() : Theme::light());
}

void MainWindow::applyTheme(const Theme& theme)
{
    m_currentTheme = theme;
    for (auto& tab : m_tabs) {
        tab.editor->setTheme(theme);
        tab.preview->setTheme(theme);
    }
    m_tocPanel->setTheme(theme);

    // 主窗口 UI 元素跟随深色/浅色主题（含菜单栏、右键菜单、对话框）
    if (theme.isDark) {
        setStyleSheet(QStringLiteral(
            "QMainWindow { background: #2b2b2b; }"
            // 编辑器和预览区域去掉默认 frame 边框
            "QAbstractScrollArea { border: none; }"
            // 菜单栏
            "QMenuBar { background: #2b2b2b; color: #ccc; border: none; }"
            "QMenuBar::item { padding: 6px 10px; }"
            "QMenuBar::item:selected { background: #3c3f41; border-bottom: 2px solid #4a9eff; }"
            // 菜单（包括右键菜单）
            "QMenu { background: #2b2b2b; color: #ccc; border: 1px solid #555; padding: 4px 0; }"
            "QMenu::item { padding: 6px 24px 6px 32px; }"  // [Spec 模块-app/10-菜单栏样式 INV-1] padding-left = indicator 占位(24) + 约1字符宽(~8)，保证 ✓ 与文字留间距
            "QMenu::item:selected { background: #3c3f41; border-left: 2px solid #4a9eff; }"
            "QMenu::separator { background: #555; height: 1px; margin: 4px 8px; }"
            "QMenu::indicator { width: 16px; height: 16px; margin-right: 8px; }"
            // Tab 栏
            "QTabWidget { border: none; }"
            "QTabWidget::pane { border: none; }"
            "QTabBar { background: #2b2b2b; border: none; }"
            "QTabBar::tab { background: #2b2b2b; color: #aaa; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: #fff; border-bottom: 2px solid #4a9eff; }"
            "QTabBar::tab:hover { color: #ddd; background: #353535; }"
            // Tab 滚动按钮（tab 过多时出现的左右箭头）
            "QTabBar::tear { width: 0; border: none; }"
            "QTabBar QToolButton { background: #2b2b2b; border: none; color: #aaa; width: 20px; }"
            "QTabBar QToolButton:hover { background: #353535; color: #fff; }"
            // 分割线
            "QSplitter::handle { background: #3c3f41; }"
            "QSplitter::handle:horizontal { width: 2px; }"
            "QSplitter::handle:vertical { height: 2px; }"
            // 对话框（About、更新日志等）
            "QDialog { background: #2b2b2b; color: #ccc; }"
            "QDialog QLabel { color: #ccc; }"
            "QDialog QTextEdit, QDialog QTextBrowser { background: #1e1e1e; color: #ccc; border: 1px solid #555; }"
            "QDialog QPushButton { background: #3c3f41; color: #ccc; border: 1px solid #555; padding: 6px 16px; border-radius: 3px; }"
            "QDialog QPushButton:hover { background: #4a4d50; }"
            "QMessageBox { background: #2b2b2b; color: #ccc; }"
            "QMessageBox QLabel { color: #ccc; }"
            // 滚动条
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
            "QScrollBar::handle:vertical { background: rgba(255,255,255,40); border-radius: 3px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,80); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }"
            "QScrollBar::handle:horizontal { background: rgba(255,255,255,40); border-radius: 3px; min-width: 30px; }"
            "QScrollBar::handle:horizontal:hover { background: rgba(255,255,255,80); }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
            // 状态栏
            "QStatusBar { background: #2b2b2b; color: #aaa; border-top: 1px solid #3c3f41; font-size: 12px; }"
            "QStatusBar QLabel { color: #aaa; font-size: 12px; }"
        ));
    } else {
        setStyleSheet(QStringLiteral(
            // 编辑器和预览区域去掉默认 frame 边框
            "QAbstractScrollArea { border: none; }"
            // 菜单栏
            "QMenuBar { border: none; }"
            "QMenuBar::item { padding: 6px 10px; }"
            "QMenuBar::item:selected { background: #e8e8e8; border-bottom: 2px solid #0078d4; }"
            // 菜单
            "QMenu { border: 1px solid #d0d0d0; padding: 4px 0; }"
            "QMenu::item { padding: 6px 24px 6px 32px; }"  // [Spec 模块-app/10-菜单栏样式 INV-1] padding-left = indicator 占位(24) + 约1字符宽(~8)，保证 ✓ 与文字留间距
            "QMenu::item:selected { background: #e8f0fe; border-left: 2px solid #0078d4; }"
            "QMenu::separator { background: #e0e0e0; height: 1px; margin: 4px 8px; }"
            "QMenu::indicator { width: 16px; height: 16px; margin-right: 8px; }"
            // Tab 栏
            "QTabWidget { border: none; }"
            "QTabWidget::pane { border: none; }"
            "QTabBar { background: #f0f0f0; border: none; }"
            "QTabBar::tab { background: #f0f0f0; color: #666; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: #333; border-bottom: 2px solid #0078d4; background: #fff; }"
            "QTabBar::tab:hover { color: #333; background: #e8e8e8; }"
            // Tab 滚动按钮
            "QTabBar::tear { width: 0; border: none; }"
            "QTabBar QToolButton { background: #f0f0f0; border: none; color: #666; width: 20px; }"
            "QTabBar QToolButton:hover { background: #e0e0e0; color: #333; }"
            // 分割线
            "QSplitter::handle { background: #e0e0e0; }"
            "QSplitter::handle:horizontal { width: 1px; }"
            "QSplitter::handle:vertical { height: 1px; }"
            // 滚动条
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
            "QScrollBar::handle:vertical { background: rgba(0,0,0,40); border-radius: 3px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(0,0,0,80); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }"
            "QScrollBar::handle:horizontal { background: rgba(0,0,0,40); border-radius: 3px; min-width: 30px; }"
            "QScrollBar::handle:horizontal:hover { background: rgba(0,0,0,80); }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
            // 状态栏
            "QStatusBar { background: #f0f0f0; color: #666; border-top: 1px solid #ddd; font-size: 12px; }"
            "QStatusBar QLabel { color: #666; font-size: 12px; }"
        ));
    }

    updateAllTabCloseButtons();

    // Windows 深色标题栏
#ifdef _WIN32
    setDarkTitleBar(theme.isDark);
#endif
}

MainWindow::TabData* MainWindow::currentTab()
{
    int idx = m_tabWidget->currentIndex();
    if (idx < 0 || idx >= m_tabs.size())
        return nullptr;
    return &m_tabs[idx];
}

bool MainWindow::maybeSave(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return true;

    Document* doc = m_tabs[index].editor->document();
    if (!doc->isModified())
        return true;

    // 空的未命名文件无需保存确认
    if (doc->filePath().isEmpty() && doc->length() == 0)
        return true;

    QString name = doc->filePath().isEmpty() ? tr("Untitled") : QFileInfo(doc->filePath()).fileName();
    QMessageBox::StandardButton ret = QMessageBox::warning(
        this, tr("Save Changes"),
        tr("Do you want to save changes to \"%1\"?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Save) {
        if (doc->filePath().isEmpty()) {
            QString path = QFileDialog::getSaveFileName(
                this, tr("Save File"), QString(),
                tr("Markdown Files (*.md *.markdown);;All Files (*)"));
            if (path.isEmpty())
                return false;
            doc->saveToFile(path);
        } else {
            unwatchFile(doc->filePath());
            doc->saveToFile();
            // 关闭 tab 时不需要重新监控（tab 即将被移除）
        }
        return true;
    } else if (ret == QMessageBox::Discard) {
        return true;
    }
    return false;  // Cancel
}

// -- Slots --

void MainWindow::onNewFile()
{
    newTab();
}

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open File"), QString(),
        tr("Markdown Files (*.md *.markdown);;Text Files (*.txt);;All Files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onSaveFile()
{
    auto* tab = currentTab();
    if (!tab) return;

    Document* doc = tab->editor->document();
    if (doc->filePath().isEmpty()) {
        onSaveFileAs();
    } else {
        QString fp = doc->filePath();
        unwatchFile(fp);
        doc->saveToFile();
        updateTabTitle(m_tabWidget->currentIndex());
        // 延迟重新监控，避免捕获自身保存产生的文件变更事件
        QTimer::singleShot(500, this, [this, fp]() { watchFile(fp); });
    }
}

void MainWindow::onSaveFileAs()
{
    auto* tab = currentTab();
    if (!tab) return;

    QString oldPath = tab->editor->document()->filePath();
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save File As"), QString(),
        tr("Markdown Files (*.md *.markdown);;All Files (*)"));
    if (path.isEmpty()) return;

    if (!oldPath.isEmpty())
        unwatchFile(oldPath);
    tab->editor->document()->saveToFile(path);
    QString absFp = QFileInfo(path).absoluteFilePath();
    QTimer::singleShot(500, this, [this, absFp]() { watchFile(absFp); });
    m_recentFiles->addFile(path);
    updateTabTitle(m_tabWidget->currentIndex());
}

void MainWindow::onCloseTab(int index)
{
    if (!maybeSave(index))
        return;

    // 取消文件监控
    QString fp = m_tabs[index].editor->document()->filePath();
    if (!fp.isEmpty())
        unwatchFile(fp);

    // Remove tab data
    m_tabs[index].splitter->deleteLater();
    m_tabs.removeAt(index);
    m_tabWidget->removeTab(index);

    // If no tabs left, create a new one
    if (m_tabs.isEmpty())
        newTab();

    saveSessionLater();
}

void MainWindow::onTabChanged(int index)
{
    updateTabTitle(index);

    // 切换 tab 时更新 TOC 面板
    if (index >= 0 && index < m_tabs.size()) {
        auto* preview = m_tabs[index].preview;
        m_tocPanel->setEntries(preview->tocEntries());
        m_tocPanel->setHighlightedEntries(preview->tocHighlightedIndices());

        // 切换到有待重载标记的 tab 时弹窗提示
        if (m_tabs[index].pendingReload)
            QTimer::singleShot(0, this, [this, index]() { promptReloadTab(index); });

        // [演示模式] 切换 tab 时：隐藏编辑器、预览占满
        if (m_focusMode) {
            m_tabs[index].editor->hide();
            m_tabs[index].preview->show();
            m_tabs[index].splitter->setSizes({0, 1});
            m_tabs[index].preview->setFocus();
        }
    } else {
        m_tocPanel->setEntries({});
    }

    // 切换 tab 时更新状态栏统计
    updateStatusBarStats();
    if (index >= 0 && index < m_tabs.size()) {
        auto curPos = m_tabs[index].editor->document()->selection().cursorPosition();
        updateCursorPosition(curPos.line, curPos.column);
    }
}

// -- Drag & Drop --

static bool isImageFilePath(const QString& path)
{
    static const QStringList exts = {
        "png", "jpg", "jpeg", "gif", "svg", "webp", "bmp"
    };
    QString suffix = QFileInfo(path).suffix().toLower();
    return exts.contains(suffix);
}

// 明确二进制扩展名黑名单——不做 sniff，直接拒绝
static bool isKnownBinaryExt(const QString& path)
{
    static const QStringList exts = {
        "exe", "dll", "so", "dylib", "zip", "rar", "7z", "tar", "gz", "bz2", "xz",
        "pdf", "docx", "xlsx", "pptx", "odt", "ods", "odp",
        "mp3", "mp4", "avi", "mkv", "mov", "wav", "flac", "ogg",
        "iso", "bin", "dat", "db", "sqlite", "pyc", "class", "o", "obj",
        "ttf", "otf", "woff", "woff2"
    };
    QString suffix = QFileInfo(path).suffix().toLower();
    return exts.contains(suffix);
}

// [Spec 模块-app/03-文件操作与拖拽 Plan]
// 前 4KB sniff 判定是否文本：NUL 即拒，控制字符占比 > 10% 即拒
static bool sniffIsTextFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray head = f.read(4096);
    f.close();
    if (head.isEmpty()) return true;  // 空文件视为文本
    int ctrl = 0;
    for (char c : head) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u == 0) return false;  // 任何 NUL 即二进制
        if (u < 0x20 && u != '\t' && u != '\n' && u != '\r') {
            ++ctrl;
        }
    }
    // 控制字符占比 > 10% 视为二进制
    return ctrl * 10 <= head.size();
}

// 宽松文本白名单：扩展名明确允许
static bool isKnownTextExt(const QString& path)
{
    static const QStringList exts = {
        "md", "markdown", "txt", "log", "conf", "cfg", "ini",
        "json", "yaml", "yml", "xml", "csv", "tsv",
        "h", "hpp", "c", "cpp", "cc", "cxx", "py", "js", "ts", "java", "kt",
        "rs", "go", "rb", "php", "sh", "bash", "zsh", "ps1",
        "gitignore", "gitattributes", "editorconfig", "env",
        "cmake", "mk", "makefile", "dockerfile",
        "html", "htm", "css", "scss", "sass", "less",
        "rst", "tex", "toml", "properties"
    };
    QFileInfo fi(path);
    QString suffix = fi.suffix().toLower();
    if (exts.contains(suffix)) return true;
    // 特殊文件名：TODO / README / LICENSE / CHANGELOG / CMakeLists.txt 等无后缀约定名
    static const QStringList knownNames = {
        "todo", "readme", "license", "changelog", "authors", "contributing",
        "cmakelists", "makefile", "dockerfile", "rakefile", "gemfile"
    };
    QString base = fi.completeBaseName().toLower();
    if (fi.suffix().isEmpty() && knownNames.contains(base)) return true;
    return false;
}

// 综合判定：能否用文本方式打开（决策 1: 宽松 + sniff 兜底）
static bool isOpenableTextFile(const QString& path)
{
    // 1. 明确二进制扩展名 → 直接拒
    if (isKnownBinaryExt(path)) return false;
    // 2. 已知文本扩展名 / 约定文件名 → 直接允许
    if (isKnownTextExt(path)) return true;
    // 3. 无后缀 / 不认识的后缀 → sniff 前 4KB
    return sniffIsTextFile(path);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    // [Spec Plan 决策 2: 晚拒绝] dragEnter 全接受，drop 时再校验
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList rejected;
    for (const QUrl& url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.isEmpty()) continue;

        if (isImageFilePath(path)) {
            // 图片文件：插入到当前编辑器的光标处
            TabData* tab = currentTab();
            if (tab && tab->editor) {
                tab->editor->insertImageMarkdown(path);
            }
            continue;
        }

        if (isOpenableTextFile(path)) {
            openFile(path);
        } else {
            rejected.append(QFileInfo(path).fileName());
        }
    }

    // [Spec Plan 决策 1+2] 拒绝时统一弹窗列出
    if (!rejected.isEmpty()) {
        QString body = tr("The following file(s) appear to be binary or unsupported. "
                          "SimpleMarkdown only opens Markdown and plain text files:\n\n  %1")
                           .arg(rejected.join("\n  "));
        QMessageBox::warning(this, tr("Cannot Open File"), body);
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Esc 键退出专注模式
    if (m_focusMode && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            exitFocusMode();
            return true;
        }
    }

    // Ctrl+滚轮缩放字体
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            if (we->angleDelta().y() > 0)
                zoomIn();
            else if (we->angleDelta().y() < 0)
                zoomOut();
            return true;
        }
    }

#ifdef _WIN32
    // 为所有弹窗（QDialog、QMessageBox 等）自动设置深色标题栏
    if (event->type() == QEvent::Show) {
        QWidget* w = qobject_cast<QWidget*>(obj);
        if (w && w->isWindow() && w != this) {
            HWND hwnd = reinterpret_cast<HWND>(w->winId());
            BOOL useDark = m_currentTheme.isDark ? TRUE : FALSE;
            ::DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
        }
    }
#endif
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_splitterInitialized) {
        m_splitterInitialized = true;
        if (!m_pendingSplitterState.isEmpty()) {
            m_mainSplitter->restoreState(m_pendingSplitterState);
        } else {
            int totalW = m_mainSplitter->width();
            m_mainSplitter->setSizes({totalW - 220, 220});
        }
        m_pendingSplitterState.clear();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 关闭窗口时先退出专注模式，以便正确保存布局状态
    if (m_focusMode)
        exitFocusMode();

    // Check all tabs for unsaved changes
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (!maybeSave(i)) {
            event->ignore();
            return;
        }
    }
    saveSettings();
    event->accept();
}

void MainWindow::startLocalServer(const char* serverName)
{
    QLocalServer::removeServer(serverName);
    m_localServer = new QLocalServer(this);
    m_localServer->listen(serverName);

    connect(m_localServer, &QLocalServer::newConnection, this, [this]() {
        QLocalSocket* socket = m_localServer->nextPendingConnection();
        if (!socket)
            return;

        socket->waitForReadyRead(1000);
        QByteArray data = socket->readAll();
        socket->deleteLater();

        QString filePath = QString::fromUtf8(data);
        if (!filePath.isEmpty()) {
            openFile(filePath);
        }

        // Activate window
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        raise();
        activateWindow();
    });
}

void MainWindow::saveSessionLater()
{
    m_saveSessionTimer.start();  // (重新)启动 1 秒倒计时
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("view/wordWrap", m_wordWrapAct ? m_wordWrapAct->isChecked() : true);
    s.setValue("view/lineSpacing", m_lineSpacingFactor);
    s.setValue("view/fontSizeDelta", m_fontSizeDelta);
    // 主题：follow_system / light / dark
    QString themeMode = "follow_system";
    if (m_lightThemeAct && m_lightThemeAct->isChecked()) themeMode = "light";
    else if (m_darkThemeAct && m_darkThemeAct->isChecked()) themeMode = "dark";
    s.setValue("view/themeMode", themeMode);
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/mainSplitter", m_mainSplitter->saveState());

    // 会话恢复
    s.setValue("session/restoreLastFile", m_restoreSessionAct ? m_restoreSessionAct->isChecked() : true);
    s.setValue("session/activeTab", m_tabWidget->currentIndex());

    // 保存所有标签页
    s.beginWriteArray("session/tabs");
    int written = 0;
    for (int i = 0; i < m_tabs.size(); ++i) {
        QString fp = m_tabs[i].editor->document()->filePath();
        if (fp.isEmpty()) continue; // 跳过未保存的空文档
        s.setArrayIndex(written++);
        s.setValue("file", fp);
        s.setValue("editorScroll", m_tabs[i].editor->verticalScrollBar()->value());
        s.setValue("editorHScroll", m_tabs[i].editor->horizontalScrollBar()->value());
        s.setValue("previewScroll", m_tabs[i].preview->verticalScrollBar()->value());
        s.setValue("previewHScroll", m_tabs[i].preview->horizontalScrollBar()->value());
        // 光标位置（行号、列号）
        auto cursorPos = m_tabs[i].editor->document()->selection().cursorPosition();
        s.setValue("cursorLine", cursorPos.line);
        s.setValue("cursorColumn", cursorPos.column);
    }
    s.endArray();
}

void MainWindow::loadSettings()
{
    QSettings s;

    // 窗口位置
    if (s.contains("window/geometry"))
        restoreGeometry(s.value("window/geometry").toByteArray());

    // 主分割器（编辑/预览 | TOC）状态在 showEvent 中恢复（需窗口已显示）
    m_pendingSplitterState = s.value("window/mainSplitter").toByteArray();

    // 主题
    QString themeMode = s.value("view/themeMode", "follow_system").toString();
    if (themeMode == "light") {
        m_lightThemeAct->setChecked(true);
        applyTheme(Theme::light());
    } else if (themeMode == "dark") {
        m_darkThemeAct->setChecked(true);
        applyTheme(Theme::dark());
    } else {
        m_followSystemThemeAct->setChecked(true);
        applySystemTheme();
    }

    // 自动换行
    bool wordWrap = s.value("view/wordWrap", true).toBool();
    m_wordWrapAct->setChecked(wordWrap);

    // 会话恢复选项
    if (m_restoreSessionAct)
        m_restoreSessionAct->setChecked(s.value("session/restoreLastFile", true).toBool());

    // 行间距
    m_lineSpacingFactor = s.value("view/lineSpacing", 1.5).toDouble();
    // 更新菜单选中状态
    double spacings[] = {1.0, 1.2, 1.5, 1.8, 2.0};
    for (int i = 0; i < m_spacingActions.size() && i < 5; ++i) {
        if (qFuzzyCompare(spacings[i], m_lineSpacingFactor))
            m_spacingActions[i]->setChecked(true);
    }
    // 字体缩放
    m_fontSizeDelta = s.value("view/fontSizeDelta", 0).toInt();

    // 语言设置
    QString locale = s.value("language/locale", "zh_CN").toString();
    if (locale == "en_US" && m_enUSAct) {
        m_enUSAct->setChecked(true);
    } else if (m_zhCNAct) {
        m_zhCNAct->setChecked(true);
    }

    // 应用到已有标签页
    for (auto& tab : m_tabs) {
        tab.editor->setLineSpacing(m_lineSpacingFactor);
        tab.editor->setWordWrap(wordWrap);
        tab.preview->setWordWrap(wordWrap);
    }

    // [字体系统] Spec: specs/横切关注点/80-字体系统.md INV-4
    // 无条件调用：即使 delta==0 也要同步两侧字体，避免两边走各自构造函数的默认值而出现差异
    applyFontSize();
}

static QIcon makeCloseIcon(const QColor& normal, const QColor& hover)
{
    auto drawX = [](int size, const QColor& color) {
        qreal dpr = qApp->devicePixelRatio();
        int px = qRound(size * dpr);
        QPixmap pm(px, px);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        qreal m = size * 0.3;
        p.drawLine(QPointF(m, m), QPointF(size - m, size - m));
        p.drawLine(QPointF(size - m, m), QPointF(m, size - m));
        return pm;
    };
    QIcon icon;
    icon.addPixmap(drawX(14, normal), QIcon::Normal);
    icon.addPixmap(drawX(14, hover), QIcon::Active);
    return icon;
}

void MainWindow::setTabCloseButton(int index)
{
    auto* btn = new QToolButton();
    btn->setAutoRaise(true);
    btn->setCursor(Qt::ArrowCursor);
    btn->setToolTip(tr("Close"));

    QColor normal = m_currentTheme.isDark ? QColor(136, 136, 136) : QColor(153, 153, 153);
    QColor hover  = m_currentTheme.isDark ? QColor(255, 255, 255) : QColor(51, 51, 51);
    btn->setIcon(makeCloseIcon(normal, hover));
    btn->setIconSize(QSize(14, 14));
    btn->setFixedSize(18, 18);

    QString hoverBg = m_currentTheme.isDark
        ? "rgba(255,255,255,20)" : "rgba(0,0,0,15)";
    btn->setStyleSheet(QString(
        "QToolButton { border: none; background: transparent; border-radius: 3px; }"
        "QToolButton:hover { background: %1; }"
    ).arg(hoverBg));

    connect(btn, &QToolButton::clicked, this, [this, btn]() {
        // 通过按钮找到对应的 tab 索引
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            if (m_tabWidget->tabBar()->tabButton(i, QTabBar::RightSide) == btn) {
                onCloseTab(i);
                return;
            }
        }
    });

    m_tabWidget->tabBar()->setTabButton(index, QTabBar::RightSide, btn);
}

void MainWindow::updateAllTabCloseButtons()
{
    for (int i = 0; i < m_tabWidget->count(); ++i)
        setTabCloseButton(i);
}

void MainWindow::watchFile(const QString& path)
{
    if (!path.isEmpty() && !m_fileWatcher.files().contains(path))
        m_fileWatcher.addPath(path);
}

void MainWindow::unwatchFile(const QString& path)
{
    if (!path.isEmpty() && m_fileWatcher.files().contains(path))
        m_fileWatcher.removePath(path);
}

void MainWindow::onFileChangedExternally(const QString& path)
{
    // 找到对应的 tab
    int tabIndex = -1;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].editor->document()->filePath() == path) {
            tabIndex = i;
            break;
        }
    }
    if (tabIndex < 0)
        return;

    // 文件可能被删除，检查是否还存在
    if (!QFileInfo::exists(path))
        return;

    // 重新添加监控（某些系统修改后会自动移除）
    watchFile(path);

    // 非当前 tab：标记待重载，切换时再弹窗
    if (tabIndex != m_tabWidget->currentIndex()) {
        m_tabs[tabIndex].pendingReload = true;
        return;
    }

    promptReloadTab(tabIndex);
}

void MainWindow::promptReloadTab(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= m_tabs.size())
        return;

    m_tabs[tabIndex].pendingReload = false;

    QString path = m_tabs[tabIndex].editor->document()->filePath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;

    QString name = QFileInfo(path).fileName();
    QMessageBox::StandardButton ret = QMessageBox::question(
        this, tr("File Changed"),
        tr("\"%1\" has been modified by another program.\n\n"
           "Do you want to reload it?").arg(name),
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        m_tabs[tabIndex].editor->document()->loadFromFile(path);
        m_tabs[tabIndex].scheduler->parseNow();
        updateTabTitle(tabIndex);
    }
}

// ---- 状态栏统计信息 ----

void MainWindow::setupStatusBar()
{
    auto* sb = statusBar();
    sb->setSizeGripEnabled(true);

    // 统一样式：QLabel 之间加竖线分隔
    auto makeLabel = [sb](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, sb);
        label->setContentsMargins(8, 0, 8, 0);
        sb->addPermanentWidget(label);
        return label;
    };

    m_statusCursorPos = makeLabel(tr("Ln %1, Col %2").arg(1).arg(1));
    m_statusLineCount = makeLabel(tr("Lines: %1").arg(0));
    m_statusWordCount = makeLabel(tr("Words: %1").arg(0));
    m_statusCharCount = makeLabel(tr("Chars: %1").arg(0));
    m_statusReadTime = makeLabel(tr("Read: <1 min"));

    // 防抖定时器：文本变化后 300ms 才更新统计（避免频繁计算）
    m_statsDebounceTimer.setSingleShot(true);
    m_statsDebounceTimer.setInterval(300);
    connect(&m_statsDebounceTimer, &QTimer::timeout, this, &MainWindow::updateStatusBarStats);
}

void MainWindow::connectTabStatusBar(const TabData& tab)
{
    // 文本变化 → 防抖更新统计
    connect(tab.editor->document(), &Document::textChanged,
            this, [this]() {
        m_statsDebounceTimer.start();
    });

    // 光标移动 → 立即更新光标位置
    connect(tab.editor, &EditorWidget::cursorPositionChanged,
            this, &MainWindow::updateCursorPosition);
}

void MainWindow::updateCursorPosition(int line, int column)
{
    m_statusCursorPos->setText(tr("Ln %1, Col %2").arg(line + 1).arg(column + 1));
}

void MainWindow::updateStatusBarStats()
{
    auto* tab = currentTab();
    if (!tab) return;

    QString text = tab->editor->document()->text();

    // 行数
    int lineCount = tab->editor->document()->lineCount();
    m_statusLineCount->setText(tr("Lines: %1").arg(lineCount));

    // 字符数（不含空格）
    int charsNoSpace = 0;
    for (const QChar& ch : text) {
        if (!ch.isSpace())
            ++charsNoSpace;
    }
    m_statusCharCount->setText(tr("Chars: %1/%2").arg(charsNoSpace).arg(text.length()));

    // 字数统计：中文按字计算，英文按单词计算
    int wordCount = 0;
    bool inWord = false;
    for (int i = 0; i < text.length(); ++i) {
        QChar ch = text[i];
        // CJK 统一汉字区间
        ushort uc = ch.unicode();
        bool isCJK = (uc >= 0x4E00 && uc <= 0x9FFF)    // CJK 统一汉字
                  || (uc >= 0x3400 && uc <= 0x4DBF)     // CJK 扩展 A
                  || (uc >= 0xF900 && uc <= 0xFAFF);    // CJK 兼容汉字
        if (isCJK) {
            if (inWord) {
                ++wordCount;  // 结束英文单词
                inWord = false;
            }
            ++wordCount;  // 每个中文字符算一个
        } else if (ch.isLetterOrNumber()) {
            inWord = true;
        } else {
            if (inWord) {
                ++wordCount;
                inWord = false;
            }
        }
    }
    if (inWord)
        ++wordCount;

    m_statusWordCount->setText(tr("Words: %1").arg(wordCount));

    // 阅读时间估算（300 字/分钟）
    int minutes = wordCount / 300;
    if (minutes < 1)
        m_statusReadTime->setText(tr("Read: <1 min"));
    else
        m_statusReadTime->setText(tr("Read: %1 min").arg(minutes));
}

// ---- 字体缩放 ----

void MainWindow::zoomIn()
{
    if (m_fontSizeDelta < 20) {
        m_fontSizeDelta += 2;
        applyFontSize();
    }
}

void MainWindow::zoomOut()
{
    if (m_fontSizeDelta > -8) {
        m_fontSizeDelta -= 2;
        applyFontSize();
    }
}

void MainWindow::zoomReset()
{
    m_fontSizeDelta = 0;
    applyFontSize();
}

void MainWindow::applyFontSize()
{
    // [字体系统] Spec: specs/横切关注点/80-字体系统.md INV-1, INV-2, INV-5, INV-6, INV-10
    // 预览侧字号固定为 base + delta；编辑器侧通过 balanceEditorFontSize 做视觉补偿
    // （等宽字体与比例字体在同 pointSize 下视觉差异显著，需对齐 xHeight）
    QFont previewFont = font_defaults::defaultPreviewFont(m_fontSizeDelta);
    QFont editorBase  = font_defaults::defaultEditorFont(m_fontSizeDelta);

    for (auto& tab : m_tabs) {
        // 用预览的 viewport 作为度量 device，确保 DPI 一致
        QFont editorFont = font_defaults::balanceEditorFontSize(
            editorBase, previewFont, tab.preview->viewport());
        tab.editor->editorLayout()->setFont(editorFont);
        tab.preview->previewLayout()->setFont(previewFont);
        tab.preview->rebuildLayout();
        tab.preview->viewport()->update();
        tab.editor->viewport()->update();
    }
    saveSessionLater();
}

// ---- 导出与打印 ----

QString MainWindow::currentMarkdownToHtml()
{
    auto* tab = currentTab();
    if (!tab) return {};

    QString markdown = tab->editor->document()->text();
    MarkdownParser parser;
    return parser.renderHtml(markdown);
}

void MainWindow::onExportHtml()
{
    auto* tab = currentTab();
    if (!tab) return;

    QString body = currentMarkdownToHtml();
    if (body.isEmpty()) return;

    QString baseName = QFileInfo(tab->editor->document()->filePath()).completeBaseName();
    if (baseName.isEmpty()) baseName = "untitled";

    QString html = QString(kHtmlTemplate).arg(baseName.toHtmlEscaped(), body);

    QString defaultPath = QFileInfo(tab->editor->document()->filePath()).absolutePath();
    if (defaultPath.isEmpty()) defaultPath = QDir::homePath();

    QString savePath = QFileDialog::getSaveFileName(
        this, tr("Export HTML"), defaultPath + "/" + baseName + ".html",
        tr("HTML Files (*.html)"));
    if (savePath.isEmpty()) return;

    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(html.toUtf8());
        file.close();
    }
}

void MainWindow::onExportPdf()
{
    auto* tab = currentTab();
    if (!tab) return;

    QString body = currentMarkdownToHtml();
    if (body.isEmpty()) return;

    QString baseName = QFileInfo(tab->editor->document()->filePath()).completeBaseName();
    if (baseName.isEmpty()) baseName = "untitled";

    QString html = QString(kHtmlTemplate).arg(baseName.toHtmlEscaped(), body);

    QString defaultPath = QFileInfo(tab->editor->document()->filePath()).absolutePath();
    if (defaultPath.isEmpty()) defaultPath = QDir::homePath();

    QString savePath = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), defaultPath + "/" + baseName + ".pdf",
        tr("PDF Files (*.pdf)"));
    if (savePath.isEmpty()) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(savePath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    QTextDocument doc;
    doc.setDefaultFont(QFont("Microsoft YaHei", 10));
    doc.setHtml(html);
    // pageRect() 返回设备像素(1200 DPI)，QTextDocument 使用 72 DPI 逻辑坐标
    qreal scale = 72.0 / printer.resolution();
    doc.setPageSize(QSizeF(printer.pageRect().width() * scale,
                           printer.pageRect().height() * scale));
    doc.print(&printer);
}

void MainWindow::onPrint()
{
    auto* tab = currentTab();
    if (!tab) return;

    QString body = currentMarkdownToHtml();
    if (body.isEmpty()) return;

    QString baseName = QFileInfo(tab->editor->document()->filePath()).completeBaseName();
    if (baseName.isEmpty()) baseName = "untitled";

    QString html = QString(kHtmlTemplate).arg(baseName.toHtmlEscaped(), body);

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));

    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;

    QTextDocument doc;
    doc.setDefaultFont(QFont("Microsoft YaHei", 10));
    doc.setHtml(html);
    // pageRect() 返回设备像素(1200 DPI)，QTextDocument 使用 72 DPI 逻辑坐标
    qreal scale = 72.0 / printer.resolution();
    doc.setPageSize(QSizeF(printer.pageRect().width() * scale,
                           printer.pageRect().height() * scale));
    doc.print(&printer);
}

void MainWindow::onShowShortcuts()
{
    // [Spec 模块-app/07-快捷键弹窗.md INV-1] 构造时直接注入当前主题。
    ShortcutsDialog dialog(m_currentTheme, this);
    dialog.exec();
}

#ifdef _WIN32
void MainWindow::setDarkTitleBar(bool dark)
{
    // Windows 10 1809+ / Windows 11: 使用 DWMWA_USE_IMMERSIVE_DARK_MODE
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL useDark = dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+)
    // 旧值 19 用于 Windows 10 1809-1903
    ::DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
}
#endif

void MainWindow::switchLanguage(const QString& locale)
{
    QSettings s;
    s.setValue("language/locale", locale);

    QMessageBox::information(this, tr("Language Changed"),
        tr("Language will be changed after restart."));
}

void MainWindow::retranslateUi()
{
    // TODO: 实现运行时语言切换（当前依赖重启生效）
}

// ---- 演示模式 ----
// [Spec 模块-app/11-演示模式] Plan 裁定方案 A：替换原 Focus Mode
// 进入时：全屏 + 隐藏编辑器/菜单/Tab/TOC，只显示当前 Tab 的预览
// 退出时：恢复所有原布局
// 兼容：保留 m_focusMode / enterFocusMode / exitFocusMode 字段名避免大面积改动，
//       但实际行为已改为 Presentation Mode

void MainWindow::toggleFocusMode()
{
    if (m_focusMode)
        exitFocusMode();
    else
        enterFocusMode();
}

void MainWindow::enterFocusMode()
{
    if (m_focusMode) return;
    m_focusMode = true;
    m_focusModeAct->setChecked(true);

    // 保存当前布局状态
    m_savedMainSplitterState = m_mainSplitter->saveState();
    m_savedTocVisible = m_tocPanel->isVisible();
    auto* tab = currentTab();
    if (tab)
        m_savedTabSplitterSizes = tab->splitter->sizes();

    // 隐藏菜单栏、状态栏、tab 栏
    menuBar()->hide();
    statusBar()->hide();
    m_tabWidget->tabBar()->hide();

    // 隐藏 TOC 面板
    m_tocPanel->hide();

    // [演示模式] 隐藏编辑器、显示预览，让预览占满整个 Tab
    for (auto& t : m_tabs) {
        t.editor->hide();
        t.preview->show();
        t.editor->setTypewriterMode(false);  // 演示模式不需要打字机
    }

    // 让预览吃掉所有宽度
    if (tab) {
        tab->splitter->setSizes({0, 1});  // 编辑器 0，预览 1
    }

    // 进入全屏
    showFullScreen();

    // 给预览焦点（用于翻页键）
    if (tab)
        tab->preview->setFocus();
}

void MainWindow::exitFocusMode()
{
    if (!m_focusMode) return;
    m_focusMode = false;
    m_focusModeAct->setChecked(false);

    // 退出全屏
    showNormal();

    // 恢复菜单栏、状态栏、tab 栏
    menuBar()->show();
    statusBar()->show();
    m_tabWidget->tabBar()->show();

    // 恢复 TOC 面板
    m_tocPanel->setVisible(m_savedTocVisible);

    // [演示模式] 恢复编辑器显示
    for (auto& t : m_tabs) {
        t.editor->show();
        t.preview->show();
    }

    // 恢复 splitter 状态
    if (!m_savedMainSplitterState.isEmpty())
        m_mainSplitter->restoreState(m_savedMainSplitterState);

    auto* tab = currentTab();
    if (tab && !m_savedTabSplitterSizes.isEmpty())
        tab->splitter->setSizes(m_savedTabSplitterSizes);

    // 给编辑器焦点
    if (tab)
        tab->editor->setFocus();
}

// ---- 预览区链接点击处理 [Spec 模块-preview/09] ----

static bool isExecutableExt(const QString& path)
{
    static const QStringList exts = {
        "exe", "bat", "cmd", "com", "ps1", "sh", "msi", "app", "scr"
    };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

void MainWindow::onPreviewLinkClicked(const QString& url, EditorWidget* originEditor)
{
    if (url.isEmpty()) return;

    // 1. http(s):// / mailto: / ftp:// 等 scheme → 交给系统
    QUrl qurl(url);
    if (qurl.scheme() == "http" || qurl.scheme() == "https"
        || qurl.scheme() == "mailto" || qurl.scheme() == "ftp") {
        QDesktopServices::openUrl(qurl);
        return;
    }

    // 2. 本地路径：相对路径基于当前文件目录解析
    QString localPath = url;
    if (qurl.isLocalFile()) localPath = qurl.toLocalFile();

    QFileInfo fi(localPath);
    if (fi.isRelative() && originEditor) {
        QString srcDir = QFileInfo(originEditor->document()->filePath()).absolutePath();
        fi = QFileInfo(srcDir + "/" + localPath);
    }
    QString absPath = fi.absoluteFilePath();

    // 3. 安全黑名单：禁止执行可执行文件
    if (isExecutableExt(absPath)) {
        QMessageBox::warning(this, tr("Blocked"),
            tr("Refusing to open executable file: %1").arg(fi.fileName()));
        return;
    }

    // 4. 文件不存在 → 提示
    if (!fi.exists()) {
        QMessageBox::warning(this, tr("File Not Found"),
            tr("Cannot find file: %1").arg(absPath));
        return;
    }

    // 5. Markdown/文本文件 → 新开 Tab
    QString suffix = fi.suffix().toLower();
    static const QStringList mdExts = {"md", "markdown", "txt"};
    if (mdExts.contains(suffix)) {
        openFile(absPath);
        return;
    }

    // 6. 其他可打开的文档 → 交给系统默认应用
    QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
}

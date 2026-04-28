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
#include "WelcomePanel.h"  // Spec: specs/模块-app/22-空白引导页.md
#include "EditorLayout.h"
#include "PreviewLayout.h"
#include "FontDefaults.h"
#include "SnapSplitter.h"
#include "FolderPanel.h"
#include "SideTabBar.h"

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
#include <QRegExp>
#include <QProcess>
#include <QDir>
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
#include <QTranslator>
#include <QTabBar>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialog>
#include <QPushButton>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
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
// Spec: specs/模块-app/12-主题插件系统.md
#include "ThemeLoader.h"
// Spec: specs/模块-app/04-窗口焦点管理.md Tab 栏「+」按钮
#include "TabBarWithAdd.h"

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
    // Spec: specs/模块-app/21-启动窗口几何.md
    // INV-LAUNCH-NORMAL-FALLBACK: 普通状态的还原尺寸
    resize(1280, 800);
    // INV-LAUNCH-MAXIMIZED-DEFAULT: 默认最大化；若 loadSettings 后续 restoreGeometry 命中则被覆盖（INV-LAUNCH-RESPECT-SAVED）
    setWindowState(Qt::WindowMaximized);

    // 加载翻译（必须在任何 tr() 调用和子控件创建之前）
    m_translator = new QTranslator(this);
    {
        QSettings s;
        QString locale = s.value("language/locale", "zh_CN").toString();
        if (m_translator->load(":/translations/simple_markdown_" + locale + ".qm"))
            qApp->installTranslator(m_translator);
    }

    m_recentFiles = new RecentFiles(this);

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-VALIGN
    // 拆 QTabWidget 为 QTabBar + QStackedWidget，TOC 不再与 tabBar 顶齐
    // central widget = QVBoxLayout(tabBar + mainSplitter(contentStack|tocPanel))
    m_tabBar = new TabBarWithAdd(this);
    connect(m_tabBar, &TabBarWithAdd::addClicked, this, &MainWindow::onNewFile);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);

    m_contentStack = new QStackedWidget(this);

    // Spec: specs/模块-app/22-空白引导页.md
    // 空白引导面板：m_tabs.isEmpty() 时取代 m_contentStack 显示
    m_welcomePanel = new WelcomePanel(this);
    m_welcomePanel->hide();
    connect(m_welcomePanel, &WelcomePanel::openFileClicked, this, &MainWindow::onOpenFile);
    connect(m_welcomePanel, &WelcomePanel::openFolderClicked, this, &MainWindow::onOpenFolder);
    connect(m_welcomePanel, &WelcomePanel::newFileClicked, this, &MainWindow::onNewFile);

    m_tocPanel = new TocPanel(this);
    m_tocPanel->setMinimumWidth(160);

    m_folderPanel = new FolderPanel(this);
    m_folderPanel->installEventFilter(this);  // 监听 Show/Hide 以联动左侧面板

    // 侧边 Tab 栏（默认隐藏，切换到左侧模式时显示）
    m_sideTabBar = new SideTabBar(this);
    connect(m_sideTabBar, &SideTabBar::currentChanged, this, [this](int index) {
        m_tabBar->setCurrentIndex(index);
    });
    connect(m_sideTabBar, &SideTabBar::tabCloseRequested, this, &MainWindow::onCloseTab);
    connect(m_sideTabBar, &SideTabBar::addClicked, this, &MainWindow::onNewFile);

    // folderPanel 包在容器中：容器始终在 splitter 中可见，folderPanel 在容器内 hide/show
    // 这样 QSplitter 不会因 folderPanel 隐藏而回收空间导致 sideTabBar 跳动
    m_folderContainer = new QWidget(this);
    m_folderContainer->setMinimumHeight(100);  // 防止拖拽侧边 Tab 时过度压缩
    auto* folderContainerLayout = new QVBoxLayout(m_folderContainer);
    folderContainerLayout->setContentsMargins(0, 0, 0, 0);
    folderContainerLayout->setSpacing(0);
    folderContainerLayout->addWidget(m_folderPanel);

    m_leftPaneSplitter = new QSplitter(Qt::Vertical, this);
    m_leftPaneSplitter->addWidget(m_folderContainer);
    m_leftPaneSplitter->setCollapsible(0, false);
    m_leftPaneSplitter->hide();

    // 中间内容容器：tabBar + contentStack 竖排，使 tabBar 与左侧面板平级
    auto* contentContainer = new QWidget(this);
    m_centralLayout = new QVBoxLayout(contentContainer);
    m_centralLayout->setContentsMargins(0, 0, 0, 0);
    m_centralLayout->setSpacing(0);
    m_centralLayout->addWidget(m_tabBar);
    m_centralLayout->addWidget(m_contentStack, /*stretch=*/1);
    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-WELCOME-MUTUAL
    // WelcomePanel 与 m_contentStack 互斥占用 stretch=1 的中央区域
    m_centralLayout->addWidget(m_welcomePanel, /*stretch=*/1);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->addWidget(m_leftPaneSplitter); // 0: 左侧面板容器
    m_mainSplitter->addWidget(contentContainer);   // 1: 中间（tabBar + content）
    m_mainSplitter->addWidget(m_tocPanel);         // 2: 右侧目录面板
    m_mainSplitter->setStretchFactor(0, 0);  // leftPane 固定宽度
    m_mainSplitter->setStretchFactor(1, 1);  // content 拉伸
    m_mainSplitter->setStretchFactor(2, 0);  // tocPanel 固定宽度
    m_mainSplitter->setCollapsible(0, false);
    m_mainSplitter->setCollapsible(2, false);  // 防止 TOC 面板被折叠

    // FolderPanel 信号：单击打开当前 Tab，双击新 Tab
    connect(m_folderPanel, &FolderPanel::fileClicked, this, [this](const QString& path) {
        openFile(path);
    });
    connect(m_folderPanel, &FolderPanel::fileDoubleClicked, this, [this](const QString& path) {
        // 双击强制新 Tab 打开（先新建 Tab，再加载文件）
        openFile(path);
    });

    setCentralWidget(m_mainSplitter);

    // TocPanel 点击 → 跳转到当前 tab 的 preview 对应位置
    connect(m_tocPanel, &TocPanel::headingClicked, this, [this](int sourceLine) {
        auto* tab = currentTab();
        if (tab)
            tab->preview->smoothScrollToSourceLine(sourceLine);
    });

    connect(m_tocPanel, &TocPanel::clearSectionMarksRequested, this, [this](int entryIndex) {
        auto* tab = currentTab();
        if (tab)
            tab->preview->clearHighlightsInSection(entryIndex);
    });

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-AUTO
    // TocPanel 宽度自适应：每次 setEntries 后重算偏好宽度，应用到 mainSplitter
    connect(m_tocPanel, &TocPanel::preferredWidthChanged,
            this, &MainWindow::applyTocPreferredWidth);

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-USER-OVERRIDE
    // 监听用户拖拽 mainSplitter 分隔条，之后禁用宽度自适应
    connect(m_mainSplitter, &QSplitter::splitterMoved,
            this, [this](int, int) {
        // 只在 splitter 已完成初始化之后接收用户拖拽事件
        if (m_splitterInitialized) m_userDraggedToc = true;
    });

    connect(m_tabBar, &QTabBar::tabCloseRequested,
            this, &MainWindow::onCloseTab);
    connect(m_tabBar, &QTabBar::currentChanged,
            m_contentStack, &QStackedWidget::setCurrentIndex);
    connect(m_tabBar, &QTabBar::currentChanged,
            this, &MainWindow::onTabChanged);
    // 拖拽重排 tab 时同步 m_tabs 顺序与 contentStack 顺序
    connect(m_tabBar, &QTabBar::tabMoved,
            this, [this](int from, int to) {
        if (from >= 0 && from < m_tabs.size() && to >= 0 && to < m_tabs.size())
            m_tabs.move(from, to);
        // QStackedWidget 没有 moveWidget，需要 remove + insert
        if (m_contentStack && from >= 0 && to >= 0
            && from < m_contentStack->count() && to < m_contentStack->count() && from != to) {
            QWidget* w = m_contentStack->widget(from);
            if (w) {
                m_contentStack->removeWidget(w);
                m_contentStack->insertWidget(to, w);
                m_contentStack->setCurrentIndex(m_tabBar->currentIndex());
            }
        }
        saveSessionLater();
    });

    // 切换标签时实时保存会话
    connect(m_tabBar, &QTabBar::currentChanged,
            this, &MainWindow::saveSessionLater);

    // Tab 右键菜单
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        int idx = m_tabBar->tabAt(pos);
        if (idx < 0) return;
        QMenu menu(this);
        menu.addAction(tr("Close"), [this, idx]() { onCloseTab(idx); });
        // [Spec specs/模块-app/04-窗口焦点管理.md INV-5] 按上下文禁用无可关闭对象的项
        QAction* closeOthersAct = menu.addAction(tr("Close Others"), [this, idx]() {
            for (int i = m_tabs.size() - 1; i >= 0; --i)
                if (i != idx) onCloseTab(i);
        });
        if (m_tabs.size() <= 1) closeOthersAct->setEnabled(false);
        QAction* closeLeftAct = menu.addAction(tr("Close to the Left"), [this, idx]() {
            for (int i = idx - 1; i >= 0; --i)
                onCloseTab(i);
        });
        if (idx == 0) closeLeftAct->setEnabled(false);
        QAction* closeRightAct = menu.addAction(tr("Close to the Right"), [this, idx]() {
            for (int i = m_tabs.size() - 1; i > idx; --i)
                onCloseTab(i);
        });
        if (idx >= m_tabs.size() - 1) closeRightAct->setEnabled(false);

        // [Plan plans/2026-04-14-Tab打开文件所在目录.md]
        menu.addSeparator();
        QString filePath = (idx < m_tabs.size())
            ? m_tabs[idx].editor->document()->filePath() : QString();
        QAction* openDirAct = menu.addAction(tr("Open Containing Folder"), [this, filePath]() {
            if (filePath.isEmpty()) return;
#ifdef Q_OS_WIN
            // 用 explorer /select, 高亮目标文件
            QStringList args;
            args << "/select," << QDir::toNativeSeparators(filePath);
            QProcess::startDetached("explorer.exe", args);
#else
            // Linux 大部分文件管理器不支持 select 参数，只能打开目录
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
#endif
        });
        if (filePath.isEmpty()) openDirAct->setEnabled(false);

        menu.exec(m_tabBar->mapToGlobal(pos));
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
    // -- 文件菜单 --
    QMenu* fileMenu = menuBar()->addMenu(tr("File"));

    QAction* newAct = fileMenu->addAction(tr("New"), this, &MainWindow::onNewFile, QKeySequence::New);
    Q_UNUSED(newAct);

    QAction* openAct = fileMenu->addAction(tr("Open..."), this, &MainWindow::onOpenFile, QKeySequence::Open);
    Q_UNUSED(openAct);

    fileMenu->addAction(tr("Open Folder..."), this, &MainWindow::onOpenFolder);

    QAction* saveAct = fileMenu->addAction(tr("Save"), this, &MainWindow::onSaveFile, QKeySequence::Save);
    Q_UNUSED(saveAct);

    QAction* saveAsAct = fileMenu->addAction(tr("Save As..."), this, &MainWindow::onSaveFileAs, QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_S));
    Q_UNUSED(saveAsAct);

    fileMenu->addSeparator();

    m_recentMenu = fileMenu->addMenu(tr("Recent"));
    updateRecentFilesMenu();
    connect(m_recentFiles, &RecentFiles::changed,
            this, &MainWindow::updateRecentFilesMenu);

    fileMenu->addSeparator();

    fileMenu->addAction(tr("Export HTML..."), this, &MainWindow::onExportHtml);
    fileMenu->addAction(tr("Export PDF..."), this, &MainWindow::onExportPdf);
    fileMenu->addAction(tr("Print..."), this, &MainWindow::onPrint, QKeySequence::Print);

    fileMenu->addSeparator();

    fileMenu->addAction(tr("Exit"), this, &QWidget::close, QKeySequence::Quit);

    // -- 编辑菜单 --
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

    // -- 视图菜单 --
    QMenu* viewMenu = menuBar()->addMenu(tr("View"));

    // Spec: specs/模块-app/12-主题插件系统.md
    // 主题子菜单：动态列出 Follow System + 所有已发现的主题 + 工具项。
    m_themeMenu = viewMenu->addMenu(tr("Theme"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    rebuildThemeMenu();

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
            // [Spec 模块-preview/02 INV-13] 编辑区与预览区行高乘数必须同步推送，
            // 否则两侧视觉错位（用户感知最直观的 bug）
            for (auto& tab : m_tabs) {
                tab.editor->setLineSpacing(factor);
                tab.preview->setLineSpacingFactor(factor);
            }
        });
    }

    viewMenu->addSeparator();

    viewMenu->addAction(tr("Zoom In"), this, &MainWindow::zoomIn, QKeySequence(Qt::CTRL + Qt::Key_Equal));
    viewMenu->addAction(tr("Zoom Out"), this, &MainWindow::zoomOut, QKeySequence(Qt::CTRL + Qt::Key_Minus));
    viewMenu->addAction(tr("Reset Zoom"), this, &MainWindow::zoomReset, QKeySequence(Qt::CTRL + Qt::Key_0));

    viewMenu->addSeparator();

    // Ctrl+B 切换左侧资源管理器（文件夹面板 + 侧边 Tab 栏）
    m_toggleSidebarAct = viewMenu->addAction(tr("Toggle Sidebar"));
    m_toggleSidebarAct->setCheckable(true);
    m_toggleSidebarAct->setChecked(true);  // 默认显示
    m_toggleSidebarAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(m_toggleSidebarAct, &QAction::triggered, this, &MainWindow::toggleSidebar);

    m_focusModeAct = viewMenu->addAction(tr("Presentation Mode"));
    m_focusModeAct->setCheckable(true);
    m_focusModeAct->setChecked(false);
    m_focusModeAct->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_focusModeAct, &QAction::triggered, this, &MainWindow::toggleFocusMode);

    // Tab 栏位置子菜单（4项互斥）
    {
        QMenu* tabPosMenu = viewMenu->addMenu(tr("Tab Bar Position"));
        m_tabPosGroup = new QActionGroup(this);
        m_tabPosGroup->setExclusive(true);

        m_tabPosShowAllAct = tabPosMenu->addAction(tr("Show All"));
        m_tabPosShowAllAct->setCheckable(true);
        m_tabPosShowAllAct->setChecked(true);  // 默认
        connect(m_tabPosShowAllAct, &QAction::triggered, this, [this]() {
            setTabBarPosition(true, /*hideTopBar=*/false);
        });

        m_tabPosTopAct = tabPosMenu->addAction(tr("Top Only"));
        m_tabPosTopAct->setCheckable(true);
        connect(m_tabPosTopAct, &QAction::triggered, this, [this]() {
            setTabBarPosition(false);
        });

        m_tabPosSideAct = tabPosMenu->addAction(tr("Side Only"));
        m_tabPosSideAct->setCheckable(true);
        connect(m_tabPosSideAct, &QAction::triggered, this, [this]() {
            setTabBarPosition(true, /*hideTopBar=*/true);
        });

        m_tabPosHideAllAct = tabPosMenu->addAction(tr("Hide All"));
        m_tabPosHideAllAct->setCheckable(true);
        connect(m_tabPosHideAllAct, &QAction::triggered, this, [this]() {
            setTabBarPosition(false);
            m_tabBar->hide();
            m_tabPosHideAllAct->setChecked(true);
        });

        m_tabPosGroup->addAction(m_tabPosShowAllAct);
        m_tabPosGroup->addAction(m_tabPosTopAct);
        m_tabPosGroup->addAction(m_tabPosSideAct);
        m_tabPosGroup->addAction(m_tabPosHideAllAct);
    }

    // 显示区域子菜单：编辑器+预览 / 仅编辑器 / 仅预览
    {
        QMenu* displayMenu = viewMenu->addMenu(tr("Display Area"));
        m_displayGroup = new QActionGroup(this);
        m_displayGroup->setExclusive(true);

        m_displayBothAct = displayMenu->addAction(tr("Editor and Preview"));
        m_displayBothAct->setCheckable(true);
        m_displayBothAct->setChecked(true);

        m_displayEditorAct = displayMenu->addAction(tr("Editor Only"));
        m_displayEditorAct->setCheckable(true);

        m_displayPreviewAct = displayMenu->addAction(tr("Preview Only"));
        m_displayPreviewAct->setCheckable(true);

        m_displayGroup->addAction(m_displayBothAct);
        m_displayGroup->addAction(m_displayEditorAct);
        m_displayGroup->addAction(m_displayPreviewAct);

        connect(m_displayBothAct, &QAction::triggered, this, [this]() { setDisplayMode(0); });
        connect(m_displayEditorAct, &QAction::triggered, this, [this]() { setDisplayMode(1); });
        connect(m_displayPreviewAct, &QAction::triggered, this, [this]() { setDisplayMode(2); });
    }

    viewMenu->addSeparator();

    // [Spec/Plan 文档统计信息] 弹窗显示标题/段落/代码块/图片/链接统计
    viewMenu->addAction(tr("Document Statistics..."), this, &MainWindow::onShowDocumentStats);

    viewMenu->addSeparator();

    m_restoreSessionAct = viewMenu->addAction(tr("Restore Last File"));
    m_restoreSessionAct->setCheckable(true);
    m_restoreSessionAct->setChecked(true);

    // -- 设置菜单 --
    QMenu* settingsMenu = menuBar()->addMenu(tr("Settings"));

    // Spec: specs/模块-app/14-自动保存.md
    // 自动保存：开关 + 三档延迟（1.5s / 3s / 5s）
    m_autoSaveEnabledAct = settingsMenu->addAction(tr("Auto Save"));
    m_autoSaveEnabledAct->setCheckable(true);
    m_autoSaveEnabledAct->setChecked(m_autoSaveEnabled);
    connect(m_autoSaveEnabledAct, &QAction::toggled, this, [this](bool checked) {
        m_autoSaveEnabled = checked;
        if (!checked) m_autoSaveTimer.stop();
        saveSessionLater();
    });

    QMenu* autoSaveDelayMenu = settingsMenu->addMenu(tr("Auto Save Delay"));
    QActionGroup* autoSaveDelayGroup = new QActionGroup(this);
    autoSaveDelayGroup->setExclusive(true);
    struct DelayChoice { int ms; const char* label; };
    static const DelayChoice kDelays[] = {
        { 1500, QT_TR_NOOP("1.5 seconds") },
        { 3000, QT_TR_NOOP("3 seconds") },
        { 5000, QT_TR_NOOP("5 seconds") },
    };
    m_autoSaveDelayActions.clear();
    for (const auto& dc : kDelays) {
        QAction* a = autoSaveDelayMenu->addAction(tr(dc.label));
        a->setCheckable(true);
        autoSaveDelayGroup->addAction(a);
        a->setData(dc.ms);
        const int ms = dc.ms;
        connect(a, &QAction::triggered, this, [this, ms]() {
            m_autoSaveDelayMs = ms;
            saveSessionLater();
        });
        m_autoSaveDelayActions.append(a);
    }
    settingsMenu->addSeparator();

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

    // -- 帮助菜单 --
    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));

    helpMenu->addAction(tr("Keyboard Shortcuts"), this, &MainWindow::onShowShortcuts);
    helpMenu->addSeparator();

    // [Plan plans/2026-04-13-首次启动引导.md] 欢迎对话框
    helpMenu->addAction(tr("Show Welcome"), this, &MainWindow::onShowWelcome);

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
        // About 对话框改为自定义 QDialog（参考 onShowWelcome / ChangelogDialog 结构），
        // 关闭按钮借 QVBoxLayout 默认横向铺满，颜色靠全局 cascade。QMessageBox 内部按钮 row
        // 无法控制布局，故弃用。
        QDialog dlg(this);
        dlg.setWindowTitle(tr("About SimpleMarkdown"));
        dlg.setMinimumWidth(460);

        auto* layout = new QVBoxLayout(&dlg);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(14);

        // 顶部：图标 + 富文本（横向 HBox）
        auto* topRow = new QHBoxLayout();
        topRow->setSpacing(16);
        auto* iconLabel = new QLabel(&dlg);
        iconLabel->setPixmap(this->windowIcon().pixmap(64, 64));
        iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        iconLabel->setFixedSize(64, 64);
        topRow->addWidget(iconLabel, 0, Qt::AlignTop);

        auto* textLabel = new QLabel(about, &dlg);
        textLabel->setTextFormat(Qt::RichText);
        textLabel->setOpenExternalLinks(true);
        textLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        textLabel->setWordWrap(true);
        topRow->addWidget(textLabel, 1);

        layout->addLayout(topRow);

        // 关闭按钮：与首次启动欢迎对话框（onShowWelcome）完全一致的最简实现
        // 不 setStyleSheet / setObjectName / setSizePolicy / setDefault：
        // 颜色与尺寸完全靠 MainWindow::applyTheme 全局 "QDialog QPushButton {...}" cascade
        auto* closeBtn = new QPushButton(tr("Close"), &dlg);
        connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        layout->addWidget(closeBtn);

        dlg.exec();
    });
}

void MainWindow::setupDragDrop()
{
    setAcceptDrops(true);
}

// Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-AUTO
// 应用 TocPanel 偏好宽度到 mainSplitter 第二栏
// - 未被用户拖拽过：setSizes(content, toc)
// - 被用户拖拽过：只更新 minimumWidth，不覆盖用户意图
void MainWindow::applyTocPreferredWidth(int w)
{
    if (!m_mainSplitter || !m_tocPanel) return;
    // 保证 TOC 至少能显示内容（minimumWidth 始终跟随 preferred width 但不低于 160）
    m_tocPanel->setMinimumWidth(qMin(160, w));

    if (m_userDraggedToc) return;

    const int totalW = m_mainSplitter->width();
    if (totalW <= 0) return;  // 尚未 layout
    // 夹紧到屏幕 1/8
    const QScreen* scr = QGuiApplication::screenAt(mapToGlobal(QPoint(0, 0)));
    if (!scr) scr = QGuiApplication::primaryScreen();
    const int maxW = scr ? scr->availableGeometry().width() / 8 : 240;
    const int tocW = qMin(qMax(w, 120), maxW);

    // 忽略信号防止 splitterMoved 把 m_userDraggedToc 置 true
    auto sizes = m_mainSplitter->sizes();
    int folderW = sizes.size() >= 3 ? sizes[0] : 0;
    int contentW = qMax(0, totalW - folderW - tocW);
    m_mainSplitter->blockSignals(true);
    if (sizes.size() >= 3)
        m_mainSplitter->setSizes({folderW, contentW, tocW});
    else
        m_mainSplitter->setSizes({contentW, tocW});
    m_mainSplitter->blockSignals(false);
}

// Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-MAX, INV-TOC-WIDTH-USER-OVERRIDE
// 窗口 resize：若 TOC 当前宽度 > 屏幕 1/8，夹紧到上限；用户曾手动调整 / 持久化的宽度不受此限制
void MainWindow::clampTocWidthToScreen()
{
    if (!m_mainSplitter) return;
    if (m_userDraggedToc) return; // INV-TOC-WIDTH-USER-OVERRIDE / INV-TOC-WIDTH-PERSIST
    auto sizes = m_mainSplitter->sizes();
    if (sizes.size() < 2) return;
    const QScreen* scr = QGuiApplication::screenAt(mapToGlobal(QPoint(0, 0)));
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    const int maxW = scr->availableGeometry().width() / 8;
    int tocIdx = sizes.size() - 1;  // TOC 始终是最后一项
    if (sizes[tocIdx] > maxW) {
        const int totalW = m_mainSplitter->width();
        int folderW = sizes.size() >= 3 ? sizes[0] : 0;
        int contentW = qMax(0, totalW - folderW - maxW);
        m_mainSplitter->blockSignals(true);
        if (sizes.size() >= 3)
            m_mainSplitter->setSizes({folderW, contentW, maxW});
        else
            m_mainSplitter->setSizes({contentW, maxW});
        m_mainSplitter->blockSignals(false);
    }
}

// Spec: specs/模块-preview/07-TOC面板.md INV-TOC-VALIGN
// 拆分 QTabWidget 后，addTab/removeTab 需同时操作 m_tabBar 与 m_contentStack
int MainWindow::addPage(QWidget* page, const QString& title)
{
    int i = m_tabBar->addTab(title);
    m_contentStack->insertWidget(i, page);
    if (m_tabBarOnSide) m_sideTabBar->addTab(title);
    // 首个 tab 添加后手动同步 current，避免 stack 未定位
    if (m_tabBar->count() == 1) {
        m_contentStack->setCurrentIndex(0);
        m_tabBar->setCurrentIndex(0);
    }
    return i;
}

void MainWindow::removePage(int index)
{
    if (index < 0 || index >= m_tabBar->count()) return;
    QWidget* w = m_contentStack->widget(index);
    m_tabBar->removeTab(index);
    if (w) m_contentStack->removeWidget(w);
    if (m_tabBarOnSide) m_sideTabBar->removeTab(index);
}

void MainWindow::newTab()
{
    TabData tab = createTab();
    int index = addPage(tab.splitter, tr("Untitled"));
    m_tabs.append(tab);
    setTabCloseButton(index);
    m_tabBar->setCurrentIndex(index);

    // 为新建空标签页插入示例文本
    tab.editor->document()->insert(0,
        "# SimpleMarkdown\n"
        "\n"
        "A **lightweight** cross-platform Markdown editor.\n"
    );
    tab.editor->document()->setModified(false);
    // parseNow 会由 textChanged → debounce 自动触发

    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-OPEN-EXITS
    updateEmptyState();
}

void MainWindow::restoreSession(const QString& requestedFile)
{
    // Spec: specs/横切关注点/30-主题系统.md INV-3（切换主题时全部 widget 必须响应）
    // 根因：loadSettings() 在构造末尾调用 applyTheme 时 m_tabs 为空，随后 restoreSession
    // 创建的 Tab 虽然 createTab 里调了 setTheme(m_currentTheme)，但解析器/块缓存时序在
    // 某些路径下会把默认浅色固化进 preview 块缓存，导致编辑器/预览区保持浅色。
    // 修复：在所有返回路径末尾重新 applyTheme 一次，触发 tab 重绘并清缓存。
    // 用 lambda 封装避免在 3 个 return 点重复写。
    auto reapplyTheme = [this]() { applyTheme(m_currentTheme); };

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
                    int idx = m_tabBar->count() - 1;
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
        reapplyTheme();
        return;
    }

    // 无命令行文件：恢复整个会话
    if (restore) {
        struct TabState {
            QString filePath;
            int editorScroll, editorHScroll, previewScroll, previewHScroll;
            int cursorLine, cursorColumn;
        };
        int count = s.beginReadArray("session/tabs");
        QVector<TabState> tabStates;
        for (int i = 0; i < count; ++i) {
            s.setArrayIndex(i);
            QString fp = s.value("file").toString();
            if (!fp.isEmpty() && QFileInfo::exists(fp)) {
                tabStates.append({fp,
                                  s.value("editorScroll", 0).toInt(),
                                  s.value("editorHScroll", 0).toInt(),
                                  s.value("previewScroll", 0).toInt(),
                                  s.value("previewHScroll", 0).toInt(),
                                  s.value("cursorLine", 0).toInt(),
                                  s.value("cursorColumn", 0).toInt()});
            }
        }
        s.endArray();

        if (!tabStates.isEmpty()) {
            int activeTab = s.value("session/activeTab", 0).toInt();
            if (activeTab < 0 || activeTab >= tabStates.size())
                activeTab = 0;

            // 创建所有标签页：活跃 tab 立即加载，其余懒加载
            for (int i = 0; i < tabStates.size(); ++i) {
                const auto& st = tabStates[i];
                if (i == activeTab) {
                    openFile(st.filePath);
                } else {
                    // 懒加载：创建 tab 但不加载文件内容
                    TabData tab = createTab();
                    int idx = addPage(tab.splitter, QFileInfo(st.filePath).fileName());
                    tab.lazyPending = true;
                    tab.lazyFilePath = st.filePath;
                    m_tabs.append(tab);
                    setTabCloseButton(idx);
                    watchFile(QFileInfo(st.filePath).absoluteFilePath());
                }
            }

            // 设置活跃标签
            if (activeTab >= 0 && activeTab < m_tabBar->count())
                m_tabBar->setCurrentIndex(activeTab);

            // 延迟恢复活跃标签页的光标和滚动位置
            const auto& activeSt = tabStates[activeTab];
            QTimer::singleShot(200, this, [this, activeSt]() {
                if (auto* tab = currentTab()) {
                    tab->editor->document()->selection().setCursorPosition(
                        {activeSt.cursorLine, activeSt.cursorColumn});
                    tab->editor->ensureCursorVisible();
                    tab->editor->verticalScrollBar()->setValue(activeSt.editorScroll);
                    tab->editor->horizontalScrollBar()->setValue(activeSt.editorHScroll);
                    tab->preview->verticalScrollBar()->setValue(activeSt.previewScroll);
                    tab->preview->horizontalScrollBar()->setValue(activeSt.previewHScroll);
                    m_folderPanel->selectFile(tab->editor->document()->filePath());
                }
            });
            reapplyTheme();
            return;
        }
    }

    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-NO-AUTO-CREATE
    // 无可恢复 Tab 且无命令行文件时，不再自动创建 Untitled，改显示空白引导面板
    updateEmptyState();
    reapplyTheme();
}

void MainWindow::openFile(const QString& path)
{
    // 检查文件是否已在某个标签页打开（包括懒加载未加载的 tab）
    QString absPath = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_tabs.size(); ++i) {
        QString tabFp = m_tabs[i].editor->document()->filePath();
        if (tabFp.isEmpty() && m_tabs[i].lazyPending)
            tabFp = m_tabs[i].lazyFilePath;
        if (tabFp == absPath) {
            m_tabBar->setCurrentIndex(i);

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
    int index = addPage(tab.splitter, QFileInfo(path).fileName());
    m_tabs.append(tab);
    setTabCloseButton(index);
    m_tabBar->setCurrentIndex(index);
    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-OPEN-EXITS
    updateEmptyState();

    tab.editor->document()->loadFromFile(path);
    tab.preview->setDocumentDir(QFileInfo(path).absolutePath());
    tab.scheduler->parseNow();

    m_recentFiles->addFile(path);
    updateTabTitle(index);
    watchFile(QFileInfo(path).absoluteFilePath());

    // 文件夹面板自动选中当前打开的文件
    m_folderPanel->selectFile(path);

    // [修复] 加载新文件后必须提升窗口，确保用户能看到
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();

    saveSessionLater();
}

MainWindow::TabData MainWindow::createTab()
{
    TabData tab;

    // Spec: specs/模块-app/13-分隔条吸附刻度.md
    // 用 SnapSplitter 替代 QSplitter，启用 1/4、1/2、3/4 吸附刻度
    auto* snap = new app::SnapSplitter(Qt::Horizontal);
    snap->setAccentColor(m_currentTheme.accentColor);
    tab.splitter = snap;
    tab.editor = new EditorWidget(tab.splitter);
    tab.splitter->addWidget(tab.editor);

    tab.preview = new PreviewWidget(tab.splitter);
    tab.splitter->addWidget(tab.preview);
    tab.splitter->setSizes({640, 640});

    // 允许拖拽到底完全隐藏编辑器或预览区
    tab.editor->setMinimumWidth(0);
    tab.preview->setMinimumWidth(0);
    tab.splitter->setChildrenCollapsible(true);

    // splitter 拖拽联动 displayMode 菜单状态
    // splitter 拖拽联动 displayMode（debounce 200ms，松手后才判断）
    connect(tab.splitter, &QSplitter::splitterMoved, this, [this, splitter = tab.splitter]() {
        if (m_focusMode) return;
        // 每次 splitterMoved 重启 debounce timer
        QTimer::singleShot(200, this, [this, splitter]() {
            if (m_focusMode) return;
            QList<int> sizes = splitter->sizes();
            if (sizes.size() < 2) return;
            int newMode = 0;
            if (sizes[0] == 0) newMode = 2;
            else if (sizes[1] == 0) newMode = 1;
            if (newMode != m_displayMode) {
                m_displayMode = newMode;
                if (m_displayBothAct) m_displayBothAct->setChecked(newMode == 0);
                if (m_displayEditorAct) m_displayEditorAct->setChecked(newMode == 1);
                if (m_displayPreviewAct) m_displayPreviewAct->setChecked(newMode == 2);
                if (newMode == 1)
                    showToast(tr("Preview hidden. Restore via View > Display Area."));
                else if (newMode == 2)
                    showToast(tr("Editor hidden. Restore via View > Display Area."));
            }
        });
    });

    // 解析调度器：关联文档到预览
    tab.scheduler = new ParseScheduler(tab.splitter);
    tab.scheduler->setDocument(tab.editor->document());
    connect(tab.scheduler, &ParseScheduler::astReady,
            tab.preview, &PreviewWidget::updateAst);

    // 滚动同步：编辑器 → 预览
    tab.scrollSync = new ScrollSync(tab.editor, tab.preview, tab.splitter);

    // 应用当前设置
    tab.editor->setTheme(m_currentTheme);
    tab.preview->setTheme(m_currentTheme);
    tab.editor->setWordWrap(m_wordWrapAct && m_wordWrapAct->isChecked());
    tab.preview->setWordWrap(m_wordWrapAct && m_wordWrapAct->isChecked());
    tab.editor->setLineSpacing(m_lineSpacingFactor);
    // [Spec 模块-preview/02 INV-13] 新建 tab 时同步行间距到预览区
    tab.preview->setLineSpacingFactor(m_lineSpacingFactor);

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

    // 追踪修改状态以更新标签页标题和状态栏保存状态
    connect(tab.editor->document(), &Document::modifiedChanged,
            this, [this](bool) {
        int idx = m_tabBar->currentIndex();
        if (idx >= 0)
            updateTabTitle(idx);
        // Spec: specs/模块-app/15-状态栏布局.md
        updateSaveStatusOnly();
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
    QString fp = doc->filePath();
    // 懒加载 tab 尚未加载文件，从 lazyFilePath 获取路径
    if (fp.isEmpty() && m_tabs[index].lazyPending)
        fp = m_tabs[index].lazyFilePath;

    QString title;
    if (fp.isEmpty()) {
        title = tr("Untitled");
    } else {
        title = QFileInfo(fp).fileName();
    }
    if (doc->isModified())
        title = "* " + title;

    m_tabBar->setTabText(index, title);
    m_tabBar->setTabToolTip(index, fp.isEmpty() ? tr("Untitled") : QFileInfo(fp).absoluteFilePath());
    if (m_tabBarOnSide) m_sideTabBar->setTabText(index, title);

    // 更新窗口标题
    if (index == m_tabBar->currentIndex())
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
    for (const QString& entry : files) {
        QFileInfo fi(entry);
        QString display = QDir::toNativeSeparators(entry);
        if (fi.isDir()) {
            m_recentMenu->addAction(display, [this, entry]() {
                m_folderPanel->addFolder(entry);
                m_folderPanel->setTheme(m_currentTheme);
                m_sidebarHidden = false;
                if (m_toggleSidebarAct) m_toggleSidebarAct->setChecked(true);
                updateLeftPaneVisibility();
                saveSettings();  // 文件夹路径是关键状态，立即持久化
            });
        } else {
            m_recentMenu->addAction(display, [this, entry]() {
                openFile(entry);
            });
        }
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear"), m_recentFiles, &RecentFiles::clear);
}

// Spec: specs/模块-app/19-Linux深色主题检测.md
// Linux 下综合 GNOME / KDE / Qt Palette 三级探测，取首个确定的结果。
// Windows 直接读 AppsUseLightTheme 注册表项。
bool MainWindow::isSystemDarkMode() const
{
#ifdef _WIN32
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                  QSettings::NativeFormat);
    return reg.value("AppsUseLightTheme", 1).toInt() == 0;
#elif defined(__linux__)
    // 1) GNOME / GTK 3.0+：gsettings get org.gnome.desktop.interface color-scheme
    //    GNOME 42+ 返回 'prefer-dark' / 'default' / 'prefer-light'（GNOME <42 只返回 'default'）
    {
        QProcess proc;
        proc.start(QStringLiteral("gsettings"),
                   {QStringLiteral("get"),
                    QStringLiteral("org.gnome.desktop.interface"),
                    QStringLiteral("color-scheme")});
        if (proc.waitForStarted(500) && proc.waitForFinished(500)) {
            const QByteArray out = proc.readAllStandardOutput();
            const QString s = QString::fromUtf8(out).trimmed().toLower();
            // gsettings 输出带引号，如 "'prefer-dark'"
            if (s.contains(QStringLiteral("prefer-dark")))
                return true;
            if (s.contains(QStringLiteral("prefer-light")) ||
                s.contains(QStringLiteral("default"))) {
                // GNOME <42 固定返回 'default'，含义模糊，不能凭此判浅色；
                // 让路径继续跌落到 KDE / QPalette 判断
                if (!s.contains(QStringLiteral("default")))
                    return false;
            }
        }
    }

    // 2) KDE Plasma：~/.config/kdeglobals 的 [General]ColorScheme 字段
    //    典型值 "BreezeDark" / "Breeze Dark" / "OxygenDark" → 深色
    {
        const QString kdeFile = QDir::homePath() + QStringLiteral("/.config/kdeglobals");
        if (QFileInfo::exists(kdeFile)) {
            QSettings s(kdeFile, QSettings::IniFormat);
            s.beginGroup(QStringLiteral("General"));
            const QString cs = s.value(QStringLiteral("ColorScheme")).toString().toLower();
            s.endGroup();
            if (!cs.isEmpty()) {
                // 名字里含 dark 就判深色；否则判浅色
                return cs.contains(QStringLiteral("dark"));
            }
        }
    }

    // 3) 其他 DE（Sway / Hyprland / XFCE / LXQt / KDE 未设 ColorScheme）：
    //    回退到 Qt palette 亮度启发式
    QPalette pal = QApplication::palette();
    return pal.color(QPalette::Window).lightness() < 128;
#else
    // 其他平台（macOS 未实装）：Qt palette 启发式
    QPalette pal = QApplication::palette();
    return pal.color(QPalette::Window).lightness() < 128;
#endif
}

void MainWindow::applySystemTheme()
{
    m_currentThemeId.clear();  // 空表示跟随系统
    applyTheme(isSystemDarkMode() ? Theme::dark() : Theme::light());
    saveSessionLater();  // 主题切换后持久化
}

// Spec: specs/模块-app/12-主题插件系统.md
// 按 id 切换主题；id 必须是已发现的主题（内置或用户目录）
void MainWindow::applyThemeById(const QString& id)
{
    m_currentThemeId = id;
    applyTheme(Theme::byId(id));
    saveSessionLater();  // 主题切换后持久化
}

// 打开用户主题目录（首次打开会自动创建）
// Spec: specs/模块-app/12-主题插件系统.md INV-15
// 核心 inject 逻辑在 core::ThemeLoader::injectThemeTemplatesIfEmpty —— 这里只负责弹窗 UI。
void MainWindow::openThemeDirectory()
{
    const QString dir = core::ThemeLoader::userThemeDir();

    QStringList writtenNames;
    QStringList failedNames;
    core::ThemeLoader::injectThemeTemplatesIfEmpty(dir, &writtenNames, &failedNames);

    if (!writtenNames.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Theme Templates Created"),
            tr("Generated theme template and guide in this folder:\n\n  %1\n\n"
               "Copy template.toml, rename it (e.g. my-theme.toml), edit and save, "
               "then click \"Rescan Themes\" in the Theme menu to load it.")
                .arg(writtenNames.join(QStringLiteral(", "))));
    }
    if (!failedNames.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Failed to Create Theme Templates"),
            tr("Could not write the following file(s) to the theme folder:\n\n  %1\n\n"
               "Please check that the folder is writable.")
                .arg(failedNames.join(QStringLiteral(", "))));
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// 动态构建主题子菜单：
// - Follow System（第一个）
// - 所有已发现的主题（light / dark / liquid-glass / 用户主题）
// - 分隔线
// - 重新扫描主题 / 打开主题目录
void MainWindow::rebuildThemeMenu()
{
    if (!m_themeMenu || !m_themeGroup) return;

    // 清空旧内容
    m_themeMenu->clear();
    // Qt 的 actionGroup 需要手动移除，否则旧 action 仍会在组里
    for (QAction* act : m_themeGroup->actions())
        m_themeGroup->removeAction(act);
    m_dynamicThemeActs.clear();
    m_followSystemThemeAct = nullptr;
    m_lightThemeAct = nullptr;
    m_darkThemeAct = nullptr;

    // 1) 基础三项：跟随系统 / 浅色模式 / 深色模式
    m_followSystemThemeAct = m_themeMenu->addAction(tr("Follow System"));
    m_followSystemThemeAct->setCheckable(true);
    m_themeGroup->addAction(m_followSystemThemeAct);
    connect(m_followSystemThemeAct, &QAction::triggered, this, [this]() {
        applySystemTheme();
    });

    m_lightThemeAct = m_themeMenu->addAction(tr("Light Mode"));
    m_lightThemeAct->setCheckable(true);
    m_themeGroup->addAction(m_lightThemeAct);
    connect(m_lightThemeAct, &QAction::triggered, this, [this]() {
        applyThemeById("light");
    });

    m_darkThemeAct = m_themeMenu->addAction(tr("Dark Mode"));
    m_darkThemeAct->setCheckable(true);
    m_themeGroup->addAction(m_darkThemeAct);
    connect(m_darkThemeAct, &QAction::triggered, this, [this]() {
        applyThemeById("dark");
    });

    m_themeMenu->addSeparator();

    // 2) 其他已发现的主题（跳过 light/dark，已单独添加）
    const QString userDir = core::ThemeLoader::userThemeDir();
    const auto all = core::ThemeLoader::discoverAll(userDir);
    for (const auto& d : all) {
        if (d.id == QLatin1String("light") || d.id == QLatin1String("dark"))
            continue;
        QAction* act = m_themeMenu->addAction(d.displayName);
        act->setCheckable(true);
        const QString id = d.id;
        m_themeGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, id]() {
            applyThemeById(id);
        });
        m_dynamicThemeActs.append(act);
    }

    m_themeMenu->addSeparator();

    // 3) 工具项：打开主题目录 / 重新扫描
    m_themeMenu->addAction(tr("Open Themes Folder"), this, [this]() {
        openThemeDirectory();
    });
    m_themeMenu->addAction(tr("Rescan Themes"), this, [this]() {
        rebuildThemeMenu();
    });

    // 4) 根据 m_currentThemeId 恢复选中
    bool checkedAny = false;
    if (m_currentThemeId.isEmpty()) {
        m_followSystemThemeAct->setChecked(true);
        checkedAny = true;
    } else {
        for (QAction* act : m_dynamicThemeActs) {
            // 通过 connect 时捕获的 id 比对：我们没有存 id 到 QAction.data，这里用文本兜底
            // 更稳妥：扫描 all 再比 id
        }
        // 再扫一遍 all 匹配 id 并选中对应 action
        int idx = 0;
        for (const auto& d : all) {
            if (d.id == m_currentThemeId && idx < m_dynamicThemeActs.size()) {
                m_dynamicThemeActs[idx]->setChecked(true);
                checkedAny = true;
                break;
            }
            ++idx;
        }
    }
    if (!checkedAny && m_followSystemThemeAct) {
        m_followSystemThemeAct->setChecked(true);
    }
}

void MainWindow::applyTheme(const Theme& theme)
{
    m_currentTheme = theme;
    for (auto& tab : m_tabs) {
        tab.editor->setTheme(theme);
        tab.preview->setTheme(theme);
        // Spec: specs/模块-app/13-分隔条吸附刻度.md INV-SNAP-THEME
        if (auto* snap = qobject_cast<app::SnapSplitter*>(tab.splitter))
            snap->setAccentColor(theme.accentColor);
    }
    m_tocPanel->setTheme(theme);
    m_folderPanel->setTheme(theme);
    m_sideTabBar->setTheme(theme);
    if (m_welcomePanel) m_welcomePanel->setTheme(theme);  // INV-EMPTY-THEME
    // "+" 按钮前景色和 hover 背景跟随主题
    m_tabBar->setAddButtonColors(
        theme.editorFg,
        theme.isDark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 26));

    // Spec: specs/模块-app/12-主题插件系统.md INV-1 唯一数据源
    // menuBar / TabBar / StatusBar / Splitter / ScrollBar 的样式从 Theme 字段派生，
    // 所有主题（Arctic Frost / Sunset Haze / Midnight Aurora 等）切换时外壳跟随。
    // 只保留 dark 分支的硬编码（向后兼容 Spec INV-4 "弹窗跟随深色主题"要求）。
    const QString mainBg      = theme.editorBg.name();
    const QString panelBg     = theme.editorGutterBg.name();
    const QString chromeBg    = theme.editorGutterBg.name();
    const QString chromeFg    = theme.editorFg.name();
    const QString chromeMuted = theme.editorLineNumber.name();
    const QString border      = theme.editorGutterLine.name();
    const QString accent      = theme.accentColor.name();
    const QString hover       = theme.editorCurrentLine.name();
    const QString tabActiveBg = theme.editorBg.name();
    const QString scrollThumb = theme.isDark
        ? QStringLiteral("rgba(255,255,255,40)")
        : QStringLiteral("rgba(0,0,0,40)");
    const QString scrollThumbHover = theme.isDark
        ? QStringLiteral("rgba(255,255,255,80)")
        : QStringLiteral("rgba(0,0,0,80)");

    if (theme.isDark) {
        // 深色主题：menuBar / TabBar / StatusBar / Splitter / ScrollBar 从 Theme 字段派生
        // （对称非 dark 分支的数据化），保留 QDialog / QMessageBox / QTextEdit / QPushButton
        // 硬编码（Spec INV-4 强要求：深色下所有弹窗统一走深色，避免 Qt 5.15 QDialog 默认浅色 bug）
        QString css;
        css += QStringLiteral(
            "QMainWindow { background: %1; }"
            "QAbstractScrollArea { border: none; }"
            // menuBar：底部加 1px 分割线，避免与下方 TabBar 背景色相同时融成一片
            // Spec: specs/模块-app/10-菜单栏样式.md INV-7 (menuBar/tabBar 分割)
            "QMenuBar { background: %2; color: %3; border: none; border-bottom: 1px solid %6; }"
            "QMenuBar::item { padding: 6px 10px; background: transparent; }"
            "QMenuBar::item:selected { background: %4; border-bottom: 2px solid %5; }"
        ).arg(mainBg, chromeBg, chromeFg, hover, accent, border);

        css += QStringLiteral(
            // 菜单（包括右键菜单）
            "QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px 0; }"
            "QMenu::item { padding: 6px 24px 6px 32px; background: transparent; }"  // [Spec 模块-app/10-菜单栏样式 INV-1] padding-left = indicator 占位(24) + 约1字符宽(~8)，保证 ✓ 与文字留间距
            "QMenu::item:selected { background: %4; border-left: 2px solid %5; }"
            "QMenu::separator { background: %3; height: 1px; margin: 4px 8px; }"
            "QMenu::indicator { width: 16px; height: 16px; margin-right: 8px; }"
        ).arg(mainBg, chromeFg, border, hover, accent);

        css += QStringLiteral(
            // Tab 栏（拆 QTabWidget 后直接使用 QTabBar + menuBar 底部分割线）
            "QTabBar { background: %1; border: none; border-bottom: 1px solid %7; }"
            "QTabBar::tab { background: %1; color: %2; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: %3; border-bottom: 2px solid %4; background: %5; }"
            "QTabBar::tab:hover { color: %3; background: %6; }"
            // Tab 滚动按钮（tab 过多时出现的左右箭头）
            "QTabBar::tear { width: 0; border: none; }"
            "QTabBar QToolButton { background: %1; border: none; color: %2; width: 20px; }"
            "QTabBar QToolButton:hover { background: %6; color: %3; }"
        ).arg(chromeBg, chromeMuted, chromeFg, accent, tabActiveBg, hover, border);

        css += QStringLiteral(
            // 分割线
            "QSplitter::handle { background: %1; }"
            "QSplitter::handle:horizontal { width: 2px; }"
            "QSplitter::handle:vertical { height: 2px; }"
        ).arg(border);

        // 弹窗（QDialog / QMessageBox 及其子控件）——深色硬编码 + accent 色点缀
        // Spec INV-4：深色主题下弹窗统一灰黑调（避免 Qt 5.15 QDialog 默认浅色 bug），
        // 但 button hover 引入当前主题 accent 色以体现主题差异（INV-4a）
        css += QStringLiteral(
            "QDialog { background: #2b2b2b; color: #ccc; }"
            "QDialog QLabel { color: #ccc; }"
            "QDialog QTextEdit, QDialog QTextBrowser { background: #1e1e1e; color: #ccc; border: 1px solid #555; }"
            // [Spec 模块-app/22-空白引导页.md] 通用规则用 
            // 排除主操作按钮，让 styleDialogPrimaryButtonWide 在按钮上设的 stylesheet
            // 不会被祖先规则的 :default / 灰底 / 70px min-width 覆盖。
            "QDialog QPushButton { background: #3c3f41; color: #ccc; border: 1px solid #555; padding: 6px 16px; border-radius: 3px; min-width: 70px; }"
            "QDialog QPushButton:hover { background: %1; color: white; border-color: %1; }"
            "QDialog QPushButton:pressed { background: %1; color: white; }"
            "QDialog QPushButton:default { border: 1px solid %1; }"
            "QMessageBox { background: #2b2b2b; color: #ccc; }"
            "QMessageBox QLabel { color: #ccc; }"
            "QMessageBox QPushButton { background: #3c3f41; color: #ccc; border: 1px solid #555; padding: 6px 16px; border-radius: 3px; min-width: 70px; }"
            "QMessageBox QPushButton:hover { background: %1; color: white; border-color: %1; }"
            "QMessageBox QPushButton:pressed { background: %1; color: white; }"
            "QMessageBox QPushButton:default { border: 1px solid %1; }"
        ).arg(accent);

        css += QStringLiteral(
            // 滚动条
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
            "QScrollBar::handle:vertical { background: %1; border-radius: 3px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: %2; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }"
            "QScrollBar::handle:horizontal { background: %1; border-radius: 3px; min-width: 30px; }"
            "QScrollBar::handle:horizontal:hover { background: %2; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
        ).arg(scrollThumb, scrollThumbHover);

        css += QStringLiteral(
            // 状态栏
            "QStatusBar { background: %1; color: %2; border-top: 1px solid %3; font-size: 12px; }"
            "QStatusBar QLabel { color: %2; font-size: 12px; }"
        ).arg(chromeBg, chromeMuted, border);

        Q_UNUSED(panelBg);
        setStyleSheet(css);
    } else {
        // 非深色主题（Light / Arctic Frost / Sunset Haze / ...）：stylesheet 从 Theme 派生
        // 超过 9 个占位符时 QString::arg 会出问题，这里拆成 3 段
        QString css;
        css += QStringLiteral(
            "QMainWindow { background: %1; }"
            "QAbstractScrollArea { border: none; }"
            // menuBar：底部加 1px 分割线，避免与下方 TabBar 背景色相同时融成一片
            // Spec: specs/模块-app/10-菜单栏样式.md INV-7 (menuBar/tabBar 分割)
            "QMenuBar { background: %2; color: %3; border: none; border-bottom: 1px solid %6; }"
            "QMenuBar::item { padding: 6px 10px; background: transparent; }"
            "QMenuBar::item:selected { background: %4; border-bottom: 2px solid %5; }"
        ).arg(mainBg, chromeBg, chromeFg, hover, accent, border);

        css += QStringLiteral(
            // menu（弹出菜单）
            "QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px 0; }"
            "QMenu::item { padding: 6px 24px 6px 32px; background: transparent; }"
            "QMenu::item:selected { background: %4; border-left: 2px solid %5; }"
            "QMenu::separator { background: %3; height: 1px; margin: 4px 8px; }"
            "QMenu::indicator { width: 16px; height: 16px; margin-right: 8px; }"
        ).arg(mainBg, chromeFg, border, hover, accent);

        css += QStringLiteral(
            // TabBar（拆 QTabWidget 后直接使用 QTabBar + 底部分割线）
            "QTabBar { background: %1; border: none; border-bottom: 1px solid %7; }"
            "QTabBar::tab { background: %1; color: %2; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: %3; border-bottom: 2px solid %4; background: %5; }"
            "QTabBar::tab:hover { color: %3; background: %6; }"
            "QTabBar::tear { width: 0; border: none; }"
            "QTabBar QToolButton { background: %1; border: none; color: %2; width: 20px; }"
            "QTabBar QToolButton:hover { background: %6; color: %3; }"
        ).arg(chromeBg, chromeMuted, chromeFg, accent, tabActiveBg, hover, border);

        css += QStringLiteral(
            // splitter
            "QSplitter::handle { background: %1; }"
            "QSplitter::handle:horizontal { width: 1px; }"
            "QSplitter::handle:vertical { height: 1px; }"
        ).arg(border);

        css += QStringLiteral(
            // 滚动条
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
            "QScrollBar::handle:vertical { background: %1; border-radius: 3px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: %2; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }"
            "QScrollBar::handle:horizontal { background: %1; border-radius: 3px; min-width: 30px; }"
            "QScrollBar::handle:horizontal:hover { background: %2; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
        ).arg(scrollThumb, scrollThumbHover);

        css += QStringLiteral(
            // 状态栏
            "QStatusBar { background: %1; color: %2; border-top: 1px solid %3; font-size: 12px; }"
            "QStatusBar QLabel { color: %2; font-size: 12px; }"
        ).arg(chromeBg, chromeMuted, border);

        // 弹窗（非深色主题）：从 Theme 字段派生，按钮 hover 用 accent 色
        // Spec: specs/横切关注点/30-主题系统.md INV-4a (accent 色体现)
        // 之前本分支完全没有 QDialog/QMessageBox 规则，浅色主题下所有弹窗按钮原生灰白，
        // 完全不体现主题。现在补齐：6 款浅色主题的弹窗按钮 hover 会变成对应 accent 色。
        css += QStringLiteral(
            "QDialog { background: %1; color: %2; }"
            "QDialog QLabel { color: %2; }"
            "QDialog QTextEdit, QDialog QTextBrowser { background: %1; color: %2; border: 1px solid %3; }"
            // [Spec 模块-app/22-空白引导页.md] 通用规则用  排除主按钮
            "QDialog QPushButton { background: %4; color: %2; border: 1px solid %3; padding: 6px 16px; border-radius: 3px; min-width: 70px; }"
            "QDialog QPushButton:hover { background: %5; color: white; border-color: %5; }"
            "QDialog QPushButton:pressed { background: %5; color: white; }"
            "QDialog QPushButton:default { border: 1px solid %5; }"
            "QMessageBox { background: %1; color: %2; }"
            "QMessageBox QLabel { color: %2; }"
            "QMessageBox QPushButton { background: %4; color: %2; border: 1px solid %3; padding: 6px 16px; border-radius: 3px; min-width: 70px; }"
            "QMessageBox QPushButton:hover { background: %5; color: white; border-color: %5; }"
            "QMessageBox QPushButton:pressed { background: %5; color: white; }"
            "QMessageBox QPushButton:default { border: 1px solid %5; }"
        ).arg(mainBg, chromeFg, border, chromeBg, accent);

        Q_UNUSED(panelBg);
        setStyleSheet(css);
    }

    updateAllTabCloseButtons();

    // Windows 标题栏配色：深色模式 + 背景/文字颜色匹配主题
#ifdef _WIN32
    setDarkTitleBar(theme.isDark);
    // Win11 22H2+: DWMWA_CAPTION_COLOR(35) / DWMWA_TEXT_COLOR(36)
    if (auto hwnd = reinterpret_cast<HWND>(winId())) {
        COLORREF captionColor = RGB(theme.editorGutterBg.red(),
                                    theme.editorGutterBg.green(),
                                    theme.editorGutterBg.blue());
        COLORREF textColor    = RGB(theme.editorFg.red(),
                                    theme.editorFg.green(),
                                    theme.editorFg.blue());
        ::DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
        ::DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
    }
#endif

    // Spec: specs/模块-app/15-状态栏布局.md
    // 主题切换 → 刷新状态栏右区主题名（其它字段顺便也刷一次，零成本）
    updateRightStatusBar();
}

MainWindow::TabData* MainWindow::currentTab()
{
    int idx = m_tabBar->currentIndex();
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

// -- 槽函数 --

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

void MainWindow::onOpenFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
    if (dir.isEmpty()) return;
    m_folderPanel->addFolder(dir);
    m_folderPanel->setTheme(m_currentTheme);
    m_sidebarHidden = false;
    if (m_toggleSidebarAct) m_toggleSidebarAct->setChecked(true);
    updateLeftPaneVisibility();
    m_recentFiles->addFile(dir);
    saveSettings();  // 文件夹路径是关键状态，立即持久化
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
        updateTabTitle(m_tabBar->currentIndex());
        // 延迟重新监控，避免捕获自身保存产生的文件变更事件
        QTimer::singleShot(500, this, [this, fp]() { watchFile(fp); });
        // Spec: specs/模块-app/15-状态栏布局.md
        updateSaveStatusOnly();
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
    // 另存为后更新预览区文档目录（图片相对路径可能变化）
    tab->preview->setDocumentDir(QFileInfo(path).absolutePath());
    QString absFp = QFileInfo(path).absoluteFilePath();
    QTimer::singleShot(500, this, [this, absFp]() { watchFile(absFp); });
    m_recentFiles->addFile(path);
    updateTabTitle(m_tabBar->currentIndex());
    // Spec: specs/模块-app/15-状态栏布局.md
    updateRightStatusBar();
}

void MainWindow::onCloseTab(int index)
{
    if (!maybeSave(index))
        return;

    // 取消文件监控
    QString fp = m_tabs[index].editor->document()->filePath();
    if (fp.isEmpty() && m_tabs[index].lazyPending)
        fp = m_tabs[index].lazyFilePath;
    if (!fp.isEmpty())
        unwatchFile(fp);

    // 移除标签页数据
    auto* sp = m_tabs[index].splitter;
    m_tabs.removeAt(index);
    removePage(index);  // 从 tabBar + contentStack 移除
    if (sp) sp->deleteLater();

    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-CLOSE-LAST
    // 关闭最后一个 Tab 后回到空白引导状态（不再自动 newTab）
    updateEmptyState();

    saveSessionLater();
}

void MainWindow::onTabChanged(int index)
{
    // 同步侧边 Tab 栏选中状态
    if (m_tabBarOnSide) m_sideTabBar->setCurrentIndex(index);

    updateTabTitle(index);

    // 懒加载：切换到尚未加载的 tab 时触发文件加载
    if (index >= 0 && index < m_tabs.size() && m_tabs[index].lazyPending) {
        m_tabs[index].lazyPending = false;
        QString fp = m_tabs[index].lazyFilePath;
        m_tabs[index].lazyFilePath.clear();
        if (!fp.isEmpty() && QFileInfo::exists(fp)) {
            m_tabs[index].editor->document()->loadFromFile(fp);
            m_tabs[index].preview->setDocumentDir(QFileInfo(fp).absolutePath());
            m_tabs[index].scheduler->parseNow();
        }
    }

    // 切换 tab 时更新 TOC 面板
    if (index >= 0 && index < m_tabs.size()) {
        auto* preview = m_tabs[index].preview;
        // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-COLLAPSE
        // 切 Tab 先更新 fileKey（触发折叠状态 load），再 setEntries
        QString fp = m_tabs[index].editor->document()->filePath();
        // 更新预览区文档目录，用于解析图片相对路径
        preview->setDocumentDir(fp.isEmpty() ? QString() : QFileInfo(fp).absolutePath());
        m_tocPanel->setCurrentFileKey(fp);
        m_tocPanel->setEntries(preview->tocEntries());
        m_tocPanel->setHighlightedEntries(preview->tocHighlightedIndices());

        // 文件夹面板同步选中当前文件
        m_folderPanel->selectFile(fp);

        // 切换到有待重载标记的 tab 时弹窗提示
        if (m_tabs[index].pendingReload)
            QTimer::singleShot(0, this, [this, index]() { promptReloadTab(index); });

        // 切换到被外部删除的 tab 时提示并关闭
        if (m_tabs[index].pendingDelete) {
            QTimer::singleShot(0, this, [this, index]() {
                if (index < 0 || index >= m_tabs.size() || !m_tabs[index].pendingDelete)
                    return;
                m_tabs[index].pendingDelete = false;
                QString path = m_tabs[index].editor->document()->filePath();
                QString name = QFileInfo(path).fileName();
                QMessageBox::information(
                    this, tr("File Deleted"),
                    tr("\"%1\" has been deleted by another program.\n\n"
                       "This tab will be closed.").arg(name));
                onCloseTab(index);
            });
        }

        // 应用当前显示区域模式（仅编辑器/仅预览/双栏）
        if (!m_focusMode) applyDisplayMode();

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
    // Spec: specs/模块-app/15-状态栏布局.md
    updateRightStatusBar();

    // P1 微交互：切换 tab 时内容区淡入（150ms，0.6→1.0）
    if (index >= 0 && index < m_tabs.size() && !m_focusMode) {
        auto* page = m_contentStack->currentWidget();
        if (page) {
            auto* effect = new QGraphicsOpacityEffect(page);
            page->setGraphicsEffect(effect);
            auto* anim = new QPropertyAnimation(effect, "opacity", this);
            anim->setDuration(150);
            anim->setStartValue(0.6);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            connect(anim, &QPropertyAnimation::finished, this, [page]() {
                page->setGraphicsEffect(nullptr);  // 动画结束移除 effect，避免持续性能开销
            });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

// -- 拖放 --

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
    // 为所有弹窗（QDialog、QMessageBox 等）自动设置标题栏配色
    if (event->type() == QEvent::Show) {
        QWidget* w = qobject_cast<QWidget*>(obj);
        if (w && w->isWindow() && w != this) {
            HWND hwnd = reinterpret_cast<HWND>(w->winId());
            BOOL useDark = m_currentTheme.isDark ? TRUE : FALSE;
            ::DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
            // Win11: 标题栏背景/文字颜色匹配主题
            COLORREF captionColor = RGB(m_currentTheme.editorGutterBg.red(),
                                        m_currentTheme.editorGutterBg.green(),
                                        m_currentTheme.editorGutterBg.blue());
            COLORREF textColor    = RGB(m_currentTheme.editorFg.red(),
                                        m_currentTheme.editorFg.green(),
                                        m_currentTheme.editorFg.blue());
            ::DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
            ::DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
        }
    }
#endif

    // FolderPanel 显隐联动左侧面板容器
    if (obj == m_folderPanel &&
        (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        // folderPanel 在容器内 hide/show，不影响 splitter 布局
        // 不需要额外处理
        updateLeftPaneVisibility();
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-MAX
    // 窗口缩小时 TOC 宽度不得超过屏幕 1/5
    clampTocWidthToScreen();
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_splitterInitialized) {
        m_splitterInitialized = true;

        // 先让 mainSplitter restoreState（仅作过渡，最终 sizes 由下方统一覆盖）
        if (!m_pendingSplitterState.isEmpty()) {
            m_mainSplitter->restoreState(m_pendingSplitterState);
        }
        m_pendingSplitterState.clear();

        // Spec: specs/模块-app/20-左侧面板.md INV-LP-WIDTH-DEFAULT
        // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-DEFAULT
        // 宽度策略：用户保存值优先；否则用"应用所在屏幕宽度 / 8"作为默认
        // 用屏幕宽度而非 m_mainSplitter->width() 作基准，避免最大化几何过渡期 width 仍为初始 1280
        // 导致默认值或 maxW 截断错误的时序耦合
        {
            QScreen* scr = nullptr;
            if (windowHandle()) scr = windowHandle()->screen();
            if (!scr) scr = QGuiApplication::primaryScreen();
            int screenW = (scr && scr->geometry().width() > 0) ? scr->geometry().width() : 1920;
            int defaultPanelW = screenW / 8;

            QSettings s;
            int savedLeftW = s.value("view/leftPanelWidth", -1).toInt();
            int savedTocW = s.value("view/tocPanelWidth", -1).toInt();
            bool leftShouldShow = !m_folderPanel->rootPaths().isEmpty() && !m_sidebarHidden;

            int leftW = (savedLeftW > 0) ? savedLeftW : (leftShouldShow ? defaultPanelW : 0);
            int tocW = (savedTocW > 0) ? savedTocW : defaultPanelW;

            // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-USER-OVERRIDE
            // 持久化的 TOC 宽度视为"用户意图"，避免启动后内容自适应（preferredWidthChanged）覆盖用户偏好
            if (savedTocW > 0) m_userDraggedToc = true;

            int totalW = m_mainSplitter->width();
            if (totalW <= 0) totalW = width();
            int contentW = qMax(100, totalW - leftW - tocW);
            m_mainSplitter->setSizes({leftW, contentW, tocW});
        }

        updateLeftPaneVisibility();

        // [Plan plans/2026-04-13-首次启动引导.md] 首次启动弹欢迎页
        // 延迟到下一个事件循环，避免和窗口初始化竞争
        QTimer::singleShot(100, this, &MainWindow::maybeShowWelcomeOnFirstLaunch);

        // Spec: specs/模块-app/16-崩溃报告收集.md
        // 上次崩溃后的 dump 检测：欢迎页之后再弹（避免对话框抢焦点）
        QTimer::singleShot(500, this, &MainWindow::maybeShowCrashReportPrompt);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 关闭窗口时先退出专注模式，以便正确保存布局状态
    if (m_focusMode)
        exitFocusMode();

    // 检查所有标签页是否有未保存的更改
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

        // 激活窗口
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
    // Spec: specs/模块-app/14-自动保存.md
    s.setValue("autoSave/enabled", m_autoSaveEnabled);
    s.setValue("autoSave/delayMs", m_autoSaveDelayMs);
    // Spec: specs/模块-app/12-主题插件系统.md
    // 主题：follow_system / <theme-id>（如 light / dark / liquid-glass / 用户自定义）
    QString themeMode = "follow_system";
    if (!m_currentThemeId.isEmpty()) themeMode = m_currentThemeId;
    s.setValue("view/themeMode", themeMode);
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/mainSplitter", m_mainSplitter->saveState());
    // 显式保存左侧面板/TOC 面板宽度（saveState 在面板显隐变化或窗口几何转换时可能无法精确恢复）
    // Spec: specs/模块-app/20-左侧面板.md INV-LP-WIDTH-DEFAULT、specs/模块-preview/07-TOC面板.md
    {
        auto sizes = m_mainSplitter->sizes();
        if (m_leftPaneSplitter->isVisible() && sizes.size() >= 1 && sizes[0] > 0)
            s.setValue("view/leftPanelWidth", sizes[0]);
        if (sizes.size() >= 3 && sizes[2] > 0)
            s.setValue("view/tocPanelWidth", sizes[2]);
    }
    // Tab 栏位置
    s.setValue("view/tabBarOnSide", m_tabBarOnSide);
    s.setValue("view/hideTopBarWhenSide", m_hideTopBarWhenSide);
    // 文件夹面板：持久化所有打开的目录
    s.setValue("folderPanel/rootPaths", m_folderPanel->rootPaths());

    // 会话恢复
    s.setValue("session/restoreLastFile", m_restoreSessionAct ? m_restoreSessionAct->isChecked() : true);
    s.setValue("session/activeTab", m_tabBar->currentIndex());

    // 保存所有标签页
    s.beginWriteArray("session/tabs");
    int written = 0;
    for (int i = 0; i < m_tabs.size(); ++i) {
        QString fp = m_tabs[i].editor->document()->filePath();
        // 懒加载 tab 尚未加载文件，从 lazyFilePath 获取路径
        if (fp.isEmpty() && m_tabs[i].lazyPending)
            fp = m_tabs[i].lazyFilePath;
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

    // Spec: specs/模块-app/12-主题插件系统.md
    // 主题：向后兼容旧的 "light"/"dark" 字符串，同时接受任意主题 id
    QString themeMode = s.value("view/themeMode", "follow_system").toString();
    if (themeMode == "follow_system") {
        if (m_followSystemThemeAct) m_followSystemThemeAct->setChecked(true);
        applySystemTheme();
    } else {
        // 按 id 切换，并在菜单中勾选对应项
        applyThemeById(themeMode);
        rebuildThemeMenu();  // 触发重新选中（rebuild 会根据 m_currentThemeId 设 checked）
    }

    // 自动换行
    bool wordWrap = s.value("view/wordWrap", true).toBool();
    m_wordWrapAct->setChecked(wordWrap);

    // Tab 栏位置（默认 "全部显示"：侧边 + 顶部都显示）
    bool tabOnSide = s.value("view/tabBarOnSide", true).toBool();
    bool hideTop = s.value("view/hideTopBarWhenSide", false).toBool();
    if (tabOnSide) setTabBarPosition(true, hideTop);


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

    // Spec: specs/模块-app/14-自动保存.md
    m_autoSaveEnabled = s.value("autoSave/enabled", true).toBool();
    m_autoSaveDelayMs = s.value("autoSave/delayMs", 1500).toInt();
    // 兜底：只接受 1500/3000/5000 三档
    if (m_autoSaveDelayMs != 1500 && m_autoSaveDelayMs != 3000 && m_autoSaveDelayMs != 5000)
        m_autoSaveDelayMs = 1500;
    if (m_autoSaveEnabledAct) m_autoSaveEnabledAct->setChecked(m_autoSaveEnabled);
    for (QAction* a : m_autoSaveDelayActions) {
        if (a->data().toInt() == m_autoSaveDelayMs) {
            a->setChecked(true);
            break;
        }
    }

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
        // [Spec 模块-preview/02 INV-13] 重启后从 QSettings 读出的行间距
        // 必须同步推送到预览区，否则重启后预览仍按 PreviewLayout 默认乘数
        // 渲染（典型表现：用户上次设了 1.0，重启后预览仍是 1.5）
        tab.preview->setLineSpacingFactor(m_lineSpacingFactor);
        tab.editor->setWordWrap(wordWrap);
        tab.preview->setWordWrap(wordWrap);
    }

    // 文件夹面板：恢复上次打开的目录（兼容旧版单路径和新版多路径）
    QStringList folderPaths = s.value("folderPanel/rootPaths").toStringList();
    if (folderPaths.isEmpty()) {
        // 兼容旧版配置
        QString single = s.value("folderPanel/rootPath").toString();
        if (!single.isEmpty()) folderPaths.append(single);
    }
    for (const auto& fp : folderPaths) {
        if (!fp.isEmpty() && QDir(fp).exists())
            m_folderPanel->addFolder(fp);
    }
    if (!m_folderPanel->rootPaths().isEmpty())
        m_folderPanel->setTheme(m_currentTheme);

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
        for (int i = 0; i < m_tabBar->count(); ++i) {
            if (m_tabBar->tabButton(i, QTabBar::RightSide) == btn) {
                onCloseTab(i);
                return;
            }
        }
    });

    m_tabBar->setTabButton(index, QTabBar::RightSide, btn);
}

void MainWindow::updateAllTabCloseButtons()
{
    for (int i = 0; i < m_tabBar->count(); ++i)
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

    // 文件被删除的情况
    if (!QFileInfo::exists(path)) {
        // 标记为已修改（磁盘文件已不存在，内存内容需要重新保存）
        m_tabs[tabIndex].editor->document()->setModified(true);
        updateTabTitle(tabIndex);

        if (tabIndex == m_tabBar->currentIndex()) {
            // 当前 tab：提示用户，可继续编辑并 Ctrl+S 重新保存
            QString name = QFileInfo(path).fileName();
            QMessageBox::information(
                this, tr("File Deleted"),
                tr("\"%1\" has been deleted by another program.\n\n"
                   "You can continue editing and press Ctrl+S to save it again.").arg(name));
        } else {
            // 非当前 tab：标记待删除，切换时提示并关闭
            m_tabs[tabIndex].pendingDelete = true;
        }
        return;
    }

    // 重新添加监控（某些系统修改后会自动移除）
    watchFile(path);

    // 非当前 tab：标记待重载，切换时再弹窗
    if (tabIndex != m_tabBar->currentIndex()) {
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

    // Spec: specs/模块-app/15-状态栏布局.md
    // 左区：addWidget（贴左）；右区：addPermanentWidget（贴右）。中间 QStatusBar 自然 stretch。
    auto makeLeftLabel = [sb](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, sb);
        label->setContentsMargins(8, 0, 8, 0);
        sb->addWidget(label);
        return label;
    };
    auto makeRightLabel = [sb](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, sb);
        label->setContentsMargins(8, 0, 8, 0);
        sb->addPermanentWidget(label);
        return label;
    };

    // 左区：当前编辑信息
    m_statusCursorPos = makeLeftLabel(tr("Ln %1, Col %2").arg(1).arg(1));
    m_statusLineCount = makeLeftLabel(tr("Lines: %1").arg(0));
    m_statusWordCount = makeLeftLabel(tr("Words: %1").arg(0));
    m_statusCharCount = makeLeftLabel(tr("Chars: %1").arg(0));
    m_statusReadTime = makeLeftLabel(tr("Read: <1 min"));

    // 右区：文档元数据 + 保存状态（不含主题名 — 主题从 UI 颜色直接感知）
    m_statusEncoding   = makeRightLabel(QStringLiteral("UTF-8"));
    m_statusLineEnding = makeRightLabel(QStringLiteral("LF"));
    m_statusSaveStatus = makeRightLabel(tr("Unsaved (new file)"));

    // Spec: specs/模块-app/14-自动保存.md
    // 自动保存失败提示 label：默认空字符串、隐式占位；失败时填字 + 5s 后清空
    m_statusAutoSaveMsg = makeRightLabel(QString());
    m_statusAutoSaveMsg->setStyleSheet(QStringLiteral("color: #C62828;"));  // 醒目红
    m_statusAutoSaveMsg->hide();  // 平时不占地

    m_autoSaveMsgClearTimer.setSingleShot(true);
    m_autoSaveMsgClearTimer.setInterval(5000);
    connect(&m_autoSaveMsgClearTimer, &QTimer::timeout, this, [this]() {
        if (m_statusAutoSaveMsg) {
            m_statusAutoSaveMsg->setText(QString());
            m_statusAutoSaveMsg->hide();
        }
    });

    // Spec: specs/模块-app/14-自动保存.md
    // 自动保存：debounce 定时器；textChanged → start，timeout → performAutoSave
    m_autoSaveTimer.setSingleShot(true);
    m_autoSaveTimer.setInterval(m_autoSaveDelayMs);
    connect(&m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::performAutoSave);

    // 防抖定时器：文本变化后 300ms 才更新统计（避免频繁计算）
    m_statsDebounceTimer.setSingleShot(true);
    m_statsDebounceTimer.setInterval(300);
    connect(&m_statsDebounceTimer, &QTimer::timeout, this, &MainWindow::updateStatusBarStats);

    // Spec: specs/模块-app/15-状态栏布局.md
    // 30s 周期重算"已保存 · X 分钟前"相对时间（不访问磁盘，只重算字符串）
    m_relTimeRefreshTimer.setSingleShot(false);
    m_relTimeRefreshTimer.setInterval(30 * 1000);
    connect(&m_relTimeRefreshTimer, &QTimer::timeout, this, &MainWindow::updateSaveStatusOnly);
    m_relTimeRefreshTimer.start();
}

void MainWindow::connectTabStatusBar(const TabData& tab)
{
    // 文本变化 → 防抖更新统计 + 触发自动保存
    // Spec: specs/模块-app/14-自动保存.md
    connect(tab.editor->document(), &Document::textChanged,
            this, [this]() {
        m_statsDebounceTimer.start();
        scheduleAutoSave();
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

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-DOCCARD-NO-REPARSE
    // 同步推送文档摘要到 TocPanel 的 DocInfoCard（复用上面算好的结果，不重复 parse）
    DocInfo di;
    di.wordCount = wordCount;
    di.charCount = text.length();
    di.charCountNoSpace = charsNoSpace;
    di.lineCount = lineCount;
    const QString fp = tab->editor->document()->filePath();
    if (!fp.isEmpty()) {
        QFileInfo fi(fp);
        if (fi.exists()) {
            di.sizeBytes = fi.size();
            di.mtime = fi.lastModified();
        }
    }
    // frontmatter title/tags 留待 v2 从 AST 提取，当前传空
    m_tocPanel->setDocumentInfo(di);
}

// ---- 自动保存 ----
// Spec: specs/模块-app/14-自动保存.md
// INV-1: 仅保存有磁盘路径的 Tab；未命名（filePath 空）跳过、不弹文件对话框
// INV-2: 失败不打断输入，状态栏右下显示瞬时提示
// INV-3: textChanged 重置 timer，连续输入只在最后一次停顿后保存

void MainWindow::scheduleAutoSave()
{
    if (!m_autoSaveEnabled) return;
    // restart：每次编辑都重置 debounce
    m_autoSaveTimer.start(m_autoSaveDelayMs);
}

void MainWindow::performAutoSave()
{
    if (!m_autoSaveEnabled) return;

    QStringList failedNames;
    for (int i = 0; i < m_tabs.size(); ++i) {
        Document* doc = m_tabs[i].editor->document();
        if (!doc) continue;
        const QString fp = doc->filePath();
        if (fp.isEmpty()) continue;            // INV-1：未命名跳过
        if (m_tabs[i].pendingDelete) continue; // 文件已被外部删除，切换时关闭，不自动重建
        if (!doc->isModified()) continue;      // 没改过的也跳过

        // 与手动保存对齐：先 unwatch，避免 self-trigger 文件外部修改信号
        unwatchFile(fp);
        const bool ok = doc->saveToFile();     // 用当前 filePath 保存
        if (ok) {
            updateTabTitle(i);
            // 异步 re-watch（与手动保存一致）
            QTimer::singleShot(500, this, [this, fp]() { watchFile(fp); });
        } else {
            failedNames.append(QFileInfo(fp).fileName());
        }
    }

    if (!failedNames.isEmpty()) {
        // INV-2：失败不弹对话框，仅状态栏提示
        showAutoSaveError(tr("Auto save failed: %1").arg(failedNames.join(QStringLiteral(", "))));
    }

    // 自动保存时同步持久化会话状态（Tab 列表、滚动位置等），
    // 防止进程被强杀时丢失会话信息
    saveSettings();

    // Spec: specs/模块-app/15-状态栏布局.md
    updateSaveStatusOnly();
}

void MainWindow::showAutoSaveError(const QString& message)
{
    if (!m_statusAutoSaveMsg) return;
    m_statusAutoSaveMsg->setText(message);
    m_statusAutoSaveMsg->show();
    m_autoSaveMsgClearTimer.start();
}

// ---- 状态栏右区元数据 ----
// Spec: specs/模块-app/15-状态栏布局.md

namespace {

// 把"距 ts 经过的秒数"格式化为相对时间字符串（中文 + 英文模板由 tr 处理）
// 返回的是给 QObject::tr 用的 source 模板 + arg 值，由调用方 tr() arg() 补全。
struct SaveStatusText {
    enum Kind { JustSaved, MinutesAgo, HoursAgo, DaysAgo };
    Kind kind = JustSaved;
    int  value = 0;
};

SaveStatusText classifySaveTime(const QDateTime& mtime) {
    SaveStatusText r;
    if (!mtime.isValid()) {
        r.kind = SaveStatusText::JustSaved;
        return r;
    }
    const qint64 secs = mtime.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        r.kind = SaveStatusText::JustSaved;
    } else if (secs < 3600) {
        r.kind = SaveStatusText::MinutesAgo;
        r.value = static_cast<int>(secs / 60);
    } else if (secs < 86400) {
        r.kind = SaveStatusText::HoursAgo;
        r.value = static_cast<int>(secs / 3600);
    } else {
        r.kind = SaveStatusText::DaysAgo;
        r.value = static_cast<int>(secs / 86400);
    }
    return r;
}

} // namespace

void MainWindow::updateRightStatusBar()
{
    if (!m_statusEncoding) return;  // 还未 setupStatusBar

    // 编码：项目硬约束 UTF-8（参见 INV-CODE-UTF8 + Document::loadFromFile 行为）
    m_statusEncoding->setText(QStringLiteral("UTF-8"));

    auto* tab = currentTab();
    if (!tab) {
        m_statusLineEnding->setText(QStringLiteral("LF"));
        m_statusSaveStatus->setText(tr("Unsaved (new file)"));
        return;
    }

    // 换行风格
    Document* doc = tab->editor->document();
    const auto le = doc->detectedLineEnding();
    m_statusLineEnding->setText(le == Document::CRLF
        ? QStringLiteral("CRLF")
        : QStringLiteral("LF"));

    // 保存状态
    updateSaveStatusOnly();
}

void MainWindow::updateSaveStatusOnly()
{
    if (!m_statusSaveStatus) return;
    auto* tab = currentTab();
    if (!tab) {
        m_statusSaveStatus->setText(tr("Unsaved (new file)"));
        return;
    }
    Document* doc = tab->editor->document();
    const QString fp = doc->filePath();

    if (fp.isEmpty()) {
        // 未命名文档
        m_statusSaveStatus->setText(tr("Unsaved (new file)"));
        m_statusSaveStatus->setStyleSheet(QString());
        return;
    }
    if (doc->isModified()) {
        // 有路径但未保存修改
        m_statusSaveStatus->setText(tr("Unsaved changes"));
        m_statusSaveStatus->setStyleSheet(QStringLiteral("color: #C62828;"));
        return;
    }

    // 已保存：计算相对时间（不访问磁盘多余字段，只读 lastModified）
    QFileInfo fi(fp);
    const QDateTime mtime = fi.exists() ? fi.lastModified() : QDateTime();
    const auto t = classifySaveTime(mtime);
    QString text;
    switch (t.kind) {
        case SaveStatusText::JustSaved:
            text = tr("Saved · just now");
            break;
        case SaveStatusText::MinutesAgo:
            text = tr("Saved · %1 min ago", "", t.value).arg(t.value);
            break;
        case SaveStatusText::HoursAgo:
            text = tr("Saved · %1 hour ago", "", t.value).arg(t.value);
            break;
        case SaveStatusText::DaysAgo:
            text = tr("Saved · %1 day ago", "", t.value).arg(t.value);
            break;
    }
    m_statusSaveStatus->setText(text);
    m_statusSaveStatus->setStyleSheet(QString());
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
    QString current = s.value("language/locale", "zh_CN").toString();
    if (current == locale) return;

    s.setValue("language/locale", locale);

    // 双按钮对话框：重启 / 确认
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Language Changed"));
    msgBox.setText(tr("Language will take effect after restart."));
    QPushButton* restartBtn = msgBox.addButton(tr("Restart Now"), QMessageBox::AcceptRole);
    msgBox.addButton(tr("OK"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(restartBtn);
    msgBox.exec();

    if (msgBox.clickedButton() == restartBtn) {
        // 保存会话后重启
        saveSettings();
        QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
        QApplication::quit();
    }
}

void MainWindow::retranslateUi()
{
    // 预留：如果未来需要运行时语言切换，在此重建菜单和 UI 文本
}

// ---- 演示模式 ----
// [Spec 模块-app/11-演示模式] Plan 裁定方案 A：替换原 Focus Mode
// 进入时：全屏 + 隐藏编辑器/菜单/Tab/TOC，只显示当前 Tab 的预览
// 退出时：恢复所有原布局
// ---- 底部居中 toast 通知 ----

void MainWindow::showToast(const QString& message, int durationMs)
{
    auto* toast = new QLabel(message, this);
    toast->setAlignment(Qt::AlignCenter);
    toast->setWordWrap(true);
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    toast->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background: %1; color: %2;"
        "  border-radius: 6px; padding: 8px 20px;"
        "  font-size: 13px;"
        "}"
    ).arg(m_currentTheme.isDark ? "rgba(255,255,255,0.15)" : "rgba(0,0,0,0.75)",
          m_currentTheme.isDark ? "#ddd" : "#fff"));

    toast->adjustSize();
    int x = (width() - toast->width()) / 2;
    int y = height() - toast->height() - 40;
    toast->move(x, y);
    toast->show();

    // 3s 后淡出
    QTimer::singleShot(durationMs, this, [toast]() {
        auto* effect = new QGraphicsOpacityEffect(toast);
        toast->setGraphicsEffect(effect);
        auto* anim = new QPropertyAnimation(effect, "opacity", toast);
        anim->setDuration(300);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, toast, &QLabel::deleteLater);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

// ---- 显示区域切换 ----

void MainWindow::setDisplayMode(int mode)
{
    m_displayMode = mode;
    applyDisplayMode();
    saveSessionLater();
}

void MainWindow::applyDisplayMode()
{
    auto* tab = currentTab();
    if (!tab) return;
    switch (m_displayMode) {
    case 0: // 双栏
        tab->editor->show();
        tab->preview->show();
        // 恢复 50/50 比例（如果某侧被隐藏过）
        if (tab->splitter->sizes()[0] == 0 || tab->splitter->sizes()[1] == 0)
            tab->splitter->setSizes({1, 1});
        break;
    case 1: // 仅编辑器
        tab->editor->show();
        tab->preview->hide();
        tab->splitter->setSizes({1, 0});
        break;
    case 2: // 仅预览
        tab->editor->hide();
        tab->preview->show();
        tab->splitter->setSizes({0, 1});
        break;
    }
}

// ---- Tab 栏位置切换 ----

void MainWindow::setTabBarPosition(bool onSide, bool hideTopBar)
{
    // 切换 Tab 栏位置时，自动取消用户手动隐藏
    m_sidebarHidden = false;
    if (m_toggleSidebarAct) m_toggleSidebarAct->setChecked(true);

    // 关闭侧边模式
    if (!onSide) {
        if (!m_tabBarOnSide) return;  // 已经是顶部模式
        m_tabBarOnSide = false;
        m_hideTopBarWhenSide = true;
        m_sideTabBar->hide();
        m_sideTabBar->setParent(this);
        m_tabBar->setVisible(true);
        updateLeftPaneVisibility();
        if (m_tabPosTopAct) m_tabPosTopAct->setChecked(true);
        saveSessionLater();
        return;
    }

    // 开启或切换侧边模式
    m_hideTopBarWhenSide = hideTopBar;

    if (!m_tabBarOnSide) {
        // 首次切换到侧边模式
        m_tabBarOnSide = true;
        syncSideTabBar();
        m_leftPaneSplitter->addWidget(m_sideTabBar);
        m_leftPaneSplitter->setCollapsible(m_leftPaneSplitter->indexOf(m_sideTabBar), false);
        m_sideTabBar->show();
        m_sideTabBar->setTheme(m_currentTheme);
        // 默认比例：文件夹 2/3，Tab 栏 1/3
        int totalH = m_leftPaneSplitter->height();
        if (totalH < 100) totalH = 600;
        m_leftPaneSplitter->setSizes({totalH * 2 / 3, totalH / 3});
        m_leftPaneSplitter->show();
        QList<int> mainSizes = m_mainSplitter->sizes();
        int totalW = m_mainSplitter->width();
        // 左侧面板宽度：默认 1/8，最大 1/5
        int minW = totalW / 8;
        int maxW = totalW / 5;
        if (mainSizes.size() >= 2 && mainSizes[0] < minW) {
            mainSizes[0] = qBound(minW, totalW / 8, maxW);
            m_mainSplitter->setSizes(mainSizes);
        }
    }

    // 顶部 tab 栏：根据 hideTopBar 决定显隐
    m_tabBar->setVisible(!hideTopBar);

    updateLeftPaneVisibility();
    // 更新菜单勾选状态
    if (onSide && !hideTopBar && m_tabPosShowAllAct) m_tabPosShowAllAct->setChecked(true);
    else if (onSide && hideTopBar && m_tabPosSideAct) m_tabPosSideAct->setChecked(true);
    saveSessionLater();
}

void MainWindow::syncSideTabBar()
{
    m_sideTabBar->clear();
    for (int i = 0; i < m_tabBar->count(); ++i)
        m_sideTabBar->addTab(m_tabBar->tabText(i));
    m_sideTabBar->setCurrentIndex(m_tabBar->currentIndex());
}

void MainWindow::updateEmptyState()
{
    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-WELCOME-MUTUAL、INV-EMPTY-TOC-HIDE
    // m_tabs 为空：WelcomePanel 顶替 contentStack，且右侧 TocPanel 一并隐藏
    //（没有打开的页面就没有目录/文档信息要展示，挂着空卡片只是无意义 chrome）
    const bool empty = m_tabs.isEmpty();
    if (m_welcomePanel) m_welcomePanel->setVisible(empty);
    if (m_contentStack) m_contentStack->setVisible(!empty);
    // 演示模式下 TocPanel 由 m_savedTocVisible 独立控制（enterFocusMode/exitFocusMode），
    // 不与本机制叠加；T-9 保证演示模式与空 Tab 不会同时成立
    if (m_tocPanel && !m_focusMode) m_tocPanel->setVisible(!empty);
}

void MainWindow::updateLeftPaneVisibility()
{
    // 演示模式下左侧面板始终隐藏
    if (m_focusMode) {
        m_leftPaneSplitter->hide();
        return;
    }
    // 用户通过 Ctrl+B 手动隐藏时，保持隐藏
    if (m_sidebarHidden) {
        m_leftPaneSplitter->hide();
        return;
    }
    // 无论 Tab 栏位置，只在有打开的文件夹时才显示左侧面板
    bool shouldShow = !m_folderPanel->rootPaths().isEmpty();
    bool wasHidden = !m_leftPaneSplitter->isVisible();
    m_leftPaneSplitter->setVisible(shouldShow);

    // 从隐藏变为可见时，若 sizes[0] 为 0 则补充宽度
    // Spec: specs/模块-app/20-左侧面板.md INV-LP-WIDTH-DEFAULT
    // 优先使用用户保存的宽度，否则用应用所在屏幕宽度的 1/8
    if (shouldShow && wasHidden && m_splitterInitialized && m_mainSplitter) {
        auto sizes = m_mainSplitter->sizes();
        if (sizes.size() >= 3 && sizes[0] <= 0) {
            QSettings s;
            int savedLeftW = s.value("view/leftPanelWidth", -1).toInt();
            QScreen* scr = nullptr;
            if (windowHandle()) scr = windowHandle()->screen();
            if (!scr) scr = QGuiApplication::primaryScreen();
            int screenW = (scr && scr->geometry().width() > 0) ? scr->geometry().width() : 1920;
            int leftW = (savedLeftW > 0) ? savedLeftW : (screenW / 8);

            int totalW = m_mainSplitter->width();
            if (totalW <= 0) totalW = width();
            int otherW = 0;
            for (int i = 2; i < sizes.size(); ++i) otherW += sizes[i];
            sizes[0] = leftW;
            sizes[1] = qMax(100, totalW - leftW - otherW);
            m_mainSplitter->setSizes(sizes);
        }
    }
}

void MainWindow::toggleSidebar()
{
    m_sidebarHidden = !m_sidebarHidden;
    m_toggleSidebarAct->setChecked(!m_sidebarHidden);
    updateLeftPaneVisibility();
    saveSettings();
}

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

    // 隐藏菜单栏、状态栏、tab 栏、左侧面板
    menuBar()->hide();
    statusBar()->hide();
    m_tabBar->hide();
    m_leftPaneSplitter->hide();

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

    // mainSplitter：所有宽度给 contentStack（中间）
    m_mainSplitter->setSizes({0, 1, 0});

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

    // 恢复菜单栏、状态栏
    menuBar()->show();
    statusBar()->show();
    // 恢复 tab 栏（根据侧边模式决定显隐）
    m_tabBar->setVisible(!m_tabBarOnSide || !m_hideTopBarWhenSide);
    // 恢复左侧面板
    updateLeftPaneVisibility();

    // 恢复 TOC 面板
    m_tocPanel->setVisible(m_savedTocVisible);

    // [演示模式] 恢复编辑器显示（根据 displayMode）
    for (auto& t : m_tabs) {
        t.editor->show();
        t.preview->show();
    }
    applyDisplayMode();

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

// ---- 文档统计弹窗 [Plan plans/2026-04-13-文档统计信息.md] ----

void MainWindow::onShowDocumentStats()
{
    auto* tab = currentTab();
    if (!tab) return;

    const QString text = tab->editor->document()->text();

    // 基础字数/字符数/行数（复用状态栏逻辑）
    const int totalChars = text.length();
    int charsNoSpace = 0;
    for (const QChar& ch : text) if (!ch.isSpace()) ++charsNoSpace;
    const int lineCount = tab->editor->document()->lineCount();

    // Word count（中文按字 + 英文按词）
    int wordCount = 0;
    bool inWord = false;
    for (const QChar& ch : text) {
        ushort uc = ch.unicode();
        bool isCJK = (uc >= 0x4E00 && uc <= 0x9FFF)
                  || (uc >= 0x3400 && uc <= 0x4DBF)
                  || (uc >= 0xF900 && uc <= 0xFAFF);
        if (isCJK) {
            if (inWord) { ++wordCount; inWord = false; }
            ++wordCount;
        } else if (ch.isLetterOrNumber()) {
            inWord = true;
        } else {
            if (inWord) { ++wordCount; inWord = false; }
        }
    }
    if (inWord) ++wordCount;

    // 结构统计：用正则扫源码，避免依赖 AST 指针
    int h1 = 0, h2 = 0, h3 = 0, h4 = 0, h5 = 0, h6 = 0;
    int paragraphCount = 0;
    int codeBlockCount = 0;
    int imageCount = 0;
    int linkCount = 0;
    int tableCount = 0;
    int blockQuoteCount = 0;

    const QStringList lines = text.split('\n');
    bool inCodeFence = false;
    bool prevBlank = true;
    for (const QString& raw : lines) {
        QString line = raw;
        QString trimmed = line.trimmed();

        // 代码围栏
        if (trimmed.startsWith("```") || trimmed.startsWith("~~~")) {
            if (!inCodeFence) ++codeBlockCount;
            inCodeFence = !inCodeFence;
            prevBlank = false;
            continue;
        }
        if (inCodeFence) { prevBlank = false; continue; }

        // 标题
        if (trimmed.startsWith("#")) {
            int level = 0;
            while (level < trimmed.size() && trimmed[level] == '#') ++level;
            if (level >= 1 && level <= 6
                && (level == trimmed.size() || trimmed[level] == ' ')) {
                switch (level) {
                    case 1: ++h1; break; case 2: ++h2; break; case 3: ++h3; break;
                    case 4: ++h4; break; case 5: ++h5; break; case 6: ++h6; break;
                }
                prevBlank = false;
                continue;
            }
        }

        // 引用块
        if (trimmed.startsWith(">")) ++blockQuoteCount;

        // 表格（至少两行 | 分隔）
        if (trimmed.contains('|') && trimmed.contains("---")) ++tableCount;

        // 段落：非空行且前一行是空行
        if (!trimmed.isEmpty() && prevBlank) ++paragraphCount;

        prevBlank = trimmed.isEmpty();
    }

    // 图片 / 链接：用正则全文扫
    QRegExp imgRe("!\\[[^\\]]*\\]\\([^)]+\\)");
    int pos = 0;
    while ((pos = imgRe.indexIn(text, pos)) != -1) { ++imageCount; pos += imgRe.matchedLength(); }
    QRegExp linkRe("(?<!!)\\[[^\\]]*\\]\\([^)]+\\)");
    pos = 0;
    while ((pos = linkRe.indexIn(text, pos)) != -1) { ++linkCount; pos += linkRe.matchedLength(); }

    const int minutes = qMax(1, wordCount / 300);

    const QString title = tr("Document Statistics");
    const QString body = QString(
        "<table cellpadding='3'>"
        "<tr><td><b>%1</b></td><td>%2</td></tr>"
        "<tr><td><b>%3</b></td><td>%4 (%5 %6)</td></tr>"
        "<tr><td><b>%7</b></td><td>%8</td></tr>"
        "<tr><td><b>%9</b></td><td>%10</td></tr>"
        "<tr><td><b>%11</b></td><td>%12</td></tr>"
        "<tr><td><b>%13</b></td><td>H1:%14 H2:%15 H3:%16 H4:%17 H5:%18 H6:%19</td></tr>"
        "<tr><td><b>%20</b></td><td>%21</td></tr>"
        "<tr><td><b>%22</b></td><td>%23</td></tr>"
        "<tr><td><b>%24</b></td><td>%25</td></tr>"
        "<tr><td><b>%26</b></td><td>%27</td></tr>"
        "<tr><td><b>%28</b></td><td>%29</td></tr>"
        "<tr><td><b>%30</b></td><td>%31 min</td></tr>"
        "</table>"
    )
    .arg(tr("Words:")).arg(wordCount)
    .arg(tr("Characters:")).arg(totalChars).arg(charsNoSpace).arg(tr("no space"))
    .arg(tr("Lines:")).arg(lineCount)
    .arg(tr("Paragraphs:")).arg(paragraphCount)
    .arg(tr("Headings:")).arg(h1 + h2 + h3 + h4 + h5 + h6)
    .arg(tr("By Level:")).arg(h1).arg(h2).arg(h3).arg(h4).arg(h5).arg(h6)
    .arg(tr("Code Blocks:")).arg(codeBlockCount)
    .arg(tr("Images:")).arg(imageCount)
    .arg(tr("Links:")).arg(linkCount)
    .arg(tr("Tables:")).arg(tableCount)
    .arg(tr("Block Quotes:")).arg(blockQuoteCount)
    .arg(tr("Reading Time:")).arg(minutes);

    QMessageBox::information(this, title, body);
}

// ---- 首次启动欢迎对话框 [Plan plans/2026-04-13-首次启动引导.md] ----

void MainWindow::onShowWelcome()
{
    const QString body = QString(
        "<h2>SimpleMarkdown</h2>"
        "<p><i>%1</i></p>"
        "<hr>"
        "<h3>%2</h3>"
        "<ul>"
        "<li>%3</li>"
        "<li>%4</li>"
        "<li>%5</li>"
        "<li>%6</li>"
        "<li>%7</li>"
        "<li>%8</li>"
        "<li>%9</li>"
        "</ul>"
        "<p><b>%10</b></p>"
    )
    .arg(tr("A lightweight, fast, cross-platform Markdown editor."))
    .arg(tr("Key Features"))
    .arg(tr("Dual-pane view: edit on the left, live preview on the right"))
    .arg(tr("Multi-tab workspace with drag-and-drop file support"))
    .arg(tr("Full-text search with highlight (Ctrl+F), Ctrl+click to follow links"))
    .arg(tr("Outline/TOC panel for quick heading navigation"))
    .arg(tr("Content marking (highlighter effect) for key passages"))
    .arg(tr("Presentation Mode: press F11 to full-screen the preview"))
    .arg(tr("Light / dark themes with system-follow option"))
    .arg(tr("Tip: press Ctrl+/ or open Help → Keyboard Shortcuts to see all shortcuts."));

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Welcome to SimpleMarkdown"));
    dlg.setMinimumWidth(500);

    QLabel* label = new QLabel(body, &dlg);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);

    QPushButton* closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->addWidget(label);
    layout->addWidget(closeBtn);

    dlg.exec();
}

void MainWindow::maybeShowWelcomeOnFirstLaunch()
{
    QSettings s;
    QString lastSeen = s.value("session/firstLaunchedVersion").toString();
    if (lastSeen.isEmpty()) {
        // 首次启动
        onShowWelcome();
        s.setValue("session/firstLaunchedVersion", "0.2.4");
    }
}

// Spec: specs/模块-app/16-崩溃报告收集.md
// 启动后检测 %APPDATA%/SimpleMarkdown/crashes/ 下的 crash.dmp，
// 若 mtime 新于上次"已查看"标记 → 弹窗提示用户上次崩溃，并给"打开崩溃报告目录"按钮。
void MainWindow::maybeShowCrashReportPrompt()
{
    // Spec: specs/模块-app/16-崩溃报告收集.md
    // 路径必须与 main.cpp 的 prepareCrashDir 完全一致：<APPDATA>/SimpleMarkdown/crashes
    // （单层 SimpleMarkdown，不走 QStandardPaths::AppDataLocation 的双层 <Org>/<App>）
    const QByteArray appdata = qgetenv("APPDATA");
    if (appdata.isEmpty()) return;
    const QString crashDir = QString::fromLocal8Bit(appdata) + QStringLiteral("/SimpleMarkdown/crashes");
    const QString dmpPath = crashDir + QStringLiteral("/crash.dmp");

    QFileInfo fi(dmpPath);
    if (!fi.exists()) return;

    QSettings s;
    const qint64 lastSeen = s.value(QStringLiteral("crashes/lastSeenMtimeMs"), 0).toLongLong();
    const qint64 dumpMtime = fi.lastModified().toMSecsSinceEpoch();
    if (dumpMtime <= lastSeen) return;  // 已经看过，不再打扰

    // 标记已查看（即便用户点 No 也算"已通知"，避免反复弹）
    s.setValue(QStringLiteral("crashes/lastSeenMtimeMs"), dumpMtime);

    QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        tr("Crash Report Found"),
        tr("SimpleMarkdown unexpectedly closed last time.\n"
           "A crash report has been saved at:\n\n  %1\n\n"
           "Open the crash reports folder?")
            .arg(QDir::toNativeSeparators(crashDir)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(crashDir));
    }
}

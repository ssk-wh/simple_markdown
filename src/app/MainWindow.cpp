#include "MainWindow.h"
#include "../core/PerfProbe.h"
#include "ViewerWidget.h"
#include "MarkdownParser.h"
#include "TocPanel.h"
#include "RecentFiles.h"
#include "ChangelogDialog.h"
#include "ShortcutsDialog.h"
#include "WelcomePanel.h"  // Spec: specs/模块-app/22-空白引导页.md
#include "UpdateChecker.h"  // Spec: specs/模块-app/23-检查更新.md
#include "PreviewLayout.h"
#include "FontDefaults.h"
#include "SnapSplitter.h"
#include "FolderPanel.h"
#include "SideTabBar.h"

#include <QSplitter>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
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
    m_qtTranslator = new QTranslator(this);
    {
        QSettings s;
        QString locale = s.value("language/locale", "zh_CN").toString();
        // Spec INV-3：先加载 Qt 内置 widget 翻译（标准按钮 Yes/No/Save、对话框等），
        // 否则中文界面下 QMessageBox 等标准按钮仍显示英文。qm 嵌入 qrc 以保证部署后
        // 用户机也能加载（不依赖 Qt 安装路径）。英文 locale 无 qt_en_US.qm，load 失败即跳过。
        const bool qtOk = m_qtTranslator->load(":/translations/qt_" + locale + ".qm");
        if (qtOk)
            qApp->installTranslator(m_qtTranslator);
        if (m_translator->load(":/translations/simple_markdown_" + locale + ".qm"))
            qApp->installTranslator(m_translator);
    }

    m_recentFiles = new RecentFiles(this);

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-VALIGN
    // 拆 QTabWidget 为 QTabBar + QStackedWidget，TOC 不再与 tabBar 顶齐
    // central widget = QVBoxLayout(tabBar + mainSplitter(contentStack|tocPanel))
    // Spec: specs/模块-app/04-窗口焦点管理.md INV-6
    // m_tabBarBox 是「Tab 栏 + 「+」按钮」容器；m_tabBar 指向其内部 QTabBar，
    // 后续所有 tab 操作仍用 m_tabBar，可见性 / 主题色用容器 m_tabBarBox。
    m_tabBarBox = new TabBarWithAdd(this);
    m_tabBar = m_tabBarBox->bar();
    connect(m_tabBarBox, &TabBarWithAdd::addClicked, this, &MainWindow::onOpenFile);
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
    connect(m_welcomePanel, &WelcomePanel::newFileClicked, this, &MainWindow::onOpenFile);

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
    connect(m_sideTabBar, &SideTabBar::addClicked, this, &MainWindow::onOpenFile);

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
    m_centralLayout->addWidget(m_tabBarBox);
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
            tab->viewer->scrollToSourceLine(sourceLine, true);
    });

    // [2026-05-13] Ctrl+1..9 章节跳转功能已移除——A1 视口剪裁让 sourceLineToY 累加
    // placeholder 粗估 Y，跳转目标在视口外时位置严重偏差，多次按才能到位。
    // 完整修复需要"跳转前全量 buildFromAst"（10k 行 ~117ms 卡顿）或 sourceLineToY
    // 走"按行数直接累加"的独立精算路径——成本不值，用户选择移除该功能。
    // 章节跳转仍可通过右侧目录面板点击实现（共用 smoothScrollToSourceLine 路径，
    // 用户已接受其潜在偏差）。

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-AUTO
    // TocPanel 宽度自适应：每次 setEntries 后重算偏好宽度，应用到 mainSplitter
    connect(m_tocPanel, &TocPanel::preferredWidthChanged,
            this, &MainWindow::applyTocPreferredWidth);

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-USER-OVERRIDE
    //       specs/模块-app/README.md          [INV-PANEL-WIDTH-DRAG-CAP]
    // 监听用户拖拽 mainSplitter 分隔条：之后禁用宽度自适应（USER-OVERRIDE），
    // 同时实时夹紧左/右两侧面板到屏幕 1/4 上限（DRAG-CAP）。
    connect(m_mainSplitter, &QSplitter::splitterMoved,
            this, [this](int, int) {
        // 只在 splitter 已完成初始化之后接收用户拖拽事件
        if (!m_splitterInitialized) return;
        m_userDraggedToc = true;
        clampSidePanelsToScreenQuarter();
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
        if (m_tabBarOnSide) syncSideTabBar();
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
        if (idx < 0) {
            // Tab 空白区右键菜单
            QMenu menu(this);
            menu.addAction(tr("New File"), this, &MainWindow::onOpenFile);
            QAction* closeAllAct = menu.addAction(tr("Close All Documents"), [this]() {
                for (int i = m_tabs.size() - 1; i >= 0; --i)
                    onCloseTab(i);
            });
            if (m_tabs.isEmpty()) closeAllAct->setEnabled(false);
            menu.exec(m_tabBar->mapToGlobal(pos));
            return;
        }
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
            ? m_tabs[idx].filePath : QString();
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
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(300);
    connect(&m_reloadTimer, &QTimer::timeout, this, &MainWindow::flushPendingReloads);

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

    QAction* openAct = fileMenu->addAction(tr("Open..."), this, &MainWindow::onOpenFile, QKeySequence::Open);
    Q_UNUSED(openAct);

    fileMenu->addAction(tr("Open Folder..."), this, &MainWindow::onOpenFolder);

    fileMenu->addSeparator();

    m_recentMenu = fileMenu->addMenu(tr("Recent"));
    updateRecentFilesMenu();
    connect(m_recentFiles, &RecentFiles::changed,
            this, &MainWindow::updateRecentFilesMenu);

    fileMenu->addSeparator();

    // 只读查看器：编辑功能的替代入口——用系统默认程序打开 / 在资源管理器中定位
    fileMenu->addAction(tr("Open with Default Application"), this, [this]() {
        if (auto* tab = currentTab(); tab && !tab->filePath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(tab->filePath));
    });
    fileMenu->addAction(tr("Show in File Explorer"), this, [this]() {
        if (auto* tab = currentTab(); tab && !tab->filePath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(tab->filePath).absolutePath()));
    });

    fileMenu->addSeparator();

    fileMenu->addAction(tr("Export HTML..."), this, &MainWindow::onExportHtml);
    fileMenu->addAction(tr("Export PDF..."), this, &MainWindow::onExportPdf);
    fileMenu->addAction(tr("Print..."), this, &MainWindow::onPrint, QKeySequence::Print);

    fileMenu->addSeparator();

    fileMenu->addAction(tr("Exit"), this, &QWidget::close, QKeySequence::Quit);

    // -- 编辑菜单（只读：复制 / 全选 / 查找）--
    QMenu* editMenu = menuBar()->addMenu(tr("Edit"));
    editMenu->addAction(tr("Copy"), [this]() {
        if (auto* tab = currentTab()) tab->viewer->copySelection();
    }, QKeySequence::Copy);
    editMenu->addAction(tr("Select All"), [this]() {
        if (auto* tab = currentTab()) tab->viewer->selectAll();
    }, QKeySequence::SelectAll);
    editMenu->addSeparator();
    editMenu->addAction(tr("Find..."), [this]() {
        if (auto* tab = currentTab()) tab->viewer->showSearchBar();
    }, QKeySequence::Find);

    // -- 视图菜单 --
    QMenu* viewMenu = menuBar()->addMenu(tr("View"));

    // Spec: specs/模块-app/12-主题插件系统.md
    // 主题子菜单：动态列出 Follow System + 所有已发现的主题 + 工具项。
    m_themeMenu = viewMenu->addMenu(tr("Theme"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    rebuildThemeMenu();

    viewMenu->addSeparator();

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
            for (auto& tab : m_tabs) {
                tab.viewer->setLineSpacingFactor(factor);
                tab.viewer->rebuildLayout();
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
            m_tabBarBox->hide();
            m_tabPosHideAllAct->setChecked(true);
        });

        m_tabPosGroup->addAction(m_tabPosShowAllAct);
        m_tabPosGroup->addAction(m_tabPosTopAct);
        m_tabPosGroup->addAction(m_tabPosSideAct);
        m_tabPosGroup->addAction(m_tabPosHideAllAct);
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

    // [Spec specs/模块-app/23-检查更新.md] 仅手动检查更新（用户主动点击触发，不在启动时自动联网）
    helpMenu->addAction(tr("Check for Updates..."), this, [this]() {
        if (m_updateChecker)
            m_updateChecker->checkForUpdates(/*manual=*/true);
    });
    setupUpdateChecker();

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

// Spec: specs/模块-app/README.md  [INV-PANEL-WIDTH-DRAG-CAP]
// 用户拖拽 mainSplitter 时，左侧资源管理器面板和右侧目录面板宽度上限 = 窗口所在屏幕宽度 / 4。
// 拖拽中实时夹紧；持久化恢复路径也复用本函数。
//
// "所在屏幕"用窗口几何中心点决定，比左上角更准确：跨屏拖拽时，center 落在哪个屏幕就以该屏幕为准。
void MainWindow::clampSidePanelsToScreenQuarter()
{
    if (!m_mainSplitter) return;
    auto sizes = m_mainSplitter->sizes();
    if (sizes.size() < 2) return;

    const QScreen* scr = QGuiApplication::screenAt(mapToGlobal(rect().center()));
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    const int maxW = scr->availableGeometry().width() / 4;
    if (maxW <= 0) return;

    bool changed = false;
    QList<int> newSizes = sizes;

    // index 0 = 左侧 leftPaneSplitter（资源管理器），index 末 = TOC 面板
    if (newSizes.size() >= 3 && newSizes[0] > maxW) {
        newSizes[0] = maxW;
        changed = true;
    }
    int tocIdx = newSizes.size() - 1;
    if (newSizes[tocIdx] > maxW) {
        newSizes[tocIdx] = maxW;
        changed = true;
    }
    if (!changed) return;

    // 把节省下来的像素回流给中间内容区，避免总宽度漂移
    const int totalW = m_mainSplitter->width();
    int contentIdx = (newSizes.size() >= 3) ? 1 : 0;
    int sumOthers = 0;
    for (int i = 0; i < newSizes.size(); ++i)
        if (i != contentIdx) sumOthers += newSizes[i];
    newSizes[contentIdx] = qMax(0, totalW - sumOthers);

    m_mainSplitter->blockSignals(true);
    m_mainSplitter->setSizes(newSizes);
    m_mainSplitter->blockSignals(false);
}

// [Plan 2026-05-06-多Tab超阈值时休眠最早文档]
// 把指定 Tab "休眠"——卸载 editor 内容、清 preview AST、设 lazyPending=true。
// 复用现有 lazy 机制：onTabChanged 中切到 lazyPending 的 Tab 时会自动 loadFromFile。
// 跳过条件保证数据安全和体感：
//   - 已是 lazy（不重复）
//   - 未保存修改（避免数据丢失）
//   - 文件路径为空（无源可恢复）
//   - 当前活跃 Tab / 上次活跃 Tab（避免 Ctrl+Tab 反复切撞墙）
// 扫描已加载（非 lazy）Tab 数，超阈值时按 openOrder 升序（最早打开优先）休眠最早的 Tab
// 直到回到阈值之内或没有更多可休眠对象。
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

void MainWindow::restoreSession(const QString& requestedFile)
{
    // 启动加速：先显示窗口，再恢复；只有活跃 tab 立即加载，其余懒加载。
    auto reapplyTheme = [this]() { applyTheme(m_currentTheme); };

    QSettings s;
    const bool restore = s.value("session/restoreLastFile", true).toBool();

    struct TabState { QString file; int line; };
    QVector<TabState> states;
    if (restore) {
        const int count = s.beginReadArray("session/tabs");
        for (int i = 0; i < count; ++i) {
            s.setArrayIndex(i);
            const QString fp = s.value("file").toString();
            if (!fp.isEmpty() && QFileInfo::exists(fp))
                states.append({fp, s.value("viewLine", 0).toInt()});
        }
        s.endArray();
    }

    // 命令行指定了文件：其他会话 tab 懒加载，指定文件立即加载并恢复其阅读位置
    if (!requestedFile.isEmpty()) {
        const QString reqAbs = QFileInfo(requestedFile).absoluteFilePath();
        int reqLine = 0;
        for (const auto& st : states) {
            if (st.file == reqAbs) { reqLine = st.line; continue; }
            TabData tab = createTab();
            tab.filePath = QFileInfo(st.file).absoluteFilePath();
            tab.lazyPending = true;
            tab.pendingLine = st.line;
            const int idx = addPage(tab.viewer, QFileInfo(st.file).fileName());
            m_tabs.append(tab);
            setTabCloseButton(idx);
            watchFile(tab.filePath);
        }
        openFile(requestedFile);
        if (reqLine > 0 && !m_tabs.isEmpty()) m_tabs.last().viewer->scrollToSourceLine(reqLine, false);
        reapplyTheme();
        return;
    }

    if (!states.isEmpty()) {
        int activeTab = 0;
        const QString activeFile = s.value("session/activeFile").toString();
        for (int i = 0; i < states.size(); ++i)
            if (!activeFile.isEmpty() && states[i].file == activeFile) { activeTab = i; break; }
        for (int i = 0; i < states.size(); ++i) {
            const auto& st = states[i];
            if (i == activeTab) {
                openFile(st.file);
                if (st.line > 0 && !m_tabs.isEmpty()) m_tabs.last().viewer->scrollToSourceLine(st.line, false);
            } else {
                TabData tab = createTab();
                tab.filePath = QFileInfo(st.file).absoluteFilePath();
                tab.lazyPending = true;
                tab.pendingLine = st.line;
                const int idx = addPage(tab.viewer, QFileInfo(st.file).fileName());
                m_tabs.append(tab);
                setTabCloseButton(idx);
                watchFile(tab.filePath);
            }
        }
        if (activeTab >= 0 && activeTab < m_tabBar->count()) {
            m_tabBar->setCurrentIndex(activeTab);
            onTabChanged(activeTab);   // 与首个 tab 同理：确保目录/文件键与当前 tab 同步
        }
        reapplyTheme();
        return;
    }

    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-NO-AUTO-CREATE
    updateEmptyState();
    reapplyTheme();
}

void MainWindow::openFile(const QString& path)
{
    // 检查文件是否已在某个标签页打开（包括懒加载未加载的 tab）
    const QString absPath = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].filePath == absPath) {
            m_tabBar->setCurrentIndex(i);
            // 已打开：切换后提升窗口（浏览器等外部程序打开文件时窗口可能被遮挡）
            setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            raise();
            activateWindow();
            return;
        }
    }

    // 创建新标签页并异步加载文件（INV-LOAD-NONBLOCKING：UI 线程不做文件 IO）
    TabData tab = createTab();
    tab.filePath = absPath;
    const int index = addPage(tab.viewer, QFileInfo(path).fileName());
    m_tabs.append(tab);
    setTabCloseButton(index);
    m_tabBar->setCurrentIndex(index);
    // 第一个 tab：addTab 时 currentChanged 已在 m_tabs 为空时触发过，之后 setCurrentIndex 不再触发，
    // 需显式同步目录面板的文件键/折叠状态等
    if (m_tabs.size() == 1) onTabChanged(index);
    updateEmptyState();

    tab.viewer->setDocumentDir(QFileInfo(absPath).absolutePath());
    tab.viewer->loadFile(absPath);

    m_recentFiles->addFile(path);
    updateTabTitle(index);
    watchFile(absPath);
    m_folderPanel->selectFile(path);

    // 加载新文件后必须提升窗口，确保用户能看到
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();

    saveSessionLater();
}

MainWindow::TabData MainWindow::createTab()
{
    TabData tab;
    tab.openOrder = m_nextTabOpenOrder++;

    // Spec: specs/模块-viewer/04-查看器控件.md —— Tab 内容 = 单个只读 ViewerWidget
    tab.viewer = new viewer::ViewerWidget();
    tab.viewer->setTheme(m_currentTheme);
    tab.viewer->setLineSpacingFactor(m_lineSpacingFactor);
    tab.viewer->previewLayout()->setFont(font_defaults::defaultPreviewFont(m_fontSizeDelta));

    // 目录：仅当此 tab 是当前活跃 tab 时更新全局 TocPanel
    connect(tab.viewer, &viewer::ViewerWidget::tocEntriesChanged,
            this, [this, v = tab.viewer](const QVector<TocEntry>& entries) {
        if (currentTab() && currentTab()->viewer == v)
            m_tocPanel->setEntries(entries);
    });

    // 链接（Ctrl+点击）
    connect(tab.viewer, &viewer::ViewerWidget::linkClicked,
            this, [this, v = tab.viewer](const QString& url) {
        onPreviewLinkClicked(url, v->filePath());
    });

    // 加载状态变化 → 刷新状态栏/文档信息
    connect(tab.viewer, &viewer::ViewerWidget::loadStateChanged, this, [this, v = tab.viewer]() {
        if (currentTab() && currentTab()->viewer == v) {
            updateStatusBarStats();
            updateRightStatusBar();
        }
    });

    // 阅读位置变化 → 状态栏 + 会话持久化（ViewerWidget 内部已去抖）
    connect(tab.viewer, &viewer::ViewerWidget::positionChanged, this, [this, v = tab.viewer]() {
        if (currentTab() && currentTab()->viewer == v) updatePositionStatus();
        saveSessionLater();
    });

    return tab;
}

void MainWindow::updateTabTitle(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;

    const QString fp = m_tabs[index].filePath;
    const QString title = fp.isEmpty() ? tr("Untitled") : QFileInfo(fp).fileName();

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
        tab.viewer->setTheme(theme);
    }
    m_tocPanel->setTheme(theme);
    m_folderPanel->setTheme(theme);
    m_sideTabBar->setTheme(theme);
    if (m_welcomePanel) m_welcomePanel->setTheme(theme);  // INV-EMPTY-THEME
    // "+" 按钮前景色和 hover 背景跟随主题
    m_tabBarBox->setAddButtonColors(
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
            // [Spec 模块-app/10-菜单栏样式 INV-MENU-ALIGN] 勾选标记列宽固定 16px、无 margin，
            // 使 checkable 与非 checkable 项文字左缘对齐（margin-right 会额外推右 checkable 文字 →
            // 与非 checkable 项错位）；同时勾选标记仍绘制在保留列内可见
            "QMenu::indicator { width: 16px; height: 16px; }"
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
            // [Spec 模块-app/10-菜单栏样式 INV-MENU-ALIGN] 勾选标记列宽固定 16px、无 margin，
            // 使 checkable 与非 checkable 项文字左缘对齐（margin-right 会额外推右 checkable 文字 →
            // 与非 checkable 项错位）；同时勾选标记仍绘制在保留列内可见
            "QMenu::indicator { width: 16px; height: 16px; }"
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

// -- 槽函数 --

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

void MainWindow::onCloseTab(int index)
{
    if (index < 0 || index >= m_tabs.size()) return;

    // 取消文件监控
    const QString fp = m_tabs[index].filePath;
    if (!fp.isEmpty())
        unwatchFile(fp);

    // 移除标签页数据（ViewerWidget 析构会取消后台加载，不阻塞）
    auto* v = m_tabs[index].viewer;
    m_tabs.removeAt(index);
    removePage(index);  // 从 tabBar + contentStack 移除
    if (v) v->deleteLater();

    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-CLOSE-LAST
    updateEmptyState();

    saveSessionLater();
}

void MainWindow::onTabChanged(int index)
{
    // 同步侧边 Tab 栏选中状态
    if (m_tabBarOnSide) m_sideTabBar->setCurrentIndex(index);

    updateTabTitle(index);

    if (index >= 0 && index < m_tabs.size()) {
        TabData& tab = m_tabs[index];
        // 懒加载：切换到尚未加载的 tab 时才触发文件加载
        if (tab.lazyPending) {
            tab.lazyPending = false;
            if (!tab.filePath.isEmpty() && QFileInfo::exists(tab.filePath)) {
                tab.viewer->setDocumentDir(QFileInfo(tab.filePath).absolutePath());
                tab.viewer->loadFile(tab.filePath);
                if (tab.pendingLine > 0) tab.viewer->scrollToSourceLine(tab.pendingLine, false);
                tab.pendingLine = -1;
            }
        }
        auto* v = tab.viewer;
        const QString fp = tab.filePath;
        v->setDocumentDir(fp.isEmpty() ? QString() : QFileInfo(fp).absolutePath());
        // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-COLLAPSE：先 fileKey 再 entries
        m_tocPanel->setCurrentFileKey(fp);
        m_tocPanel->setEntries(v->tocEntries());
        m_folderPanel->selectFile(fp);

        // 切换到有待重载标记的 tab：静默重载并保持阅读位置
        if (tab.pendingReload) {
            tab.pendingReload = false;
            v->reload();
        }
        v->setFocus();
    } else {
        m_tocPanel->setEntries({});
    }

    updateStatusBarStats();
    updatePositionStatus();
    updateRightStatusBar();
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
            rejected.append(QFileInfo(path).fileName());   // 只读查看器：不再支持把图片插入文档
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
        // 只处理来自查看器（或其视口）的 Ctrl+滚轮；对话框/文件夹面板等其他控件不受影响
        const QWidget* ow = qobject_cast<QWidget*>(obj);
        const bool fromViewer = ow && (qobject_cast<const viewer::ViewerWidget*>(ow)
                                       || qobject_cast<const viewer::ViewerWidget*>(ow->parentWidget()));
        if (fromViewer && (we->modifiers() & Qt::ControlModifier)) {
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

            // [INV-PANEL-WIDTH-DRAG-CAP] 恢复的旧宽度若超过当前屏幕 1/4，立即夹紧
            clampSidePanelsToScreenQuarter();
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

    // 只读查看器：无未保存内容，直接保存会话并退出
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

        // 异步读取：对端写完即断开。不在 UI 线程 waitForReadyRead（对端卡住会冻结界面）
        auto finish = [this, socket]() {
            const QByteArray data = socket->readAll();
            socket->deleteLater();
            const QString filePath = QString::fromUtf8(data);
            // 与拖拽打开同一套校验：只接受存在的文本文件（目录/二进制一律忽略，仅激活窗口）
            if (!filePath.isEmpty() && QFileInfo(filePath).isFile() && isOpenableTextFile(filePath))
                openFile(filePath);

            setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            raise();
            activateWindow();
        };
        if (socket->state() != QLocalSocket::ConnectedState) {
            finish();   // 对端已断开，数据已在缓冲里
        } else {
            connect(socket, &QLocalSocket::disconnected, this, finish);
            // 兜底：1 秒内对端既没写也没断开，就丢弃该连接
            QTimer::singleShot(1500, socket, [socket]() { if (socket->state() != QLocalSocket::UnconnectedState) socket->abort(); });
        }
    });
}

void MainWindow::saveSessionLater()
{
    m_saveSessionTimer.start();  // (重新)启动 1 秒倒计时
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("view/lineSpacing", m_lineSpacingFactor);
    s.setValue("view/fontSizeDelta", m_fontSizeDelta);
    // Spec: specs/模块-app/14-自动保存.md
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
    // 左侧面板竖直分隔条高度（资源管理器 vs 侧边 Tab 栏）
    if (m_tabBarOnSide && m_leftPaneSplitter->isVisible()) {
        auto leftSizes = m_leftPaneSplitter->sizes();
        if (leftSizes.size() >= 2 && leftSizes[0] > 0 && leftSizes[1] > 0) {
            s.setValue("view/leftPaneFolderHeight", leftSizes[0]);
            s.setValue("view/leftPaneSideTabHeight", leftSizes[1]);
        }
    }
    // 文件夹面板：持久化所有打开的目录
    s.setValue("folderPanel/rootPaths", m_folderPanel->rootPaths());

    // 会话恢复
    s.setValue("session/restoreLastFile", m_restoreSessionAct ? m_restoreSessionAct->isChecked() : true);
    s.setValue("session/activeTab", m_tabBar->currentIndex());
    // 同时记录活跃文件路径：恢复时已不存在的文件会被过滤，按下标恢复会错位
    if (m_tabBar->currentIndex() >= 0 && m_tabBar->currentIndex() < m_tabs.size())
        s.setValue("session/activeFile", m_tabs[m_tabBar->currentIndex()].filePath);

    // 保存所有标签页（只读查看器：路径 + 阅读位置行号）
    s.beginWriteArray("session/tabs");
    int written = 0;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QString fp = m_tabs[i].filePath;
        if (fp.isEmpty()) continue;
        s.setArrayIndex(written++);
        s.setValue("file", fp);
        // 懒加载未激活的 tab 沿用上次保存的行号
        s.setValue("viewLine", m_tabs[i].lazyPending ? m_tabs[i].pendingLine
                                                      : m_tabs[i].viewer->readingLine());
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
    // 默认不开启自动保存；延时默认 5 分钟（300000ms）
    // 语言设置
    QString locale = s.value("language/locale", "zh_CN").toString();
    if (locale == "en_US" && m_enUSAct) {
        m_enUSAct->setChecked(true);
    } else if (m_zhCNAct) {
        m_zhCNAct->setChecked(true);
    }

    // 应用到已有标签页
    for (auto& tab : m_tabs) {
        tab.viewer->setLineSpacingFactor(m_lineSpacingFactor);
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
        if (m_tabs[i].filePath == path) { tabIndex = i; break; }
    }
    if (tabIndex < 0) return;

    // 非当前 tab：标记待重载，切换时再静默刷新（懒加载 tab 激活时本来就会读最新内容）
    if (tabIndex != m_tabBar->currentIndex()) {
        m_tabs[tabIndex].pendingReload = true;
        return;
    }
    // 当前 tab：合并 + 防抖（编辑器常见的「删除再重建」原子保存会连发多次事件；
    // 事件发生的瞬间文件可能暂时不存在，所以存在性检查推迟到防抖结束）
    m_reloadPaths.insert(path);
    m_reloadTimer.start();
}

void MainWindow::flushPendingReloads()
{
    const QSet<QString> paths = m_reloadPaths;
    m_reloadPaths.clear();
    for (const QString& path : paths) {
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i].filePath != path || !m_tabs[i].viewer || m_tabs[i].lazyPending) continue;
            if (QFileInfo::exists(path)) {
                watchFile(path);                 // 原子保存会让监控失效，重新挂上
                m_tabs[i].viewer->reload();
            } else if (i == m_tabBar->currentIndex()) {
                showToast(tr("%1 has been deleted by another program.").arg(QFileInfo(path).fileName()));
            } else {
                m_tabs[i].pendingDelete = true;
            }
            break;
        }
    }
}

void MainWindow::promptReloadTab(int tabIndex)
{
    // 只读查看器：外部修改一律静默重载，不再弹窗询问
    if (tabIndex < 0 || tabIndex >= m_tabs.size()) return;
    m_tabs[tabIndex].pendingReload = false;
    if (m_tabs[tabIndex].viewer) m_tabs[tabIndex].viewer->reload();
}

// ---- 状态栏统计信息 ----

void MainWindow::setupStatusBar()
{
    auto* sb = statusBar();
    sb->setSizeGripEnabled(true);
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
    // 只读查看器状态栏：阅读位置 / 行数 / 大小 / 索引进度；右侧编码
    m_statusCursorPos = makeLeftLabel(QString());
    m_statusLineCount = makeLeftLabel(QString());
    m_statusLineCount->hide();   // 总行数已并入阅读位置 "Ln x / y"，不再单独显示
    m_statusWordCount = makeLeftLabel(QString());   // 复用：文件大小
    m_statusCharCount = makeLeftLabel(QString());   // 复用：索引进度 / 错误信息
    m_statusEncoding = makeRightLabel(QStringLiteral("UTF-8"));
}

void MainWindow::updateStatusBarStats()
{
    auto* tab = currentTab();
    if (!tab || !tab->viewer) {
        m_statusWordCount->setText(QString());
        m_statusCharCount->setText(QString());
        return;
    }
    auto* v = tab->viewer;

    const qint64 bytes = static_cast<qint64>(v->sourceBytes());
    QString sizeText;
    if (bytes < 1024) sizeText = QStringLiteral("%1 B").arg(bytes);
    else if (bytes < 1024 * 1024) sizeText = QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    else sizeText = QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    m_statusWordCount->setText(tr("Size: %1").arg(sizeText));

    if (!v->errorString().isEmpty())
        m_statusCharCount->setText(v->errorString());
    else
        m_statusCharCount->setText(v->isLoading() ? tr("Indexing…") : QString());

    // 文档信息卡：大小/行数/修改时间（字数在大文件上代价高，只对 ≤2MB 精确统计）
    DocInfo di;
    di.lineCount = static_cast<int>(v->lineCount());
    di.sizeBytes = bytes;
    const QString fp = tab->filePath;
    if (!fp.isEmpty()) {
        QFileInfo fi(fp);
        if (fi.exists()) di.mtime = fi.lastModified();
    }
    if (!v->isLoading() && bytes > 0 && bytes <= 2 * 1024 * 1024) {
        const QString text = v->readAllText();
        int words = 0, noSpace = 0;
        bool inWord = false;
        for (const QChar& ch : text) {
            if (!ch.isSpace()) ++noSpace;
            const ushort uc = ch.unicode();
            const bool cjk = (uc >= 0x4E00 && uc <= 0x9FFF) || (uc >= 0x3400 && uc <= 0x4DBF)
                          || (uc >= 0xF900 && uc <= 0xFAFF);
            if (cjk) { if (inWord) { ++words; inWord = false; } ++words; }
            else if (ch.isLetterOrNumber()) inWord = true;
            else if (inWord) { ++words; inWord = false; }
        }
        if (inWord) ++words;
        di.wordCount = words;
        di.charCount = text.length();
        di.charCountNoSpace = noSpace;
    } else {
        di.wordCount = -1;
        di.charCount = -1;
        di.charCountNoSpace = -1;
    }
    m_tocPanel->setDocumentInfo(di);
}

void MainWindow::updatePositionStatus()
{
    if (!m_statusCursorPos) return;
    auto* tab = currentTab();
    if (!tab || !tab->viewer || tab->viewer->lineCount() == 0) {
        m_statusCursorPos->setText(QString());
        return;
    }
    m_statusCursorPos->setText(tr("Ln %1 / %2").arg(tab->viewer->topSourceLine() + 1)
                                   .arg(tab->viewer->lineCount()));
}

// ---- 自动保存 ----
// Spec: specs/模块-app/14-自动保存.md
// INV-1: 仅保存有磁盘路径的 Tab；未命名（filePath 空）跳过、不弹文件对话框
// INV-2: 失败不打断输入，状态栏右下显示瞬时提示
// INV-3: textChanged 重置 timer，连续输入只在最后一次停顿后保存

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
    // 只读查看器：编码固定 UTF-8（非 UTF-8 文件加载时已转码）
    if (m_statusEncoding) m_statusEncoding->setText(QStringLiteral("UTF-8"));
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
    const QFont previewFont = font_defaults::defaultPreviewFont(m_fontSizeDelta);
    for (auto& tab : m_tabs) {
        tab.viewer->previewLayout()->setFont(previewFont);
        tab.viewer->rebuildLayout();
    }
}

// ---- 导出与打印 ----

QString MainWindow::currentMarkdownToHtml()
{
    auto* tab = currentTab();
    if (!tab || !tab->viewer) return QString();
    // 导出/打印需要整篇 HTML：此时才一次性解码并渲染。超过 8MB 的文件会长时间占用主线程且内存翻倍，
    // 直接拒绝并提示（查看大文件不受影响）。
    constexpr uint64_t kMaxExportBytes = 8ull * 1024 * 1024;
    if (tab->viewer->sourceBytes() > kMaxExportBytes) {
        showToast(tr("File is too large to export or print (limit 8 MB)."));
        return QString();
    }
    MarkdownParser parser;
    return parser.renderHtml(tab->viewer->readAllText());
}

void MainWindow::onExportHtml()
{
    auto* tab = currentTab();
    if (!tab) return;

    QString body = currentMarkdownToHtml();
    if (body.isEmpty()) return;

    QString baseName = QFileInfo(tab->filePath).completeBaseName();
    if (baseName.isEmpty()) baseName = "untitled";

    QString html = QString(kHtmlTemplate).arg(baseName.toHtmlEscaped(), body);

    QString defaultPath = QFileInfo(tab->filePath).absolutePath();
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

    QString baseName = QFileInfo(tab->filePath).completeBaseName();
    if (baseName.isEmpty()) baseName = "untitled";

    QString html = QString(kHtmlTemplate).arg(baseName.toHtmlEscaped(), body);

    QString defaultPath = QFileInfo(tab->filePath).absolutePath();
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

    QString baseName = QFileInfo(tab->filePath).completeBaseName();
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

// [Spec specs/模块-app/23-检查更新.md] 初始化检查更新器（仅手动触发，不在启动时自动联网）
void MainWindow::setupUpdateChecker()
{
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable,
            this, &MainWindow::onUpdateAvailable);
    connect(m_updateChecker, &UpdateChecker::upToDate, this, [this]() {
        showUpdateDialog(tr("Check for Updates"),
            tr("You are already on the latest version (%1).")
                .arg(QApplication::applicationVersion().toHtmlEscaped()));
    });
    connect(m_updateChecker, &UpdateChecker::checkFailed, this, [this](const QString& msg) {
        // 失败时给出可点击的 releases 页链接，方便用户手动查看
        const QString releasesUrl =
            QStringLiteral("https://github.com/ssk-wh/simple_markdown/releases");
        QString body = tr("Failed to check for updates: %1").arg(msg.toHtmlEscaped());
        body += QStringLiteral("<br><br>")
              + tr("You can visit the %1 manually.")
                    .arg(QStringLiteral("<a href=\"%1\">%2</a>")
                             .arg(releasesUrl, tr("releases page")));
        showUpdateDialog(tr("Check for Updates"), body);
    });
}

// [Spec specs/模块-app/23-检查更新.md] 检查更新统一信息弹窗——自定义 QDialog，
// 关闭按钮风格与「更新历史」对话框一致（不设样式，靠 applyTheme 的全局 QDialog QPushButton
// cascade）；正文为富文本 QLabel，内含 <a href> 链接，setOpenExternalLinks 使点击用浏览器打开
void MainWindow::showUpdateDialog(const QString& title, const QString& richBody)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(440);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    QLabel* label = new QLabel(richBody, &dlg);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setOpenExternalLinks(true);  // 链接点击用系统浏览器打开
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(label);

    // 关闭按钮：与 ChangelogDialog/onShowWelcome 完全一致的最简实现，颜色尺寸靠全局 cascade
    QPushButton* closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn);

    dlg.exec();
}

// [Spec specs/模块-app/23-检查更新.md INV-UPD-NO-AUTODOWNLOAD] 有新版本时提示，
// 用统一自定义弹窗显示版本号 + 更新说明 + 可点击的下载页链接（不自动下载/静默安装）
void MainWindow::onUpdateAvailable(const QString& version, const QString& notes, const QString& url)
{
    QString body = tr("A new version <b>%1</b> is available (current: %2).")
                       .arg(version.toHtmlEscaped(),
                            QApplication::applicationVersion().toHtmlEscaped());
    if (!url.isEmpty()) {
        body += QStringLiteral("<br><br>")
              + tr("Download page: %1")
                    .arg(QStringLiteral("<a href=\"%1\">%1</a>").arg(url.toHtmlEscaped()));
    }
    if (!notes.trimmed().isEmpty()) {
        // 更新说明转义后限长，换行转 <br>
        QString brief = notes.trimmed().toHtmlEscaped();
        if (brief.size() > 1000)
            brief = brief.left(1000) + QStringLiteral("…");
        brief.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        body += QStringLiteral("<br><br>") + brief;
    }
    showUpdateDialog(tr("Update Available"), body);
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
        m_tabBarBox->setVisible(true);
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
        // 恢复或设置左侧面板竖直分隔条高度
        QSettings s;
        int folderH = s.value("view/leftPaneFolderHeight", -1).toInt();
        int sideTabH = s.value("view/leftPaneSideTabHeight", -1).toInt();
        if (folderH > 0 && sideTabH > 0) {
            // 恢复之前保存的高度
            m_leftPaneSplitter->setSizes({folderH, sideTabH});
        } else {
            // 首次或未保存时，使用默认比例：文件夹 2/3，Tab 栏 1/3
            int totalH = m_leftPaneSplitter->height();
            if (totalH < 100) totalH = 600;
            m_leftPaneSplitter->setSizes({totalH * 2 / 3, totalH / 3});
        }
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
    m_tabBarBox->setVisible(!hideTopBar);

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
    if (empty) {
        // 关闭最后一个 tab：清理残留的标题/目录/文档信息/文件夹选中项
        setWindowTitle("SimpleMarkdown");
        if (m_tocPanel) {
            m_tocPanel->setCurrentFileKey(QString());
            m_tocPanel->setEntries({});
            m_tocPanel->setDocumentInfo(DocInfo());
        }
        if (m_folderPanel) m_folderPanel->selectFile(QString());
        if (m_statusWordCount) m_statusWordCount->setText(QString());
        if (m_statusCharCount) m_statusCharCount->setText(QString());
        if (m_statusCursorPos) m_statusCursorPos->setText(QString());
    }
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
            // [Spec 模块-app/20 INV-LP-WIDTH-TOGGLE] 恢复宽度优先级：
            // 本会话隐藏前缓存的宽度 > 持久化的 leftPanelWidth > 屏幕宽 1/8 默认
            QSettings s;
            int savedLeftW = s.value("view/leftPanelWidth", -1).toInt();
            QScreen* scr = nullptr;
            if (windowHandle()) scr = windowHandle()->screen();
            if (!scr) scr = QGuiApplication::primaryScreen();
            int screenW = (scr && scr->geometry().width() > 0) ? scr->geometry().width() : 1920;
            int leftW = (m_savedSidebarWidth > 0) ? m_savedSidebarWidth
                      : (savedLeftW > 0) ? savedLeftW
                      : (screenW / 8);

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
    // [Spec 模块-app/20-左侧面板.md INV-LP-WIDTH-TOGGLE] 隐藏前先缓存当前左面板宽度，
    // 否则 hide() 让 splitter 回收 sizes[0]→0，再显示时只能回退默认宽度 → 与隐藏前不一致。
    if (!m_sidebarHidden && m_mainSplitter) {
        auto sizes = m_mainSplitter->sizes();
        if (!sizes.isEmpty() && sizes[0] > 0)
            m_savedSidebarWidth = sizes[0];
    }
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
    m_focusMode = true;
    if (m_focusModeAct) m_focusModeAct->setChecked(true);
    m_savedMainSplitterState = m_mainSplitter->saveState();
    m_savedTocVisible = m_tocPanel->isVisible();
    m_tocPanel->hide();
    m_leftPaneSplitter->hide();
    m_tabBarBox->hide();
    menuBar()->hide();
    statusBar()->hide();
    showFullScreen();
    if (auto* tab = currentTab()) tab->viewer->setFocus();
}

void MainWindow::exitFocusMode()
{
    m_focusMode = false;
    if (m_focusModeAct) m_focusModeAct->setChecked(false);
    showNormal();
    menuBar()->show();
    statusBar()->show();
    m_tabBarBox->show();
    m_tocPanel->setVisible(m_savedTocVisible);
    updateLeftPaneVisibility();
    m_mainSplitter->restoreState(m_savedMainSplitterState);
    if (auto* tab = currentTab()) tab->viewer->setFocus();
}

// ---- 预览区链接点击处理 [Spec 模块-preview/09] ----

static bool isExecutableExt(const QString& path)
{
    static const QStringList exts = {
        "exe", "bat", "cmd", "com", "ps1", "sh", "msi", "app", "scr"
    };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

void MainWindow::onPreviewLinkClicked(const QString& url, const QString& originFile)
{
    if (url.isEmpty()) return;

    // 外部链接：交给系统浏览器
    const QUrl qurl(url);
    if (qurl.scheme() == QLatin1String("http") || qurl.scheme() == QLatin1String("https")
        || qurl.scheme() == QLatin1String("mailto")) {
        QDesktopServices::openUrl(qurl);
        return;
    }

    // 本地路径（相对路径基于当前文档目录）
    QString localPath = url;
    const int hash = localPath.indexOf('#');
    if (hash >= 0) localPath = localPath.left(hash);
    if (localPath.isEmpty()) return;
    QFileInfo fi(localPath);
    if (fi.isRelative() && !originFile.isEmpty())
        fi = QFileInfo(QFileInfo(originFile).absolutePath() + "/" + localPath);
    if (!fi.exists()) {
        showToast(tr("File not found: %1").arg(fi.absoluteFilePath()));
        return;
    }
    const QString suffix = fi.suffix().toLower();
    if (suffix == "md" || suffix == "markdown" || suffix == "txt") openFile(fi.absoluteFilePath());
    else QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
}

// ---- 文档统计弹窗 [Plan plans/2026-04-13-文档统计信息.md] ----

void MainWindow::onShowDocumentStats()
{
    auto* tab = currentTab();
    if (!tab || !tab->viewer) return;
    auto* v = tab->viewer;
    const QString title = tr("Document Statistics");
    QString body = tr("Lines: %1").arg(v->lineCount()) + "<br>"
                 + tr("Size: %1 bytes").arg(v->sourceBytes()) + "<br>"
                 + tr("Headings: %1").arg(v->tocEntries().size());
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
    .arg(tr("Read-only viewer: opens huge files instantly and scrolls smoothly"))
    .arg(tr("Multi-tab workspace with drag-and-drop file support"))
    .arg(tr("Full-text search with highlight (Ctrl+F), Ctrl+click to follow links"))
    .arg(tr("Outline/TOC panel for quick heading navigation"))
    .arg(tr("Auto-refresh when the file changes on disk, keeping your reading position"))
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

#pragma once
#include <QMainWindow>
#include <QVector>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QSet>
#include "Theme.h"

namespace viewer { class ViewerWidget; }
class RecentFiles;
class TocPanel;
class FolderPanel;
class TabBarWithAdd;  // Spec: specs/模块-app/04-窗口焦点管理.md INV-6（Tab 栏 + 「+」按钮容器）
class QTabBar;
class QSplitter;
class QStackedWidget;
class QMenu;
class QActionGroup;
class QLocalServer;
class QLabel;
class QStatusBar;
class QVBoxLayout;
class QTranslator;
class SideTabBar;
class QMessageBox;
class WelcomePanel;  // Spec: specs/模块-app/22-空白引导页.md
class UpdateChecker;  // Spec: specs/模块-app/23-检查更新.md

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFile(const QString& path);
    void newTab();
    void restoreSession(const QString& requestedFile = QString());
    void startLocalServer(const char* serverName);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenFile();
    void onOpenFolder();
    void onCloseTab(int index);
    void onTabChanged(int index);

private:
    struct TabData {
        viewer::ViewerWidget* viewer = nullptr;
        QString filePath;            // 绝对路径（懒加载 tab 也记录，加载前即可用于去重/会话）
        bool pendingReload = false;  // 文件被外部修改，等切换到此 tab 时静默重载
        bool pendingDelete = false;  // 文件被外部删除
        bool lazyPending = false;    // 懒加载：尚未加载文件内容
        int pendingLine = -1;        // 懒加载 tab 的待恢复阅读位置（0 基行号）
        qint64 openOrder = 0;
        QPointer<QMessageBox> activeReloadDialog;  // 保留字段（兼容），只读查看器不再弹窗
    };

    // [Spec 模块-preview/09] 预览区链接 Ctrl+click 处理
    void onPreviewLinkClicked(const QString& url, const QString& originFile);

    // [Plan 文档统计信息] 弹窗统计当前文档的结构化信息
    void onShowDocumentStats();

    // [Plan 首次启动引导] 显示欢迎对话框（首次启动 + 菜单手动触发）
    void onShowWelcome();
    void maybeShowWelcomeOnFirstLaunch();

    // Spec: specs/模块-app/16-崩溃报告收集.md
    // 启动后检测 %APPDATA%/SimpleMarkdown/crashes/ 下是否有"未查看"的 dump，弹窗提示
    void maybeShowCrashReportPrompt();

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-VALIGN
    // central widget = QVBoxLayout(m_tabBarBox + m_mainSplitter)
    // m_mainSplitter 左：m_contentStack（每页一个 editor|preview splitter）
    //                 右：m_tocPanel
    // Spec: specs/模块-app/04-窗口焦点管理.md INV-6
    // m_tabBarBox = Tab 栏 + 「+」按钮容器；m_tabBar 指向其内部 QTabBar（宿主用它做
    // 所有 tab 操作）。可见性切换一律作用于容器 m_tabBarBox，避免按钮孤立残留。
    TabBarWithAdd* m_tabBarBox = nullptr;
    QTabBar* m_tabBar = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    // Spec: specs/模块-app/22-空白引导页.md
    // m_tabs 为空时显示 m_welcomePanel，m_contentStack 隐藏（INV-EMPTY-WELCOME-MUTUAL）
    WelcomePanel* m_welcomePanel = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_leftPaneSplitter = nullptr;     // 左侧面板容器（文件夹 + 可选侧边 Tab 栏）
    QWidget* m_folderContainer = nullptr;      // folderPanel 的容器（始终在 splitter 中可见）
    QVBoxLayout* m_centralLayout = nullptr;     // 顶层 vbox，切换 Tab 位置时需引用
    TocPanel* m_tocPanel = nullptr;
    FolderPanel* m_folderPanel = nullptr;
    // Tab 页与 stack 页一一对应的便利函数
    int  addPage(QWidget* page, const QString& title);
    void removePage(int index);
    QVector<TabData> m_tabs;
    RecentFiles* m_recentFiles;
    QMenu* m_recentMenu = nullptr;

    Theme m_currentTheme;
    QLocalServer* m_localServer = nullptr;

    // 检查更新（Spec: specs/模块-app/23-检查更新.md）——仅手动触发
    UpdateChecker* m_updateChecker = nullptr;
    void setupUpdateChecker();                 // 构造期创建 UpdateChecker + 连接信号
    void onUpdateAvailable(const QString& version, const QString& notes, const QString& url);
    // 检查更新统一信息弹窗：自定义 QDialog（关闭按钮风格与「更新历史」一致，靠全局 QSS cascade），
    // richBody 为富文本，内含 <a href> 链接可点击跳转浏览器
    void showUpdateDialog(const QString& title, const QString& richBody);

    // 持久化的视图设置
    QAction* m_restoreSessionAct = nullptr;
    QAction* m_followSystemThemeAct = nullptr;
    QAction* m_lightThemeAct = nullptr;
    QAction* m_darkThemeAct = nullptr;
    // Spec: specs/模块-app/12-主题插件系统.md
    // 主题子菜单动态列出所有已发现主题（内置 + 用户目录）
    QMenu* m_themeMenu = nullptr;
    QActionGroup* m_themeGroup = nullptr;
    QList<QAction*> m_dynamicThemeActs;  // 除 follow system/light/dark 外的动态主题项
    QString m_currentThemeId;             // 当前选中的主题 id；空表示 follow system
    QVector<QAction*> m_spacingActions;
    qreal m_lineSpacingFactor = 1.5;

    // 语言切换
    QTranslator* m_translator = nullptr;
    QTranslator* m_qtTranslator = nullptr;  // Qt 内置 widget 翻译（标准按钮等），见 i18n INV-3
    QAction* m_zhCNAct = nullptr;
    QAction* m_enUSAct = nullptr;
    void switchLanguage(const QString& locale);
    void retranslateUi();

    void saveSettings();
    void saveSessionLater();
    void loadSettings();
#ifdef _WIN32
    void setDarkTitleBar(bool dark);
#endif
    QTimer m_saveSessionTimer;
    QByteArray m_pendingSplitterState;
    bool m_splitterInitialized = false;

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-WIDTH-AUTO / INV-TOC-WIDTH-USER-OVERRIDE
    // 用户拖拽 mainSplitter 分隔条后该 flag 为 true；此后 preferredWidthChanged 只调 minWidth
    bool m_userDraggedToc = false;
    void applyTocPreferredWidth(int w);
    void clampTocWidthToScreen();
    // [Plan 2026-05-06-面板宽度上限按所在屏幕1-4]
    // [INV-PANEL-WIDTH-DRAG-CAP] 用户拖拽 m_mainSplitter 时，
    // 左侧资源管理器面板和右侧目录面板的宽度上限均为窗口所在屏幕宽度的 1/4。
    // 拖拽过程中实时夹紧；持久化恢复时同样夹紧。
    void clampSidePanelsToScreenQuarter();

    QFileSystemWatcher m_fileWatcher;
    void watchFile(const QString& path);
    void unwatchFile(const QString& path);
    void onFileChangedExternally(const QString& path);
    void promptReloadTab(int tabIndex);
    void flushPendingReloads();
    QTimer m_reloadTimer;           // 外部修改事件合并（300ms）
    QSet<QString> m_reloadPaths;

    void setupMenuBar();
    void setupDragDrop();

    // 导出与打印
    QString currentMarkdownToHtml();
    void onExportHtml();
    void onExportPdf();
    void onPrint();
    void onShowShortcuts();

    // Tab 栏位置（顶部 / 左侧）
    bool m_tabBarOnSide = false;
    bool m_hideTopBarWhenSide = true;  // 侧边模式时是否隐藏顶部 tab 栏
    QActionGroup* m_tabPosGroup = nullptr;
    QAction* m_tabPosShowAllAct = nullptr;
    QAction* m_tabPosTopAct = nullptr;
    QAction* m_tabPosSideAct = nullptr;
    QAction* m_tabPosHideAllAct = nullptr;
    SideTabBar* m_sideTabBar = nullptr;
    void setTabBarPosition(bool onSide, bool hideTopBar = true);
    void updateLeftPaneVisibility();
    void syncSideTabBar();           // 从 m_tabBar 全量同步到 m_sideTabBar
    // Spec: specs/模块-app/22-空白引导页.md INV-EMPTY-WELCOME-MUTUAL
    // 根据 m_tabs.isEmpty() 切换 WelcomePanel / m_contentStack 的可见性
    void updateEmptyState();

    // Ctrl+B 切换左侧资源管理器显隐
    QAction* m_toggleSidebarAct = nullptr;
    bool m_sidebarHidden = false;    // 用户手动隐藏侧边栏
    // [Spec 模块-app/20-左侧面板.md INV-LP-WIDTH-TOGGLE] Ctrl+B 隐藏前缓存的左面板像素宽度，
    // 再次显示时优先恢复到该宽度（而非默认 screenW/8），保证显隐切换不改变宽度
    int m_savedSidebarWidth = -1;
    void toggleSidebar();

    // 底部居中 toast 通知
    void showToast(const QString& message, int durationMs = 3000);


    // 专注模式
    bool m_focusMode = false;
    QAction* m_focusModeAct = nullptr;
    QByteArray m_savedMainSplitterState;
    bool m_savedTocVisible = true;
    void toggleFocusMode();
    void enterFocusMode();
    void exitFocusMode();

    // 字体缩放
    int m_fontSizeDelta = 0;  // 相对于默认字号的偏移
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void applyFontSize();
    TabData createTab();

    qint64 m_nextTabOpenOrder = 1;
    void updateTabTitle(int index);
    void setTabCloseButton(int index);
    void updateAllTabCloseButtons();
    void updateRecentFilesMenu();
    void applyTheme(const Theme& theme);
    void applyThemeById(const QString& id);
    void applySystemTheme();
    bool isSystemDarkMode() const;
    // Spec: specs/模块-app/12-主题插件系统.md
    void rebuildThemeMenu();
    void openThemeDirectory();
    TabData* currentTab();
    TabData* tabForPath(const QString& absPath);

    // 状态栏（只读查看器）：阅读位置 / 行数 / 大小 / 索引进度 + 右侧编码
    QLabel* m_statusWordCount = nullptr;   // 复用：文件大小
    QLabel* m_statusCharCount = nullptr;   // 复用：索引进度 / 错误
    QLabel* m_statusLineCount = nullptr;
    QLabel* m_statusCursorPos = nullptr;   // 复用：阅读位置 Ln x / y
    QLabel* m_statusEncoding = nullptr;
    void setupStatusBar();
    void updateStatusBarStats();
    void updatePositionStatus();
    void updateRightStatusBar();

};

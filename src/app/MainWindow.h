#pragma once
#include <QMainWindow>
#include <QVector>
#include <QTimer>
#include <QFileSystemWatcher>
#include "Theme.h"

class EditorWidget;
class PreviewWidget;
class ParseScheduler;
class ScrollSync;
class RecentFiles;
class TocPanel;
class FolderPanel;
class TabBarWithAdd;  // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-VALIGN
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
    void onNewFile();
    void onOpenFile();
    void onOpenFolder();
    void onSaveFile();
    void onSaveFileAs();
    void onCloseTab(int index);
    void onTabChanged(int index);

private:
    struct TabData {
        QSplitter* splitter = nullptr;
        EditorWidget* editor = nullptr;
        PreviewWidget* preview = nullptr;
        ParseScheduler* scheduler = nullptr;
        ScrollSync* scrollSync = nullptr;
        bool pendingReload = false;  // 文件被外部修改，等切换到此 tab 时提示
        bool lazyPending = false;    // 懒加载：尚未加载文件内容
        QString lazyFilePath;        // 懒加载时记录的文件路径
    };

    // [Spec 模块-preview/09] 预览区链接 Ctrl+click 处理
    void onPreviewLinkClicked(const QString& url, class EditorWidget* originEditor);

    // [Plan 文档统计信息] 弹窗统计当前文档的结构化信息
    void onShowDocumentStats();

    // [Plan 首次启动引导] 显示欢迎对话框（首次启动 + 菜单手动触发）
    void onShowWelcome();
    void maybeShowWelcomeOnFirstLaunch();

    // Spec: specs/模块-app/16-崩溃报告收集.md
    // 启动后检测 %APPDATA%/SimpleMarkdown/crashes/ 下是否有"未查看"的 dump，弹窗提示
    void maybeShowCrashReportPrompt();

    // Spec: specs/模块-preview/07-TOC面板.md INV-TOC-VALIGN
    // central widget = QVBoxLayout(m_tabBar + m_mainSplitter)
    // m_mainSplitter 左：m_contentStack（每页一个 editor|preview splitter）
    //                 右：m_tocPanel
    TabBarWithAdd* m_tabBar = nullptr;
    QStackedWidget* m_contentStack = nullptr;
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

    // 持久化的视图设置
    QAction* m_restoreSessionAct = nullptr;
    QAction* m_wordWrapAct = nullptr;
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

    QFileSystemWatcher m_fileWatcher;
    void watchFile(const QString& path);
    void unwatchFile(const QString& path);
    void onFileChangedExternally(const QString& path);
    void promptReloadTab(int tabIndex);

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

    // 底部居中 toast 通知
    void showToast(const QString& message, int durationMs = 3000);

    // 显示区域模式：0=双栏, 1=仅编辑器, 2=仅预览
    int m_displayMode = 0;
    QActionGroup* m_displayGroup = nullptr;
    QAction* m_displayBothAct = nullptr;
    QAction* m_displayEditorAct = nullptr;
    QAction* m_displayPreviewAct = nullptr;
    void setDisplayMode(int mode);
    void applyDisplayMode();  // 应用到当前 tab

    // 专注模式
    bool m_focusMode = false;
    QAction* m_focusModeAct = nullptr;
    QByteArray m_savedMainSplitterState;
    QList<int> m_savedTabSplitterSizes;
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
    bool maybeSave(int index);

    // 状态栏统计信息
    QLabel* m_statusWordCount = nullptr;
    QLabel* m_statusCharCount = nullptr;
    QLabel* m_statusLineCount = nullptr;
    QLabel* m_statusCursorPos = nullptr;
    QLabel* m_statusReadTime = nullptr;
    // Spec: specs/模块-app/14-自动保存.md
    // 自动保存失败时显示瞬时提示（5s 自动清空），平时为空字符串
    QLabel* m_statusAutoSaveMsg = nullptr;
    QTimer m_autoSaveMsgClearTimer;
    QTimer m_statsDebounceTimer;
    // Spec: specs/模块-app/15-状态栏布局.md
    // 状态栏右区元数据：编码 / 换行 / 保存状态
    // （主题名字段已移除 — 用户从整个 UI 颜色即可感知当前主题，不需要再占状态栏）
    QLabel* m_statusEncoding = nullptr;
    QLabel* m_statusLineEnding = nullptr;
    QLabel* m_statusSaveStatus = nullptr;
    // 30s 周期重算"已保存 · X 分钟前"相对时间
    QTimer m_relTimeRefreshTimer;
    void setupStatusBar();
    void updateStatusBarStats();
    void updateCursorPosition(int line, int column);
    void connectTabStatusBar(const TabData& tab);
    void updateRightStatusBar();   // 编码 + 换行 + 主题 + 保存状态全部刷新
    void updateSaveStatusOnly();   // 仅刷新保存状态文本（30s timer 触发）

    // Spec: specs/模块-app/14-自动保存.md
    // 编辑停顿后静默保存（仅有磁盘路径的 Tab）
    QTimer m_autoSaveTimer;
    bool m_autoSaveEnabled = true;
    int m_autoSaveDelayMs = 1500;
    QAction* m_autoSaveEnabledAct = nullptr;
    QVector<QAction*> m_autoSaveDelayActions;  // 1500 / 3000 / 5000 三档
    void scheduleAutoSave();
    void performAutoSave();
    void showAutoSaveError(const QString& message);
};

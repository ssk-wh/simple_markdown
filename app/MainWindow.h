#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QVector>
#include <QTimer>
#include "Theme.h"

class EditorWidget;
class PreviewWidget;
class ParseScheduler;
class ScrollSync;
class RecentFiles;
class QSplitter;
class QMenu;
class QActionGroup;
class QLocalServer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFile(const QString& path);
    void newTab();
    void restoreSession(const QString& requestedFile = QString());
    void startLocalServer(const char* serverName);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewFile();
    void onOpenFile();
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
    };

    QTabWidget* m_tabWidget;
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
    QVector<QAction*> m_spacingActions;
    qreal m_lineSpacingFactor = 1.0;

    void saveSettings();
    void saveSessionLater();  // 延迟保存会话（debounce 1秒）
    void loadSettings();
    QTimer m_saveSessionTimer;

    void setupMenuBar();
    void setupDragDrop();
    TabData createTab();
    void updateTabTitle(int index);
    void updateRecentFilesMenu();
    void applyTheme(const Theme& theme);
    void applySystemTheme();
    bool isSystemDarkMode() const;
    TabData* currentTab();
    bool maybeSave(int index);
};

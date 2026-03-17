#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QVector>
#include "Theme.h"

class EditorWidget;
class PreviewWidget;
class ParseScheduler;
class ScrollSync;
class RecentFiles;
class QSplitter;
class QMenu;
class QActionGroup;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFile(const QString& path);
    void newTab();

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

    void setupMenuBar();
    void setupDragDrop();
    TabData createTab();
    void updateTabTitle(int index);
    void updateRecentFilesMenu();
    void applyTheme(const Theme& theme);
    TabData* currentTab();
    bool maybeSave(int index);
};

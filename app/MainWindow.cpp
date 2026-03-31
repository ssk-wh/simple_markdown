#include "MainWindow.h"
#include "EditorWidget.h"
#include "PreviewWidget.h"
#include "ParseScheduler.h"
#include "ScrollSync.h"
#include "Document.h"
#include "RecentFiles.h"
#include "ChangelogDialog.h"

#include <QSplitter>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCloseEvent>
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

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

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
    setCentralWidget(m_tabWidget);

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
        QMenu menu;
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

    setupMenuBar();
    setupDragDrop();
    loadSettings();
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
    connect(m_wordWrapAct, &QAction::toggled, this, [this](bool checked) {
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

    m_restoreSessionAct = viewMenu->addAction(tr("Restore Last File"));
    m_restoreSessionAct->setCheckable(true);
    m_restoreSessionAct->setChecked(true);

    // -- Help menu --
    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));

    helpMenu->addAction(tr("Update History"), this, [this]() {
        ChangelogDialog dialog(this);
        dialog.exec();
    });

    helpMenu->addSeparator();

    helpMenu->addAction(tr("About"), this, [this]() {
        QString about = QString(
            "<h2>SimpleMarkdown %1</h2>"
            "<p>A lightweight cross-platform Markdown editor.</p>"
            "<table>"
            "<tr><td><b>Author:</b></td><td>pcfan</td></tr>"
            "<tr><td><b>Build Date:</b></td><td>%2</td></tr>"
            "<tr><td><b>Qt Version:</b></td><td>%3</td></tr>"
            "</table>"
            "<p>Source: <a href=\"https://github.com/ssk-wh/simple_markdown\">"
            "https://github.com/ssk-wh/simple_markdown</a></p>"
        ).arg(QApplication::applicationVersion().isEmpty() ? "0.1.0" : QApplication::applicationVersion(),
              QString(__DATE__),
              qVersion());
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
                    int ps = s.value("previewScroll", 0).toInt();
                    QTimer::singleShot(200, this, [this, idx, es, ps]() {
                        if (idx < m_tabs.size()) {
                            m_tabs[idx].editor->verticalScrollBar()->setValue(es);
                            m_tabs[idx].preview->verticalScrollBar()->setValue(ps);
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
                    int ps = s.value("previewScroll", 0).toInt();
                    QTimer::singleShot(200, this, [this, es, ps]() {
                        if (auto* tab = currentTab()) {
                            tab->editor->verticalScrollBar()->setValue(es);
                            tab->preview->verticalScrollBar()->setValue(ps);
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
        int count = s.beginReadArray("session/tabs");
        QVector<QPair<int, int>> scrollPositions;
        bool opened = false;
        for (int i = 0; i < count; ++i) {
            s.setArrayIndex(i);
            QString fp = s.value("file").toString();
            if (!fp.isEmpty() && QFileInfo::exists(fp)) {
                openFile(fp);
                scrollPositions.append({s.value("editorScroll", 0).toInt(),
                                        s.value("previewScroll", 0).toInt()});
                opened = true;
            }
        }
        s.endArray();

        if (opened) {
            // 恢复当前标签
            int activeTab = s.value("session/activeTab", 0).toInt();
            if (activeTab >= 0 && activeTab < m_tabWidget->count())
                m_tabWidget->setCurrentIndex(activeTab);

            // 延迟恢复所有标签页的滚动位置
            QTimer::singleShot(200, this, [this, scrollPositions]() {
                for (int i = 0; i < scrollPositions.size() && i < m_tabs.size(); ++i) {
                    m_tabs[i].editor->verticalScrollBar()->setValue(scrollPositions[i].first);
                    m_tabs[i].preview->verticalScrollBar()->setValue(scrollPositions[i].second);
                }
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
    m_tabWidget->setCurrentIndex(index);

    tab.editor->document()->loadFromFile(path);
    tab.scheduler->parseNow();

    m_recentFiles->addFile(path);
    updateTabTitle(index);

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
    if (!qFuzzyCompare(m_lineSpacingFactor, 1.0))
        tab.editor->setLineSpacing(m_lineSpacingFactor);

    // Track modifications for tab title
    connect(tab.editor->document(), &Document::modifiedChanged,
            this, [this](bool) {
        int idx = m_tabWidget->currentIndex();
        if (idx >= 0)
            updateTabTitle(idx);
    });

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
        title += " *";

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
        m_recentMenu->addAction(QFileInfo(file).fileName(), [this, file]() {
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

    // 主窗口 UI 元素跟随深色/浅色主题（含菜单栏、右键菜单、对话框）
    if (theme.isDark) {
        setStyleSheet(QStringLiteral(
            "QMainWindow { background: #2b2b2b; }"
            // 菜单栏
            "QMenuBar { background: #2b2b2b; color: #ccc; border-bottom: 1px solid #3c3f41; }"
            "QMenuBar::item { padding: 6px 10px; }"
            "QMenuBar::item:selected { background: #3c3f41; border-bottom: 2px solid #4a9eff; }"
            // 菜单（包括右键菜单）
            "QMenu { background: #2b2b2b; color: #ccc; border: 1px solid #555; padding: 4px 0; }"
            "QMenu::item { padding: 6px 24px 6px 12px; }"
            "QMenu::item:selected { background: #3c3f41; border-left: 2px solid #4a9eff; }"
            "QMenu::separator { background: #555; height: 1px; margin: 4px 8px; }"
            // Tab 栏
            "QTabWidget::pane { border: none; }"
            "QTabBar { background: #2b2b2b; }"
            "QTabBar::tab { background: #2b2b2b; color: #aaa; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: #fff; border-bottom: 2px solid #4a9eff; }"
            "QTabBar::tab:hover { color: #ddd; background: #353535; }"
            "QTabBar::close-button { image: url(none); }"
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
        ));
    } else {
        setStyleSheet(QStringLiteral(
            // 菜单栏
            "QMenuBar { border-bottom: 1px solid #e0e0e0; }"
            "QMenuBar::item { padding: 6px 10px; }"
            "QMenuBar::item:selected { background: #e8e8e8; border-bottom: 2px solid #0078d4; }"
            // 菜单
            "QMenu { border: 1px solid #d0d0d0; padding: 4px 0; }"
            "QMenu::item { padding: 6px 24px 6px 12px; }"
            "QMenu::item:selected { background: #e8f0fe; border-left: 2px solid #0078d4; }"
            "QMenu::separator { background: #e0e0e0; height: 1px; margin: 4px 8px; }"
            // Tab 栏
            "QTabWidget::pane { border: none; }"
            "QTabBar { background: #f0f0f0; }"
            "QTabBar::tab { background: #f0f0f0; color: #666; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: #333; border-bottom: 2px solid #0078d4; background: #fff; }"
            "QTabBar::tab:hover { color: #333; background: #e8e8e8; }"
            "QTabBar::close-button { image: url(none); }"
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
        ));
    }

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
            doc->saveToFile();
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
        doc->saveToFile();
        updateTabTitle(m_tabWidget->currentIndex());
    }
}

void MainWindow::onSaveFileAs()
{
    auto* tab = currentTab();
    if (!tab) return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Save File As"), QString(),
        tr("Markdown Files (*.md *.markdown);;All Files (*)"));
    if (path.isEmpty()) return;

    tab->editor->document()->saveToFile(path);
    m_recentFiles->addFile(path);
    updateTabTitle(m_tabWidget->currentIndex());
}

void MainWindow::onCloseTab(int index)
{
    if (!maybeSave(index))
        return;

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
}

// -- Drag & Drop --

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            QString path = url.toLocalFile();
            if (path.endsWith(".md", Qt::CaseInsensitive) ||
                path.endsWith(".markdown", Qt::CaseInsensitive) ||
                path.endsWith(".txt", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (!path.isEmpty())
            openFile(path);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
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
    // 主题：follow_system / light / dark
    QString themeMode = "follow_system";
    if (m_lightThemeAct && m_lightThemeAct->isChecked()) themeMode = "light";
    else if (m_darkThemeAct && m_darkThemeAct->isChecked()) themeMode = "dark";
    s.setValue("view/themeMode", themeMode);
    s.setValue("window/geometry", saveGeometry());

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
        s.setValue("previewScroll", m_tabs[i].preview->verticalScrollBar()->value());
    }
    s.endArray();
}

void MainWindow::loadSettings()
{
    QSettings s;

    // 窗口位置
    if (s.contains("window/geometry"))
        restoreGeometry(s.value("window/geometry").toByteArray());

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
    m_lineSpacingFactor = s.value("view/lineSpacing", 1.0).toDouble();
    // 更新菜单选中状态
    double spacings[] = {1.0, 1.2, 1.5, 1.8, 2.0};
    for (int i = 0; i < m_spacingActions.size() && i < 5; ++i) {
        if (qFuzzyCompare(spacings[i], m_lineSpacingFactor))
            m_spacingActions[i]->setChecked(true);
    }
    // 应用到已有标签页
    for (auto& tab : m_tabs) {
        tab.editor->setLineSpacing(m_lineSpacingFactor);
        tab.editor->setWordWrap(wordWrap);
        tab.preview->setWordWrap(wordWrap);
    }
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

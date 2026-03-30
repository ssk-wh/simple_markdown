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
    });

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
    // Check if already open in a tab
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].editor->document()->filePath() == QFileInfo(path).absoluteFilePath()) {
            m_tabWidget->setCurrentIndex(i);
            // 如果文件已打开，切换到该标签页后提升窗口
            setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            raise();
            activateWindow();
            return;
        }
    }

    TabData tab = createTab();
    int index = m_tabWidget->addTab(tab.splitter, QFileInfo(path).fileName());
    m_tabs.append(tab);
    m_tabWidget->setCurrentIndex(index);

    tab.editor->document()->loadFromFile(path);
    tab.scheduler->parseNow();

    m_recentFiles->addFile(path);
    updateTabTitle(index);

    // 加载新文件后提升窗口，确保用户能看到
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
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

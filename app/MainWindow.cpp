#include "MainWindow.h"
#include "EditorWidget.h"
#include "PreviewWidget.h"
#include "ParseScheduler.h"
#include "ScrollSync.h"
#include "Document.h"
#include "RecentFiles.h"

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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("EasyMarkdown");
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

    setupMenuBar();
    setupDragDrop();
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

    QAction* lightThemeAct = viewMenu->addAction(tr("Light Theme"));
    lightThemeAct->setCheckable(true);
    lightThemeAct->setChecked(true);
    themeGroup->addAction(lightThemeAct);

    QAction* darkThemeAct = viewMenu->addAction(tr("Dark Theme"));
    darkThemeAct->setCheckable(true);
    themeGroup->addAction(darkThemeAct);

    connect(lightThemeAct, &QAction::triggered, this, [this]() {
        applyTheme(Theme::light());
    });
    connect(darkThemeAct, &QAction::triggered, this, [this]() {
        applyTheme(Theme::dark());
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
        "# EasyMarkdown\n"
        "\n"
        "A **lightweight** cross-platform Markdown editor.\n"
    );
    // parseNow will be triggered by textChanged → debounce
}

void MainWindow::openFile(const QString& path)
{
    // Check if already open in a tab
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].editor->document()->filePath() == QFileInfo(path).absoluteFilePath()) {
            m_tabWidget->setCurrentIndex(i);
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

    // Apply current theme
    tab.editor->setTheme(m_currentTheme);
    tab.preview->setTheme(m_currentTheme);

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

    // Update window title
    if (index == m_tabWidget->currentIndex())
        setWindowTitle(title + " - EasyMarkdown");
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
    event->accept();
}

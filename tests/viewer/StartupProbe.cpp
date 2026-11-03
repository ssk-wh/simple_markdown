// tests/viewer/StartupProbe.cpp
//
// Spec: specs/横切关注点/70-性能预算.md
// 用途：逐项测量 Qt 启动期各步骤耗时（手动运行，不入 ctest），定位冷启动瓶颈。
#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QIcon>
#include <QPixmap>
#include <QImageReader>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>
#include <QTranslator>
#include <QTreeView>
#include <QStandardItemModel>
#include <cstdio>
#include <thread>
#include <set>
#include <string>
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

static std::set<std::string> currentModules()
{
    std::set<std::string> out;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i) {
            char name[MAX_PATH];
            if (GetModuleFileNameExA(GetCurrentProcess(), mods[i], name, MAX_PATH)) out.insert(name);
        }
    }
    return out;
}
static std::set<std::string> g_seen;
static void printNewModules(const char* when)
{
    auto now = currentModules();
    std::printf("  [%s] new modules:", when);
    int n = 0;
    for (const auto& m : now) if (!g_seen.count(m)) { auto pos = m.find_last_of("/\\"); std::printf(" %s", m.substr(pos + 1).c_str()); ++n; }
    std::printf(" (%d)\n", n);
    g_seen = now;
}

static QElapsedTimer g_t;
static void lap(const char* name)
{
    std::printf("%-34s %6lld ms\n", name, (long long)g_t.restart());
    std::fflush(stdout);
}

int main(int argc, char** argv)
{
    g_t.start();
    QApplication app(argc, argv);
    lap("QApplication ctor");
    if (!qEnvironmentVariableIsEmpty("WARM")) {
        std::thread([]() {
            QElapsedTimer t; t.start();
            for (const char* fam : {"Segoe UI", "Consolas"}) {
                for (int bold = 0; bold < 2; ++bold) {
                    for (int it = 0; it < 2; ++it) {
                        QFont f(fam); f.setPixelSize(14); f.setBold(bold); f.setItalic(it);
                        QFontMetricsF fm(f);
                        (void)fm.horizontalAdvance(QStringLiteral("Ag 中文"));
                    }
                }
            }
            std::printf("  [warm thread done in %lld ms]\n", (long long)t.elapsed());
        }).detach();
    }
    app.setApplicationName("SimpleMarkdown");
    app.setOrganizationName("SimpleMarkdown");
    lap("set app names");
    QIcon icon0(QPixmap(QStringLiteral("D:/iflytek_projects/simple_markdown/resources/app-icon.png")));
    lap("QIcon(QPixmap(png))");
    QIcon icon(QStringLiteral("D:/iflytek_projects/simple_markdown/resources/app-icon.png"));
    lap("QIcon(path) 2nd");
    app.setWindowIcon(QIcon(QStringLiteral("D:/iflytek_projects/simple_markdown/resources/app-icon.png")));
    lap("setWindowIcon(png)");
    {
        QSettings s;
        (void)s.value("language/locale", "zh_CN").toString();
    }
    lap("QSettings read");
    {
        QTranslator t;
        t.load("D:/iflytek_projects/simple_markdown/translations/qt_zh_CN.qm");
        app.installTranslator(&t);
        app.removeTranslator(&t);
    }
    lap("QTranslator qt_zh_CN");
    QMainWindow w;
    lap("QMainWindow ctor");
    QLabel l("hello", &w);
    lap("QLabel ctor");
    QFontMetricsF fm(l.font());
    (void)fm.horizontalAdvance(QStringLiteral("中文 abc"));
    lap("first QFontMetricsF + CJK advance");
    {
        QLabel t1(QStringLiteral("Contents"), &w);
        QFont f = t1.font(); f.setPixelSize(14); f.setBold(true); t1.setFont(f);
        (void)t1.sizeHint();
        lap("bold14 latin QLabel sizeHint");
        QLabel t2(QStringLiteral("目录"), &w);
        t2.setFont(f);
        (void)t2.sizeHint();
        lap("bold14 CJK QLabel sizeHint");
        QLabel t3(&w);
        t3.setWordWrap(true);
        t3.setTextFormat(Qt::RichText);
        t3.setText(QStringLiteral("<div>字数: 12</div><div>Lines: 3</div>"));
        (void)t3.sizeHint();
        lap("rich text wrap CJK QLabel sizeHint");
        QToolButton tb2(&w);
        tb2.setText(QStringLiteral("− 文档信息"));
        tb2.setAutoRaise(true);
        (void)tb2.sizeHint();
        lap("QToolButton CJK sizeHint");
    }
    QSplitter sp(Qt::Horizontal, &w);
    lap("QSplitter ctor");
    QTabBar tb(&w);
    tb.addTab("a");
    lap("QTabBar ctor+addTab");
    QStandardItemModel model;
    QTreeView tv(&w);
    tv.setModel(&model);
    lap("QTreeView ctor");
    w.menuBar()->addMenu("File")->addAction("x");
    lap("menuBar addMenu");
    w.resize(1280, 800);
    printNewModules("before winId");
    w.winId();
    lap("winId() (HWND create)");
    printNewModules("after winId");
    w.show();
    lap("show()");
    printNewModules("after show");
    QApplication::processEvents();
    lap("first processEvents (paint)");
    return 0;
}

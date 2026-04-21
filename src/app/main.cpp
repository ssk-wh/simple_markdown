#include <QApplication>
#include <QByteArray>
#include <QIcon>
#include <QFileInfo>
#include <QLocalSocket>
#include <QSettings>
#include <QLibraryInfo>
#include "MainWindow.h"
#include "../core/PerfProbe.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shell32.lib")

// Spec: specs/模块-app/16-崩溃报告收集.md
// 在崩溃时刻分配内存或调用 QStandardPaths 都不安全；启动时把目录路径预解析到全局缓冲区，
// crashHandler 只用 Win32 API（无 CRT 内存分配） + 预填路径写 dump。
static wchar_t g_crashDirW[MAX_PATH * 2] = { 0 };
static wchar_t g_crashDmpW[MAX_PATH * 2] = { 0 };
static wchar_t g_crashTxtW[MAX_PATH * 2] = { 0 };

static void prepareCrashDir()
{
    wchar_t appdata[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata) != S_OK)
        return;
    // %APPDATA%/SimpleMarkdown/crashes/  (注：注释里不能用反斜杠，会被当作行继续)
    _snwprintf_s(g_crashDirW, _countof(g_crashDirW), _TRUNCATE,
                 L"%ls\\SimpleMarkdown\\crashes", appdata);
    SHCreateDirectoryExW(NULL, g_crashDirW, NULL);   // 已存在不报错
    _snwprintf_s(g_crashDmpW, _countof(g_crashDmpW), _TRUNCATE,
                 L"%ls\\crash.dmp", g_crashDirW);
    _snwprintf_s(g_crashTxtW, _countof(g_crashTxtW), _TRUNCATE,
                 L"%ls\\crash_report.txt", g_crashDirW);
}

static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep)
{
    // 写文本报告（不依赖 CRT fopen，避免崩溃时刻 CRT 状态不一致）
    if (g_crashTxtW[0] != 0) {
        HANDLE txtFile = CreateFileW(g_crashTxtW, GENERIC_WRITE, 0, NULL,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (txtFile != INVALID_HANDLE_VALUE) {
            char buf[512];
            DWORD written = 0;

            // 异常代码 + 地址
            int n = _snprintf_s(buf, _countof(buf), _TRUNCATE,
                                "Exception Code: 0x%08lX\nException Address: %p\n",
                                ep->ExceptionRecord->ExceptionCode,
                                ep->ExceptionRecord->ExceptionAddress);
            if (n > 0) WriteFile(txtFile, buf, (DWORD)n, &written, NULL);

            // 崩溃模块
            HMODULE hMod = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &hMod);
            if (hMod) {
                char name[MAX_PATH];
                DWORD nameLen = GetModuleFileNameA(hMod, name, MAX_PATH);
                n = _snprintf_s(buf, _countof(buf), _TRUNCATE,
                                "Crash module: %.*s\nOffset: 0x%llX\n",
                                (int)nameLen, name,
                                (unsigned long long)((char*)ep->ExceptionRecord->ExceptionAddress - (char*)hMod));
                if (n > 0) WriteFile(txtFile, buf, (DWORD)n, &written, NULL);
            }

            // 时间戳
            SYSTEMTIME st;
            GetLocalTime(&st);
            n = _snprintf_s(buf, _countof(buf), _TRUNCATE,
                            "Timestamp: %04d-%02d-%02d %02d:%02d:%02d\n",
                            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            if (n > 0) WriteFile(txtFile, buf, (DWORD)n, &written, NULL);

            CloseHandle(txtFile);
        }
    }

    // 写 minidump
    if (g_crashDmpW[0] != 0) {
        HANDLE dumpFile = CreateFileW(g_crashDmpW, GENERIC_WRITE, 0, NULL,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (dumpFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                              dumpFile, MiniDumpNormal, &mei, NULL, NULL);
            CloseHandle(dumpFile);
        }
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static const char* kServerName = "SimpleMarkdownInstance";

static bool sendToRunningInstance(const QString& filePath)
{
    QLocalSocket socket;
    socket.connectToServer(kServerName);
    if (!socket.waitForConnected(500))
        return false;

    // 发送文件路径（空路径也发，用于激活窗口）
    QByteArray data = filePath.toUtf8();
    socket.write(data);
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Spec: specs/模块-app/16-崩溃报告收集.md
    // 必须先准备好崩溃目录与路径缓冲，再注册异常处理器
    prepareCrashDir();
    SetUnhandledExceptionFilter(crashHandler);
#endif

    // Diagnostic output to stderr (visible in console)
    fprintf(stderr, "[1] main() started\n");
    fflush(stderr);

    // Spec: specs/模块-app/17-性能监控.md
    // 启动时读环境变量 SM_PERF；值为 "1" / "true" / "on" 即启用
    // 运行时也可通过 菜单 帮助 → 诊断 → 性能日志 toggle
    {
        const QByteArray envPerf = qgetenv("SM_PERF");
        if (envPerf == "1" || envPerf.compare("true",  Qt::CaseInsensitive) == 0
                            || envPerf.compare("on",   Qt::CaseInsensitive) == 0) {
            core::setPerfEnabled(true);
            fprintf(stderr, "[perf] enabled via SM_PERF env var\n");
            fflush(stderr);
        }
    }

    QApplication app(argc, argv);
    fprintf(stderr, "[2] QApplication created\n");
    fflush(stderr);

    app.setApplicationName("SimpleMarkdown");
    app.setOrganizationName("SimpleMarkdown");
    app.setApplicationVersion(APP_VERSION);
    app.setWindowIcon(QIcon(":/app-icon.png"));
    fprintf(stderr, "[3] App properties set\n");
    fflush(stderr);

    // 翻译由 MainWindow 管理（支持运行时切换）
    fprintf(stderr, "[3.1] Translator will be loaded by MainWindow\n");
    fflush(stderr);

    QString filePath;
    if (argc > 1)
        filePath = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();

    // 单实例：如果已有实例运行，发送文件路径后退出
    if (sendToRunningInstance(filePath)) {
        fprintf(stderr, "[4] Sent to existing instance, exiting\n");
        fflush(stderr);
        return 0;
    }
    fprintf(stderr, "[5] Creating new window\n");
    fflush(stderr);

    MainWindow window;
    fprintf(stderr, "[6] MainWindow created\n");
    fflush(stderr);

    window.startLocalServer(kServerName);
    fprintf(stderr, "[7] Local server started\n");
    fflush(stderr);

    window.restoreSession(filePath);
    fprintf(stderr, "[8] Session restored\n");
    fflush(stderr);

    window.show();
    fprintf(stderr, "[9] Window shown\n");
    fflush(stderr);

    int result = app.exec();
    fprintf(stderr, "[10] app.exec() returned %d\n", result);
    fflush(stderr);
    return result;
}

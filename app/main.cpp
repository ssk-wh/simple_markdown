#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QLocalSocket>
#include "MainWindow.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep)
{
    FILE* f = fopen("crash_report.txt", "w");
    if (f) {
        fprintf(f, "Exception Code: 0x%08lX\n", ep->ExceptionRecord->ExceptionCode);
        fprintf(f, "Exception Address: %p\n", ep->ExceptionRecord->ExceptionAddress);

        // Find which module the crash address belongs to
        HMODULE hMod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &hMod);
        if (hMod) {
            char name[MAX_PATH];
            GetModuleFileNameA(hMod, name, MAX_PATH);
            fprintf(f, "Crash module: %s\n", name);
            fprintf(f, "Offset: 0x%llX\n",
                    (unsigned long long)((char*)ep->ExceptionRecord->ExceptionAddress - (char*)hMod));
        }

        // Write minidump
        HANDLE dumpFile = CreateFileA("crash.dmp", GENERIC_WRITE, 0, NULL,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (dumpFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                              dumpFile, MiniDumpNormal, &mei, NULL, NULL);
            CloseHandle(dumpFile);
            fprintf(f, "Minidump written to crash.dmp\n");
        }
        fclose(f);
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
    SetUnhandledExceptionFilter(crashHandler);
#endif

    // Diagnostic output to stderr (visible in console)
    fprintf(stderr, "[1] main() started\n");
    fflush(stderr);

    QApplication app(argc, argv);
    fprintf(stderr, "[2] QApplication created\n");
    fflush(stderr);

    app.setApplicationName("SimpleMarkdown");
    app.setOrganizationName("SimpleMarkdown");
    app.setWindowIcon(QIcon(":/app-icon.png"));
    fprintf(stderr, "[3] App properties set\n");
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

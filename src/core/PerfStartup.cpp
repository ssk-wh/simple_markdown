// src/core/PerfStartup.cpp
//
// Spec: specs/模块-app/17-性能监控.md
// 启动里程碑：自进程创建起的毫秒数（仅 perfEnabled() 时输出）
#include "PerfProbe.h"

#include <cstdio>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace core {

void startupMark(const char* label)
{
    if (!perfEnabled()) return;
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER a, b;
        a.LowPart = created.dwLowDateTime;
        a.HighPart = created.dwHighDateTime;
        b.LowPart = now.dwLowDateTime;
        b.HighPart = now.dwHighDateTime;
        std::fprintf(stderr, "[startup] %-28s %6lld ms since process start\n", label,
                     static_cast<long long>((b.QuadPart - a.QuadPart) / 10000));
        std::fflush(stderr);
        return;
    }
#endif
    std::fprintf(stderr, "[startup] %s\n", label);
    std::fflush(stderr);
}

}  // namespace core

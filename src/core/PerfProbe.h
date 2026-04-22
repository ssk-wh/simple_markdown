// src/core/PerfProbe.h
//
// Spec: specs/模块-app/17-性能监控.md
// Last synced: 2026-04-15
//
// Header-only 性能埋点工具。
//
// 用法：
//   void MyFunc() {
//       SM_PERF_SCOPE("parser.doParse");
//       // ... 耗时操作 ...
//   }
//   析构时若开关开启，向 stderr 写 `[perf] parser.doParse: 1234 us`
//
// 开关途径（任意一种开启即可）：
//   1. 命令行参数 --debug / -d（启动时读取，main.cpp 里处理）
//   2. 环境变量 SM_PERF=1（启动时读取一次，命令行未指定时回退）
//
// Release 开销：开关关闭时，SM_PERF_SCOPE 只构造一个 bool + 不启动 timer，
// 析构时一个 bool 判断即 return。编译器可在 -O2 下几乎完全消除。

#pragma once

#include <QElapsedTimer>
#include <atomic>
#include <cstdio>

namespace core {

// 全局开关（C++17 inline 变量：多 TU 共享同一实例，无需 .cpp）
inline std::atomic<bool> g_perfEnabled{false};

inline void setPerfEnabled(bool on) {
    g_perfEnabled.store(on, std::memory_order_relaxed);
}
inline bool perfEnabled() {
    return g_perfEnabled.load(std::memory_order_relaxed);
}

class PerfScope {
public:
    explicit PerfScope(const char* name)
        : m_name(name), m_enabled(perfEnabled())
    {
        if (m_enabled) m_timer.start();
    }
    ~PerfScope() {
        if (m_enabled) {
            const qint64 us = m_timer.nsecsElapsed() / 1000;
            std::fprintf(stderr, "[perf] %s: %lld us\n", m_name, static_cast<long long>(us));
            std::fflush(stderr);
        }
    }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    const char*    m_name;
    bool           m_enabled;
    QElapsedTimer  m_timer;
};

} // namespace core

// 宏让埋点行号自动变成唯一变量名，避免一个 scope 里多个 probe 冲突
#define SM_PERF_CONCAT_(a, b) a##b
#define SM_PERF_CONCAT(a, b) SM_PERF_CONCAT_(a, b)
#define SM_PERF_SCOPE(name_literal) \
    ::core::PerfScope SM_PERF_CONCAT(_sm_perf_, __LINE__)(name_literal)

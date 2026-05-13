// src/core/LongTaskProbe.h
//
// Spec: specs/横切关注点/70-性能预算.md INV-6 主线程不阻塞
// Plan: plans/归档/2026-05-12-A4主线程长任务探针.md
//
// 主线程长任务探针：替换 QApplication 为子类 SmApplication，重写 notify()
// 自动测量每个事件处理耗时。超过 16ms（INV-6 单帧阈值）写 stderr：
//   [long-task] <us> us | <receiver class> -> <event type>
//
// 开关：复用 PerfProbe 的 core::perfEnabled()——命令行 --debug / -d 或
// 环境变量 SM_PERF=1 启用。关闭时 notify 路径只多一次 atomic load + 早返回，
// release 几乎零开销。

#pragma once

#include <QApplication>
#include <QEvent>
#include <QElapsedTimer>
#include <QMetaObject>
#include <cstdio>
#include "PerfProbe.h"

namespace core {

class SmApplication : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* e) override {
        if (!perfEnabled()) {
            return QApplication::notify(receiver, e);
        }
        QElapsedTimer t;
        t.start();
        const bool result = QApplication::notify(receiver, e);
        const qint64 us = t.nsecsElapsed() / 1000;
        // 16ms = 16000us 单帧预算（60fps）
        if (us > 16000) {
            const char* recvClass = (receiver && receiver->metaObject())
                                     ? receiver->metaObject()->className()
                                     : "null";
            const int evType = e ? static_cast<int>(e->type()) : -1;
            std::fprintf(stderr,
                         "[long-task] %lld us | %s -> event type %d\n",
                         static_cast<long long>(us), recvClass, evType);
            std::fflush(stderr);
        }
        return result;
    }
};

} // namespace core

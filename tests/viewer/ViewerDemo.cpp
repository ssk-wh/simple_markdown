// tests/viewer/ViewerDemo.cpp
//
// Spec: specs/模块-viewer/04-查看器控件.md
// 用途：无主窗口的 ViewerWidget 演示/截图/计时工具（手动运行，不入 ctest）。
// 用法：ViewerDemo <md> [--shot out.png] [--line N] [--dark] [--w 1100] [--h 800] [--scroll N] [--end]
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWheelEvent>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include "ViewerWidget.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QString file, shot;
    int line = -1, w = 1100, h = 800, scrollN = 0, benchN = 0, benchStep = 60;
    bool dark = false, toEnd = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--line") && i + 1 < argc) line = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--w") && i + 1 < argc) w = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--h") && i + 1 < argc) h = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scroll") && i + 1 < argc) scrollN = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--bench") && i + 1 < argc) benchN = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--step") && i + 1 < argc) benchStep = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--dark")) dark = true;
        else if (!std::strcmp(argv[i], "--end")) toEnd = true;
        else file = QString::fromLocal8Bit(argv[i]);
    }
    QElapsedTimer t;
    t.start();
    viewer::ViewerWidget v;
    v.resize(w, h);
    v.setTheme(dark ? Theme::dark() : Theme::light());
    v.show();
    QApplication::processEvents();
    std::printf("window shown at %lld ms\n", (long long)t.elapsed());
    bool first = false;
    QObject::connect(&v, &viewer::ViewerWidget::firstContentReady, [&]() {
        first = true;
        std::printf("first content ready at %lld ms (loader-relative %lld ms)\n",
                    (long long)t.elapsed(), (long long)v.firstContentMs());
    });
    v.loadFile(file);
    while (!first && t.elapsed() < 20000) QApplication::processEvents(QEventLoop::AllEvents, 20);
    // 等待加载完成（目录/行数就绪）
    while (v.isLoading() && t.elapsed() < 30000) QApplication::processEvents(QEventLoop::AllEvents, 20);
    std::printf("load finished at %lld ms: chunks=%zu lines=%u toc=%d\n", (long long)t.elapsed(),
                v.chunkCount(), v.lineCount(), v.tocEntries().size());
    if (line >= 0) {
        QElapsedTimer j;
        j.start();
        v.scrollToSourceLine(line, false);
        v.repaint();
        std::printf("jump to line %d took %lld ms; anchor=(%zu,%.1f) topLine=%d\n", line,
                    (long long)j.elapsed(), v.scrollModel().anchorChunk(),
                    v.scrollModel().anchorOffset(), v.topSourceLine());
    }
    if (toEnd) v.scrollToBottom();
    for (int i = 0; i < scrollN; ++i) {
        QElapsedTimer j;
        j.start();
        QWheelEvent we(QPointF(300, 300), QPointF(300, 300), QPoint(), QPoint(0, -120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(v.viewport(), &we);
        for (int k = 0; k < 30; ++k) { QApplication::processEvents(); v.repaint(); }
        if (i % 10 == 0) std::printf("  wheel %d: %lld ms (cache=%zu)\n", i, (long long)j.elapsed(), v.cachedChunkCount());
    }
    if (benchN > 0) {
        // 逐帧滚动基准：每帧 = 滚轮事件（像素级）+ 同步重绘
        std::vector<double> ms;
        for (int i = 0; i < benchN; ++i) {
            QElapsedTimer j;
            j.start();
            QWheelEvent we(QPointF(300, 300), QPointF(300, 300), QPoint(0, -benchStep), QPoint(0, -benchStep),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(v.viewport(), &we);
            v.viewport()->repaint();
            ms.push_back(j.nsecsElapsed() / 1e6);
            QApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        std::sort(ms.begin(), ms.end());
        auto pick = [&](double q) { return ms[static_cast<size_t>(q * (ms.size() - 1))]; };
        std::printf("scroll bench N=%d step=%dpx: p50=%.2f p95=%.2f p99=%.2f max=%.2f ms; cache=%zu\n",
                    benchN, benchStep, pick(0.5), pick(0.95), pick(0.99), ms.back(), v.cachedChunkCount());
    }
    v.repaint();
    QApplication::processEvents();
    if (!shot.isEmpty()) {
        const QImage img = v.grab().toImage();
        img.save(shot);
        std::printf("saved %s (%dx%d)\n", qPrintable(shot), img.width(), img.height());
    }
    return 0;
}

// tests/preview/OpenLatencyBenchmark.cpp
//
// Spec: specs/横切关注点/70-性能预算.md
// 用途：测量大文件「读文件 → 解析 → 布局」各阶段耗时，作为秒开改造的基线。
// 用法：OpenLatencyBenchmark <md文件路径>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <cstdio>
#include "MarkdownParser.h"
#include "PreviewLayout.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc < 2) return 2;
    QElapsedTimer t;
    t.start();
    QFile f(argv[1]);
    if (!f.open(QIODevice::ReadOnly)) return 3;
    const QString text = QString::fromUtf8(f.readAll());
    std::printf("read+decode      %6lld ms (%d chars)\n", t.restart(), (int)text.size());

    MarkdownParser parser;
    std::shared_ptr<AstNode> ast(parser.parse(text).release());
    std::printf("cmark parse+conv %6lld ms\n", t.restart());

    QImage dev(1, 1, QImage::Format_ARGB32);
    PreviewLayout layout;
    layout.setViewportWidth(900);
    layout.updateMetrics(&dev);
    t.restart();
    layout.setViewportYRange(0, 3000);
    layout.buildFromAst(ast);
    std::printf("layout (viewport)%6lld ms, totalH=%.0f\n", t.restart(), layout.totalHeight());
    layout.clearViewportYRange();
    layout.buildFromAst(ast);
    std::printf("layout (full)    %6lld ms, totalH=%.0f\n", t.restart(), layout.totalHeight());
    return 0;
}

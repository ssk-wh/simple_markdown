// tests/preview/OpenLatencyBenchmark.cpp
//
// Spec: specs/横切关注点/70-性能预算.md
// 用途：测量「读文件 → 解析 → 布局」各阶段耗时（整篇管线 vs 分块管线对照），手动运行，不入 ctest。
// 用法：OpenLatencyBenchmark <md文件路径>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <cstdio>
#include <functional>
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
    const QByteArray bytes = f.readAll();
    const QString text = QString::fromUtf8(bytes);
    std::printf("read+decode      %6lld ms (%d chars)\n", (long long)t.restart(), (int)text.size());

    MarkdownParser parser;
    std::shared_ptr<AstNode> ast(parser.parse(text).release());
    std::printf("cmark parse+conv %6lld ms\n", (long long)t.restart());

    QImage dev(1, 1, QImage::Format_ARGB32);
    PreviewLayout layout;
    layout.setViewportWidth(900);
    layout.updateMetrics(&dev);
    t.restart();
    layout.buildFromAst(ast);
    std::printf("layout (full)    %6lld ms, totalH=%.0f\n", (long long)t.restart(), layout.totalHeight());

    // 分块管线对照：整个文件当作一个 chunk 解析 + layoutChunk
    auto root = parser.parseChunk(bytes, 0, true);
    const qint64 parseMs = t.restart();
    std::function<int(const AstNode&)> count = [&](const AstNode& n) {
        int c = 1;
        for (auto& k : n.children) c += count(*k);
        return c;
    };
    const int nodes = count(*root);
    t.restart();
    auto res = layout.layoutChunk(*root, true);
    std::printf("parseChunk %lld ms (nodes=%d), layoutChunk %lld ms, height=%.0f\n", (long long)parseMs, nodes,
                (long long)t.restart(), res.height);
    return 0;
}

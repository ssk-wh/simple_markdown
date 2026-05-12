// tests/preview/PreviewRenderBenchmark.cpp
//
// Spec: specs/横切关注点/70-性能预算.md
//       specs/模块-preview/02-布局引擎.md
// 用途：
//   1) 作为大文件渲染的性能基线脚本（≥1000 行 Markdown）
//   2) 用于 plan 「2026-05-06-千行文档拖拽分隔条预览重排卡顿」 优化前后对比
//   3) 长期资产：字号一致性、Tab 休眠、标记锚点改造等改动均可复用同一基准
//
// 输出：
//   - 场景 A：宽度从 800 到 1600 每隔 20px 触发一次 rebuildLayout（模拟拖拽）
//   - 场景 B：固定宽度反复 buildFromAst（衡量稳态成本）
// 度量：
//   QElapsedTimer::nsecsElapsed() → ms，输出 P50 / P95 / P99 / Mean
//
// 注意：
//   - 本测试无断言，永远 PASS；目的是输出数字、留下基线
//   - warmup 5 次后再正式测量（首次 build 含缓存、字体回退等抖动）
//   - 用 QImage 作 paint device，离屏度量稳定，不弹窗

#include <gtest/gtest.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <algorithm>
#include <cstdio>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewRenderBenchmark";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override {}
private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

// 拼接生成 ≥targetLines 行的 Markdown 样例文档；
// 涵盖 heading / paragraph / list / fenced code / table / blockquote / inline 富文本
QString generateLargeMarkdown(int targetLines)
{
    QString out;
    out.reserve(targetLines * 80);
    int lineCount = 0;
    int sectionIdx = 0;
    while (lineCount < targetLines) {
        out += QString("# Section %1: heading content with **bold** and *italic*\n\n").arg(sectionIdx);
        lineCount += 2;

        for (int p = 0; p < 3; ++p) {
            out += QString("Paragraph %1.%2 contains **bold**, *italic*, `inline code`, "
                           "a [link](https://example.com), and some plain prose to make "
                           "wrapping behavior realistic across viewport widths.\n\n")
                       .arg(sectionIdx).arg(p);
            lineCount += 2;
        }

        out += "```cpp\n";
        out += QString("int foo_%1(int x, int y) {\n").arg(sectionIdx);
        out += "    // sample fenced code block\n";
        out += "    return x + y * 2;\n";
        out += "}\n";
        out += "```\n\n";
        lineCount += 7;

        for (int i = 0; i < 5; ++i) {
            out += QString("- List item %1.%2 with **bold** highlight inside\n")
                       .arg(sectionIdx).arg(i);
            ++lineCount;
        }
        out += "\n";
        ++lineCount;

        out += "| Column A | Column B | Column C |\n";
        out += "|----------|----------|----------|\n";
        out += QString("| a%1.1    | b%1.1    | c%1.1    |\n").arg(sectionIdx);
        out += QString("| a%1.2    | b%1.2    | c%1.2    |\n").arg(sectionIdx);
        out += "\n";
        lineCount += 5;

        out += QString("## Subsection %1.1\n\n").arg(sectionIdx);
        lineCount += 2;

        out += "> Quoted text providing context. Continue on next line.\n";
        out += "> Second blockquote line.\n\n";
        lineCount += 3;

        ++sectionIdx;
    }
    return out;
}

struct Stats {
    qreal p50_ms;
    qreal p95_ms;
    qreal p99_ms;
    qreal mean_ms;
    qreal min_ms;
    qreal max_ms;
    int count;
};

Stats summarize(QVector<qint64>& nss)
{
    std::sort(nss.begin(), nss.end());
    Stats s{};
    s.count = nss.size();
    if (s.count == 0) return s;
    auto pick = [&](double q) -> qreal {
        int idx = static_cast<int>(q * (nss.size() - 1));
        return nss[idx] / 1e6;
    };
    s.p50_ms = pick(0.50);
    s.p95_ms = pick(0.95);
    s.p99_ms = pick(0.99);
    s.min_ms = nss.first() / 1e6;
    s.max_ms = nss.last() / 1e6;
    qreal sum = 0;
    for (auto v : nss) sum += v;
    s.mean_ms = (sum / nss.size()) / 1e6;
    return s;
}

void printStats(const char* label, const Stats& s)
{
    std::fprintf(stdout,
                 "  [%s] N=%d  P50=%.3f ms  P95=%.3f ms  P99=%.3f ms  "
                 "mean=%.3f ms  min=%.3f ms  max=%.3f ms\n",
                 label, s.count, s.p50_ms, s.p95_ms, s.p99_ms,
                 s.mean_ms, s.min_ms, s.max_ms);
    std::fflush(stdout);
}

}  // namespace

// 基线报告：跑场景 A（拖拽模拟，变宽度）+ 场景 B（稳态，固定宽度反复 build）。
// 不做断言；目的是把数字打到 stdout 留作基线。
TEST(PreviewRenderBenchmark, BaselineReport_LargeDocument)
{
    constexpr int kTargetLines = 1000;
    QString doc = generateLargeMarkdown(kTargetLines);
    int actualLines = doc.count('\n') + 1;

    MarkdownParser parser;
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    QImage device(1600, 1200, QImage::Format_RGB32);

    PreviewLayout layout;
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&device);

    std::fprintf(stdout, "[PreviewRenderBenchmark] doc lines=%d, doc bytes=%lld\n",
                 actualLines, static_cast<long long>(doc.size()));

    // ---- Warmup ----
    constexpr int kWarmup = 5;
    layout.setViewportWidth(1200.0);
    for (int i = 0; i < kWarmup; ++i) {
        layout.buildFromAst(ast);
    }

    // ---- Scenario A: 拖拽模拟，宽度 800 → 1600 步进 20 ----
    {
        QVector<qint64> nss;
        nss.reserve(50);
        for (int w = 800; w <= 1600; w += 20) {
            layout.setViewportWidth(static_cast<qreal>(w));
            QElapsedTimer t;
            t.start();
            layout.buildFromAst(ast);
            nss.append(t.nsecsElapsed());
        }
        Stats s = summarize(nss);
        printStats("Scenario A (drag 800-1600 step 20)", s);
    }

    // ---- Scenario B: 稳态，固定 1200 反复 50 次 ----
    {
        layout.setViewportWidth(1200.0);
        QVector<qint64> nss;
        nss.reserve(50);
        for (int i = 0; i < 50; ++i) {
            QElapsedTimer t;
            t.start();
            layout.buildFromAst(ast);
            nss.append(t.nsecsElapsed());
        }
        Stats s = summarize(nss);
        printStats("Scenario B (fixed 1200, 50x)", s);
    }

    // ---- Scenario C: wordWrap 切换不参与（PreviewLayout 没暴露此开关；
    //                 wordWrap 影响在 PreviewWidget 层，本基线只测 layout）

    SUCCEED();
}

// [plan A1 Step 3] 视口剪裁路径基线：模拟"打开文档时仅显示首屏"，验证 buildFromAst
// 在 setViewportYRange(0, 800) 下的 ROI——预期视口内仅 layoutBlock 少量块，其余走 quickEstimateHeight
TEST(PreviewRenderBenchmark, BaselineReport_ViewportCropped)
{
    const int kSizes[] = {1000, 5000, 10000, 20000};
    QImage device(1600, 1200, QImage::Format_RGB32);
    QFont base("Segoe UI", 12);

    for (int targetLines : kSizes) {
        QString doc = generateLargeMarkdown(targetLines);
        int actualLines = doc.count('\n') + 1;

        MarkdownParser parser;
        auto astUnique = parser.parse(doc);
        ASSERT_NE(astUnique, nullptr);
        std::shared_ptr<AstNode> ast(std::move(astUnique));

        PreviewLayout layout;
        layout.setFont(base);
        layout.updateMetrics(&device);
        layout.setViewportWidth(1200.0);
        // 模拟首屏视口 = 800px（约 1 屏）+ ±2 屏 buffer → 把视口设为 [0, 2400]
        // widget 端会按视口高度 + 2*viewport 算 buffer，这里直接给等价值
        layout.setViewportYRange(0.0, 2400.0);

        std::fprintf(stdout,
                     "\n[PreviewRenderBenchmark::Cropped] doc lines=%d (target %d), viewport=[0,2400]\n",
                     actualLines, targetLines);
        std::fflush(stdout);

        constexpr int kWarmup = 3;
        for (int i = 0; i < kWarmup; ++i) {
            layout.buildFromAst(ast);
        }

        // 稳态：固定视口，30 次
        {
            QVector<qint64> nss;
            nss.reserve(30);
            for (int i = 0; i < 30; ++i) {
                QElapsedTimer t;
                t.start();
                layout.buildFromAst(ast);
                nss.append(t.nsecsElapsed());
            }
            Stats s = summarize(nss);
            char label[80];
            std::snprintf(label, sizeof(label), "%6d lines, viewport-crop steady, 30x", actualLines);
            printStats(label, s);
        }

        // 拖拽：宽度变化触发完整 rebuild（仍走视口剪裁）
        {
            QVector<qint64> nss;
            nss.reserve(21);
            for (int w = 800; w <= 1600; w += 40) {
                layout.setViewportWidth(static_cast<qreal>(w));
                QElapsedTimer t;
                t.start();
                layout.buildFromAst(ast);
                nss.append(t.nsecsElapsed());
            }
            Stats s = summarize(nss);
            char label[80];
            std::snprintf(label, sizeof(label), "%6d lines, viewport-crop drag, 21x", actualLines);
            printStats(label, s);
        }
    }
    SUCCEED();
}

// 基线报告：跨文档规模（1k / 5k / 10k / 20k 行）测 buildFromAst 耗时
// 目的：为 plan「2026-05-12-A1预览视口剪裁渲染」量化决策——
//   - 若 10k+ 行 P95 突破 16ms 单帧预算 → 视口剪裁有必要
//   - 若仍在预算内 → A1 可能不紧急，与 A2 增量解析比较 ROI
// 与 BaselineReport_LargeDocument 共享 Stats / printStats / generateLargeMarkdown，
// 避免「同一语义两份独立代码」反模式（CLAUDE.md 反模式 B）
TEST(PreviewRenderBenchmark, BaselineReport_ScalingDocument)
{
    const int kSizes[] = {1000, 5000, 10000, 20000};
    QImage device(1600, 1200, QImage::Format_RGB32);
    QFont base("Segoe UI", 12);

    for (int targetLines : kSizes) {
        QString doc = generateLargeMarkdown(targetLines);
        int actualLines = doc.count('\n') + 1;

        MarkdownParser parser;
        auto astUnique = parser.parse(doc);
        ASSERT_NE(astUnique, nullptr);
        std::shared_ptr<AstNode> ast(std::move(astUnique));

        PreviewLayout layout;
        layout.setFont(base);
        layout.updateMetrics(&device);
        layout.setViewportWidth(1200.0);

        std::fprintf(stdout,
                     "\n[PreviewRenderBenchmark::Scaling] doc lines=%d (target %d), bytes=%lld\n",
                     actualLines, targetLines, static_cast<long long>(doc.size()));
        std::fflush(stdout);

        // Warmup
        constexpr int kWarmup = 3;
        for (int i = 0; i < kWarmup; ++i) {
            layout.buildFromAst(ast);
        }

        // 稳态：固定 1200 宽度反复 30 次
        {
            QVector<qint64> nss;
            nss.reserve(30);
            for (int i = 0; i < 30; ++i) {
                QElapsedTimer t;
                t.start();
                layout.buildFromAst(ast);
                nss.append(t.nsecsElapsed());
            }
            Stats s = summarize(nss);
            char label[64];
            std::snprintf(label, sizeof(label), "%6d lines, steady 1200, 30x", actualLines);
            printStats(label, s);
        }

        // 拖拽：宽度 800→1600 step 40（21 个采样点，模拟 splitter 拖拽）
        {
            QVector<qint64> nss;
            nss.reserve(21);
            for (int w = 800; w <= 1600; w += 40) {
                layout.setViewportWidth(static_cast<qreal>(w));
                QElapsedTimer t;
                t.start();
                layout.buildFromAst(ast);
                nss.append(t.nsecsElapsed());
            }
            Stats s = summarize(nss);
            char label[64];
            std::snprintf(label, sizeof(label), "%6d lines, drag 800-1600 step 40", actualLines);
            printStats(label, s);
        }
    }

    SUCCEED();
}

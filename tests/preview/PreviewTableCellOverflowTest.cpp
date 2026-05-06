// tests/preview/PreviewTableCellOverflowTest.cpp
//
// Spec: specs/模块-preview/02-布局引擎.md  表格 layout
//       specs/模块-preview/03-绘制管线.md  paintInlineRuns
// Bug: plans/2026-05-06-表格单元格内容超出与下方重合.md
//
// 验收：
//   T-TABLE-CELL-NO-OVERFLOW   遍历多个临界宽度，cell.bounds.height 必须
//                              容纳 paint 端逐字符换行后的实际占用高度
//
// 复现方法：
//   - 构造一个表格 markdown，cell 内容含中英文混排 + 长单词
//   - 对每个宽度 W ∈ [200, 800] 步进 1px，跑 buildFromAst
//   - 对每个 cell，用 paint 端**完全一致**的逐字符换行算法计算 actualLines
//   - 用 estimateParagraphHeight 算法（layout 用的）计算 estimateLines
//   - 如果 actualLines > estimateLines 即可复现 bug
//   - 进一步断言 cell.bounds.height >= 实际渲染所需高度

#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QtMath>
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
            static char arg0[] = "PreviewTableCellOverflowTest";
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

// 模拟 PreviewPainter::paintInlineRuns 的逐字符换行算法（line 656-703），
// 返回该 inlineRuns 在给定 maxWidth 下渲染需要多少行。
// 与 estimate 不同的是：这是字符级真实换行（与 paint 一致），不是 ceil(总宽/行宽)。
int simulatePaintLines(const std::vector<InlineRun>& runs, qreal maxWidth, QPaintDevice* device)
{
    if (runs.empty()) return 1;
    qreal x = 0;        // 行起点
    qreal curX = 0;     // 当前光标
    int lines = 1;      // 至少一行

    auto findFitCount = [&](const QFontMetricsF& fm, const QString& str, qreal remaining) -> int {
        int fit = 0;
        qreal accW = 0;
        for (int i = 0; i < str.length(); ++i) {
            accW += fm.horizontalAdvance(str[i]);
            if (accW > remaining) break;
            fit = i + 1;
        }
        return fit;
    };

    for (const auto& run : runs) {
        if (run.text == "\n") {
            curX = x;
            ++lines;
            continue;
        }
        QFontMetricsF fm(run.font, device);
        QString text = run.text;
        while (!text.isEmpty()) {
            qreal fullWidth = fm.horizontalAdvance(text);
            qreal remaining = x + maxWidth - curX;
            if (fullWidth <= remaining || curX <= x) {
                if (fullWidth > remaining && curX <= x) {
                    int fitCount = findFitCount(fm, text, remaining);
                    if (fitCount <= 0) fitCount = 1;
                    text = text.mid(fitCount);
                    curX = x;
                    ++lines;
                    continue;
                }
                curX += fullWidth;
                break;
            }
            int wrapAt = findFitCount(fm, text, remaining);
            if (wrapAt <= 0) {
                curX = x;
                ++lines;
                continue;
            }
            text = text.mid(wrapAt);
            curX = x;
            ++lines;
        }
    }
    return lines;
}

// 收集所有 TableCell block（递归）
void collectTableCells(const LayoutBlock& root, std::vector<const LayoutBlock*>& out)
{
    if (root.type == LayoutBlock::TableCell) {
        out.push_back(&root);
    }
    for (const auto& c : root.children) {
        collectTableCells(c, out);
    }
}

}  // namespace

// 扫描多个宽度，断言每个 cell 的 bounds.height 能容纳 paint 端字符级换行的实际行数。
// 容差：用同 device 度量 cell 的 lineHeight（与 paint 端一致），允许 1px 浮点容差。
//
// 失败时输出能复现的最小临界宽度，便于调试。
TEST(PreviewTableCellOverflowTest, T_TABLE_CELL_NO_OVERFLOW)
{
    // 构造表格内容：刻意混合中英文 + 长 URL（不可断词）+ 加粗
    QString doc =
        "| 列A 列名 | 列B 描述 | 列C 链接 |\n"
        "|---------|---------|---------|\n"
        "| 苹果 apple | 这是一个**较长**的描述，用来测试换行行为 abc def ghi | https://example.com/some/very/long/path/here.html |\n"
        "| 香蕉 banana | 中文 mixed with **English bold** text and 长单词 supercalifragilisticexpialidocious | http://test.org/foo |\n"
        "| 樱桃 cherry | short | a |\n";

    MarkdownParser parser;
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    QImage device(1600, 1200, QImage::Format_RGB32);

    PreviewLayout layout;
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&device);

    // 收集失败 case，最后统一报告（便于看完整 bug 谱）
    struct FailCase {
        int width;
        int cellIdx;
        int actualLines;
        qreal cellHeight;
        qreal requiredHeight;
        QString sample;
    };
    std::vector<FailCase> failures;

    // 扫描 200..800 步进 1
    for (int W = 200; W <= 800; ++W) {
        layout.setViewportWidth(static_cast<qreal>(W));
        layout.buildFromAst(ast);

        std::vector<const LayoutBlock*> cells;
        collectTableCells(layout.rootBlock(), cells);

        for (size_t i = 0; i < cells.size(); ++i) {
            const LayoutBlock* cell = cells[i];
            if (cell->inlineRuns.empty()) continue;

            // paint 端可用宽度：cell.bounds.width - 8（左右各 4 padding）
            qreal paintWidth = cell->bounds.width() - 8.0;
            if (paintWidth <= 0) continue;

            int actualLines = simulatePaintLines(cell->inlineRuns, paintWidth, &device);

            // paint 端 lineHeight 计算（与 paintInlineRuns 一致）
            QFontMetricsF firstFm(cell->inlineRuns[0].font, &device);
            qreal lineSpacing = layout.lineSpacingFactor();
            qreal lineHeight = firstFm.height() * lineSpacing;
            qreal glyphHeight = firstFm.height();
            // paintInlineRuns 实际占用：(N-1) * lineHeight + glyphHeight
            // cell 上方 padding 4，所以 cell 必须 >= 4 + (N-1)*lineH + glyphHeight
            qreal requiredHeight = 4.0 + (actualLines - 1) * lineHeight + glyphHeight;

            // 容许 1px 浮点容差
            if (cell->bounds.height() + 1.0 < requiredHeight) {
                FailCase fc;
                fc.width = W;
                fc.cellIdx = static_cast<int>(i);
                fc.actualLines = actualLines;
                fc.cellHeight = cell->bounds.height();
                fc.requiredHeight = requiredHeight;
                // 取首 run 文本前 30 字符作样本
                QString sample = cell->inlineRuns[0].text.left(30);
                fc.sample = sample;
                failures.push_back(fc);
            }
        }
    }

    // 汇总输出（便于 bug 复现取证）
    if (!failures.empty()) {
        std::fprintf(stderr,
                     "[PreviewTableCellOverflowTest] %zu failures across widths.\n",
                     failures.size());
        // 只打印前 15 条避免日志过长
        size_t shown = qMin(failures.size(), static_cast<size_t>(15));
        for (size_t i = 0; i < shown; ++i) {
            const auto& f = failures[i];
            std::fprintf(stderr,
                         "  W=%d cell#%d  actualLines=%d  cellH=%.2f  required=%.2f  sample=\"%s\"\n",
                         f.width, f.cellIdx, f.actualLines, f.cellHeight, f.requiredHeight,
                         f.sample.toUtf8().constData());
        }
        std::fflush(stderr);
    }

    EXPECT_TRUE(failures.empty())
        << "Found " << failures.size() << " width(s) where table cell content overflows.";
}

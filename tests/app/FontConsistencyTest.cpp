// tests/app/FontConsistencyTest.cpp
//
// Spec: specs/横切关注点/80-字体系统.md
// 验收：
//   T-FONT-XHEIGHT  在 delta ∈ {-4, -2, 0, +2, +4} 下，
//                   |editor.xHeight - preview.xHeight| ≤ 1 px（INV-2）
//   T-FONT-MONO     monoFont.pointSize == baseFont.pointSize + kMonoDelta（INV-9）
//
// 说明：
//   Spec §4 接口段早就声明应有 FontConsistencyTest.cpp 兑现 INV-2，但实际不存在。
//   本测试同时承担两个责任：
//     1) 量化诊断（plans/2026-05-06-编辑区与预览区字号视觉不一致）
//        —— 输出每个 delta 下的实测 xHeight / pointSize / xHeight 差，作为"现象层"基线
//     2) 自动化保护（INV-2 / INV-10）—— 任何破坏视觉对齐的改动会被 CI 拦截

#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <cmath>
#include <cstdio>

#include "core/FontDefaults.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "FontConsistencyTest";
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

}  // namespace

// 在多个 delta 下断言 INV-2 四项约束（修订四，2026-05-11）：
//   xH ≤ 1px、capH ≤ 3px、height ≤ 8px、avgCharWidth ≤ 2px
// 阈值放宽理由：等宽字体（Courier New）vs 比例字体（Segoe UI）的 capH/height 系数
// 天生差异 → 强行追求 capH≤1 会让 editor.pt 推得远大于 preview（如 delta=0 旧算法选
// 15pt），导致 avgChar 差 36% 让用户主观感受"编辑器字号明显大"。新阈值容纳字体族
// 先天垂直差异，同时把 avgCharWidth 作为水平视觉密度的硬约束。
TEST(FontConsistencyTest, T_FONT_INV2_AllThreeMetricsAligned)
{
    QImage device(800, 600, QImage::Format_RGB32);

    std::fprintf(stdout,
                 "[FontConsistencyTest] INV-2 four-metric alignment "
                 "(device: 800x600, dpr=%.2f):\n",
                 device.devicePixelRatioF());
    std::fprintf(stdout,
                 "  delta | preview.pt | editor.pt | xH-diff | capH-diff | height-diff | avgChar-diff\n");

    int violations = 0;
    for (int delta : {-4, -2, 0, +2, +4}) {
        QFont previewFont = font_defaults::defaultPreviewFont(delta);
        QFont editorRaw = font_defaults::defaultEditorFont(delta);
        QFont editorBal = font_defaults::balanceEditorFontSize(editorRaw, previewFont, &device);

        QFontMetricsF prevFm(previewFont, &device);
        QFontMetricsF editFm(editorBal, &device);
        qreal xhDiff = std::abs(editFm.xHeight() - prevFm.xHeight());
        qreal capDiff = std::abs(editFm.capHeight() - prevFm.capHeight());
        qreal hDiff = std::abs(editFm.height() - prevFm.height());
        qreal avgDiff = std::abs(editFm.averageCharWidth() - prevFm.averageCharWidth());

        std::fprintf(stdout,
                     "  %5d | %10d | %9d | %7.3f | %9.3f | %11.3f | %12.3f\n",
                     delta, previewFont.pointSize(), editorBal.pointSize(),
                     xhDiff, capDiff, hDiff, avgDiff);

        // 修订五（2026-05-11 方向 A1）：same family same pt 下四项度量天然完全相等。
        // 阈值 0.5 留浮点余量；任何 family 不同退化（如未来又分等宽/比例）会立即触发
        // 这个测试失败，作为防御。
        EXPECT_LE(xhDiff, 0.5)
            << "INV-2 xH 违反 at delta=" << delta << ": diff=" << xhDiff
            << " — 检查 kEditorFontFamily 是否与 kPreviewFontFamily 相等";
        EXPECT_LE(capDiff, 0.5)
            << "INV-2 capH 违反 at delta=" << delta << ": diff=" << capDiff;
        EXPECT_LE(hDiff, 0.5)
            << "INV-2 height 违反 at delta=" << delta << ": diff=" << hDiff;
        EXPECT_LE(avgDiff, 0.5)
            << "INV-2 avgCharWidth 违反 at delta=" << delta << ": diff=" << avgDiff;
        if (xhDiff > 0.5 || capDiff > 0.5 || hDiff > 0.5 || avgDiff > 0.5) ++violations;
    }
    std::fflush(stdout);

    if (violations > 0) {
        ADD_FAILURE() << "Total violations: " << violations
                      << " (expected 0 to satisfy INV-2 four-metric)";
    }
}

// INV-9：monoFont 的 pointSize 必须 = baseFont.pointSize + kMonoDelta，
// 在所有 delta 下成立。
TEST(FontConsistencyTest, T_FONT_MONO_DerivedFromBase)
{
    for (int delta : {-4, -2, 0, +2, +4}) {
        QFont base = font_defaults::defaultPreviewFont(delta);
        QFont mono = font_defaults::defaultMonoFont(delta);
        int expectedMonoPt = base.pointSize() + font_defaults::kMonoDelta;
        EXPECT_EQ(mono.pointSize(), expectedMonoPt)
            << "INV-9 violated at delta=" << delta
            << ": mono.pt=" << mono.pointSize()
            << " expected=" << expectedMonoPt
            << " (base.pt=" << base.pointSize()
            << ", kMonoDelta=" << font_defaults::kMonoDelta << ")";
    }
}

// 完整字体度量对比：xHeight 之外，看 ascent/descent/height/capHeight 是否也接近。
// 用户主观感觉"字号不一致"很可能不是 xHeight 引起的——例如行高（QFontMetricsF::height）
// 在 Courier New vs Segoe UI 同 xHeight 下可能差 5-15%，导致每行整体高度有视觉差。
TEST(FontConsistencyTest, T_FONT_DiagnosticAllMetrics)
{
    QImage device(800, 600, QImage::Format_RGB32);

    std::fprintf(stdout,
                 "\n[FontConsistencyTest] full metrics comparison:\n"
                 "  delta | side    | pt | xH    | ascent | descent | height | capH  | leading | avgChar\n");

    auto dump = [&](int delta, const char* side, const QFont& f) {
        QFontMetricsF fm(f, &device);
        std::fprintf(stdout,
                     "  %5d | %-7s | %2d | %5.2f | %6.2f | %7.2f | %6.2f | %5.2f | %7.2f | %7.2f\n",
                     delta, side, f.pointSize(),
                     fm.xHeight(), fm.ascent(), fm.descent(),
                     fm.height(), fm.capHeight(), fm.leading(),
                     fm.averageCharWidth());
    };

    for (int delta : {-4, -2, 0, +2, +4}) {
        QFont preview = font_defaults::defaultPreviewFont(delta);
        QFont editorRaw = font_defaults::defaultEditorFont(delta);
        QFont editorBal = font_defaults::balanceEditorFontSize(editorRaw, preview, &device);
        dump(delta, "preview", preview);
        dump(delta, "editor",  editorBal);
    }

    std::fprintf(stdout,
                 "\n[FontConsistencyTest] diagnostic: 即使 xHeight 严格相等，"
                 "若 height (=ascent+descent+leading) 差异 > 1px，编辑器与预览的"
                 "**行整体高度**仍会让用户感受到不对齐。这是 Spec INV-2 当前未保护的盲点。\n");
    std::fflush(stdout);
    SUCCEED();
}

// 候选搜索：[base-3, base+3] 范围内，对 delta=0，列出每个 editor pointSize 的全部度量，
// 帮助评估「除 xHeight 之外，是否存在某个 pointSize 让 capHeight/height 也更接近 preview」
TEST(FontConsistencyTest, T_FONT_CandidateSearch_Delta0)
{
    QImage device(800, 600, QImage::Format_RGB32);
    QFont preview = font_defaults::defaultPreviewFont(0);
    QFontMetricsF prevFm(preview, &device);

    std::fprintf(stdout,
                 "\n[FontConsistencyTest] candidate search at delta=0 (preview.pt=%d):\n"
                 "  preview reference: xH=%.2f capH=%.2f height=%.2f ascent=%.2f\n"
                 "  editor.pt | xH | capH | height | ascent | xH-diff | capH-diff | height-diff\n",
                 preview.pointSize(),
                 prevFm.xHeight(), prevFm.capHeight(),
                 prevFm.height(), prevFm.ascent());

    int basePt = preview.pointSize();
    for (int pt = basePt - 3; pt <= basePt + 3; ++pt) {
        QFont trial = font_defaults::defaultEditorFont(0);
        trial.setPointSize(pt);
        QFontMetricsF fm(trial, &device);
        std::fprintf(stdout,
                     "  %9d | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f\n",
                     pt, fm.xHeight(), fm.capHeight(), fm.height(), fm.ascent(),
                     std::abs(fm.xHeight() - prevFm.xHeight()),
                     std::abs(fm.capHeight() - prevFm.capHeight()),
                     std::abs(fm.height() - prevFm.height()));
    }
    std::fflush(stdout);
    SUCCEED();
}

// 字体族实测对比：在 delta=0 下，把候选等宽字体族（Courier New / Consolas / Cascadia
// Mono / Cascadia Code）与 Segoe UI 12pt 同 pt 对照，看哪个 family 同 pt 下四项度量
// （xH / capH / height / avgChar）综合最接近——这是方向 G（spec 80）的实测依据。
//
// 若某 family 同 pt 下四项都接近预览，意味着我们可以用 same-pt 简单设置，不需要
// balanceEditorFontSize 的 ±3pt 搜索 + score 加权（前几轮一直绕不开字体族先天差异
// 的根源问题）。
TEST(FontConsistencyTest, T_FONT_FamilyComparison_SamePt)
{
    QImage device(800, 600, QImage::Format_RGB32);
    const int pt = font_defaults::kDefaultBaseFontSizePt;  // 12

    QFont preview(font_defaults::kPreviewFontFamily, pt);
    QFontMetricsF prevFm(preview, &device);

    std::fprintf(stdout,
                 "\n[FontConsistencyTest] family comparison at same pt=%d:\n"
                 "  preview ref (%s): xH=%.2f capH=%.2f height=%.2f avgChar=%.2f\n"
                 "  family             | xH    | capH  | height | avgChar | xH-Δ | capH-Δ | h-Δ  | avg-Δ\n",
                 pt, font_defaults::kPreviewFontFamily,
                 prevFm.xHeight(), prevFm.capHeight(),
                 prevFm.height(), prevFm.averageCharWidth());

    auto dump = [&](const char* family) {
        QFont f(family, pt);
        f.setStyleHint(QFont::Monospace);
        QFontMetricsF fm(f, &device);
        std::fprintf(stdout,
                     "  %-18s | %5.2f | %5.2f | %6.2f | %7.2f | %4.2f | %6.2f | %4.2f | %5.2f\n",
                     family,
                     fm.xHeight(), fm.capHeight(), fm.height(), fm.averageCharWidth(),
                     std::abs(fm.xHeight() - prevFm.xHeight()),
                     std::abs(fm.capHeight() - prevFm.capHeight()),
                     std::abs(fm.height() - prevFm.height()),
                     std::abs(fm.averageCharWidth() - prevFm.averageCharWidth()));
    };

    // 候选清单（按优先级）：
    // - Cascadia Mono / Cascadia Code: Microsoft 2019 设计，与 Segoe UI 同团队，理论
    //   度量最接近 Segoe UI
    // - Consolas: Vista 起的等宽字体，比 Courier New 现代
    // - Courier New: 当前默认（IBM Selectric 时代），最远
    // - Lucida Console: Win 自带
    dump("Cascadia Mono");
    dump("Cascadia Code");
    dump("Consolas");
    dump("Courier New");
    dump("Lucida Console");

    std::fprintf(stdout,
                 "\n[Family comparison] 字体族在系统不可用时 Qt 会 fallback 到 styleHint 匹配；\n"
                 "若某 family 的 avgChar/capH/height 与 preview 全部 ≤ 2px，可考虑作为\n"
                 "kEditorFontFamily 取代 Courier New。\n");
    std::fflush(stdout);
    SUCCEED();
}

// 退化路径：device 为 null 时 balanceEditorFontSize 不应改字号
TEST(FontConsistencyTest, T_FONT_BALANCE_NullDeviceFallback)
{
    QFont previewFont = font_defaults::defaultPreviewFont(0);
    QFont editorRaw = font_defaults::defaultEditorFont(0);
    QFont editorBal = font_defaults::balanceEditorFontSize(editorRaw, previewFont, nullptr);
    EXPECT_EQ(editorBal.pointSize(), editorRaw.pointSize())
        << "device=null 时 balanceEditorFontSize 应原样返回，不可改字号";
}

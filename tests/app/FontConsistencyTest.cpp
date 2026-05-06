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

// 在多个 delta 下断言 xHeight 差 ≤ 1px；同时打印基线表格便于诊断字号一致性问题。
TEST(FontConsistencyTest, T_FONT_XHEIGHT_DiffWithinOnePixel)
{
    QImage device(800, 600, QImage::Format_RGB32);

    std::fprintf(stdout,
                 "[FontConsistencyTest] xHeight measurement table "
                 "(device: 800x600, dpr=%.2f):\n",
                 device.devicePixelRatioF());
    std::fprintf(stdout,
                 "  delta | preview.pt | editor.pt(raw) | editor.pt(balanced) | "
                 "preview.xH | editor.xH | |diff|\n");

    int violations = 0;
    for (int delta : {-4, -2, 0, +2, +4}) {
        QFont previewFont = font_defaults::defaultPreviewFont(delta);
        QFont editorRaw = font_defaults::defaultEditorFont(delta);
        QFont editorBal = font_defaults::balanceEditorFontSize(editorRaw, previewFont, &device);

        QFontMetricsF prevFm(previewFont, &device);
        QFontMetricsF editFm(editorBal, &device);
        qreal prevX = prevFm.xHeight();
        qreal editX = editFm.xHeight();
        qreal diff = std::abs(editX - prevX);

        std::fprintf(stdout,
                     "  %5d | %10d | %14d | %19d | %10.3f | %9.3f | %.3f\n",
                     delta,
                     previewFont.pointSize(),
                     editorRaw.pointSize(),
                     editorBal.pointSize(),
                     prevX,
                     editX,
                     diff);

        EXPECT_LE(diff, 1.0)
            << "INV-2 violated at delta=" << delta
            << ": |editor.xHeight - preview.xHeight| = " << diff << "px > 1px";
        if (diff > 1.0) ++violations;
    }
    std::fflush(stdout);

    if (violations > 0) {
        ADD_FAILURE() << "Total violations: " << violations
                      << " (expected 0 to satisfy INV-2)";
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

// 退化路径：device 为 null 时 balanceEditorFontSize 不应改字号
TEST(FontConsistencyTest, T_FONT_BALANCE_NullDeviceFallback)
{
    QFont previewFont = font_defaults::defaultPreviewFont(0);
    QFont editorRaw = font_defaults::defaultEditorFont(0);
    QFont editorBal = font_defaults::balanceEditorFontSize(editorRaw, previewFont, nullptr);
    EXPECT_EQ(editorBal.pointSize(), editorRaw.pointSize())
        << "device=null 时 balanceEditorFontSize 应原样返回，不可改字号";
}

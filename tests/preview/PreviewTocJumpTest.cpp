// tests/preview/PreviewTocJumpTest.cpp
//
// Spec: specs/模块-preview/07-TOC面板.md INV-TOC-JUMP-CORRECT
//       specs/模块-preview/02-布局引擎.md（视口剪裁 / sourceLineToY）
// Bug:  plans/2026-06-15-渲染区目录点击章节跳转位置错误.md
//
// 不变量：TOC 点击跳转后，自校正循环必须收敛到「滚动位置 == 目标章节在当前 layout
//   下的 sourceLineToY」的不动点。绘制与滚动用同一 layout 状态，故该不动点 ⇒ 目标
//   标题落在视口顶部（far-above 块的估算误差对 scroll 与 paint 等量作用，不影响相对落位）。
//
// 验收：
//   T-TOC-JUMP-1  大文档跳转远处章节：模拟 PreviewWidget::onScrollAnimationFinished 的
//                 自校正循环，最终 |sourceLineToY(target) - scrollY| < 3（一致 = 落位准确）。
//   T-TOC-JUMP-2  对比：不做校正（停在初始估算 Y）时不一致量显著 > 校正后（证明校正有效）。

#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QImage>
#include <cmath>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"
#include "FontDefaults.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewTocJumpTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
private:
    QApplication* app_ = nullptr;
};
::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

// 生成含 N 个章节的大文档（每章一个 H2 + 若干段落），返回 markdown 及目标章节的源行号
QString makeBigDoc(int sections, int* targetLineOut)
{
    QString out;
    int line = 0;
    int targetLine = -1;
    for (int s = 0; s < sections; ++s) {
        if (s == sections - 1) targetLine = line;  // 最后一章作为"远处目标"
        out += QString("## 章节 %1 Section heading\n\n").arg(s);
        line += 2;
        for (int p = 0; p < 4; ++p) {
            out += QString("这是第 %1 章的第 %2 段，包含中英文混排 some prose content "
                           "用于撑高文档高度让远处章节落在视口外触发估算路径。\n\n").arg(s).arg(p);
            line += 2;
        }
    }
    if (targetLineOut) *targetLineOut = targetLine;
    return out;
}

// 模拟 PreviewWidget 的视口剪裁 rebuild：buffer = 2*vpH，range = [scrollY-buf, scrollY+vpH+buf]
qreal rebuildAndGetTargetY(PreviewLayout& layout, const std::shared_ptr<AstNode>& ast,
                           qreal scrollY, qreal vpH, int targetLine)
{
    qreal buffer = vpH * 2.0;
    layout.setViewportYRange(scrollY - buffer, scrollY + vpH + buffer);
    layout.buildFromAst(ast);
    return layout.sourceLineToY(targetLine);
}

}  // namespace

// ---------------------------------------------------------------------------
// T-TOC-JUMP-1：自校正收敛到一致不动点（落位准确）
// ---------------------------------------------------------------------------
TEST(PreviewTocJumpTest, T_TOC_JUMP_1_SelfCorrectionConverges)
{
    int targetLine = -1;
    QString doc = makeBigDoc(60, &targetLine);
    ASSERT_GE(targetLine, 0);

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_TRUE(astU);
    std::shared_ptr<AstNode> ast(std::move(astU));

    const int W = 700;
    const qreal vpH = 600.0;
    QImage device(W, (int)vpH, QImage::Format_ARGB32);

    PreviewLayout layout;
    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);

    // 初始：视口在顶部，远处目标是 placeholder 粗估
    qreal scrollY = rebuildAndGetTargetY(layout, ast, 0.0, vpH, targetLine);

    // 模拟 onScrollAnimationFinished 的自校正循环（最多 4 次，偏差 < 3 收敛）
    int iters = 0;
    for (; iters < 4; ++iters) {
        qreal y = rebuildAndGetTargetY(layout, ast, scrollY, vpH, targetLine);
        if (std::abs(y - scrollY) < 3.0) break;
        scrollY = y;
    }

    // 校正后：当前 layout 状态下 sourceLineToY(target) 必须与 scrollY 一致（不动点）
    qreal finalY = rebuildAndGetTargetY(layout, ast, scrollY, vpH, targetLine);
    qreal residual = std::abs(finalY - scrollY);
    fprintf(stderr, "[toc-jump] iters=%d scrollY=%.1f finalY=%.1f residual=%.2f\n",
            iters, scrollY, finalY, residual);
    EXPECT_LT(residual, 3.0) << "自校正未收敛到一致不动点，目标标题不会落在视口顶部";
}

// ---------------------------------------------------------------------------
// T-TOC-JUMP-2：校正后一致性显著优于"停在初始估算"（证明修复有效）
// ---------------------------------------------------------------------------
TEST(PreviewTocJumpTest, T_TOC_JUMP_2_CorrectionBeatsEstimate)
{
    int targetLine = -1;
    QString doc = makeBigDoc(60, &targetLine);
    ASSERT_GE(targetLine, 0);

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_TRUE(astU);
    std::shared_ptr<AstNode> ast(std::move(astU));

    const int W = 700;
    const qreal vpH = 600.0;
    QImage device(W, (int)vpH, QImage::Format_ARGB32);

    PreviewLayout layout;
    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);

    // 不校正：停在初始估算 Y，落点处实际 sourceLineToY 与之的偏差
    qreal estY = rebuildAndGetTargetY(layout, ast, 0.0, vpH, targetLine);
    qreal actualAtEst = rebuildAndGetTargetY(layout, ast, estY, vpH, targetLine);
    qreal errNoCorrect = std::abs(actualAtEst - estY);

    // 校正：迭代到不动点
    qreal scrollY = estY;
    for (int i = 0; i < 4; ++i) {
        qreal y = rebuildAndGetTargetY(layout, ast, scrollY, vpH, targetLine);
        if (std::abs(y - scrollY) < 3.0) break;
        scrollY = y;
    }
    qreal finalY = rebuildAndGetTargetY(layout, ast, scrollY, vpH, targetLine);
    qreal errCorrect = std::abs(finalY - scrollY);

    fprintf(stderr, "[toc-jump] errNoCorrect=%.1f errCorrect=%.2f\n", errNoCorrect, errCorrect);
    EXPECT_LT(errCorrect, errNoCorrect);   // 校正后更一致
    EXPECT_LT(errCorrect, 3.0);
}

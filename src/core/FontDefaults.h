//
// Spec: specs/横切关注点/80-字体系统.md
// Invariants enforced here: INV-1 (single source of truth), INV-2/6 (visual alignment),
//                           INV-9 (mono delta), INV-10 (balance editor font size)
//
// 默认字号/字体族的唯一数据源。禁止在任何其他文件中硬编码字号数字（除 < 10 的
// 局部小字号如按钮次要文字，且必须加注释说明）。
//

#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPaintDevice>
#include <cmath>
#include <limits>

namespace font_defaults {

// 基础字号（预览正文字号；编辑器字号由 balanceEditorFontSize 动态补偿）
constexpr int kDefaultBaseFontSizePt = 12;

// 等宽字号（代码块、内联代码默认值）
// 2026-04-14: 对齐编辑器代码块字号——预览代码块应与编辑器内的代码内容视觉等价
// （用户在编辑器写代码，切到预览应该等价呈现，不该明显缩小）
constexpr int kMonoFontSizePt = 12;

// 等宽字号相对基础字号的派生差值（INV-9）
// = 0 意味着预览代码块字号 = 预览正文字号 = 编辑器字号（xHeight 对齐后）
constexpr int kMonoDelta = kMonoFontSizePt - kDefaultBaseFontSizePt;  // = 0

// 字体族
// 2026-05-11 修订五（方向 A1）：kEditorFontFamily 从 "Courier New"（等宽，
// 打字机感）改为 "Segoe UI"（比例字体）——与预览同族。
// 历史教训：等宽 vs 比例的先天度量差异（capH/height 矮 18%~21%，avgChar 宽 36%）
// 无论如何调 pt 都会留下"水平 vs 垂直"二选一的差异，用户连续 5 轮反馈"字号
// 不一致"。同族同 pt 直接让 xH/capH/height/avgChar 全部相等，**根治**字号不一致
// 问题。
// 代码块和内联代码仍用 kMonoFontFamily 等宽渲染——代码字符对齐是必须的；
// 但正文（编辑器和预览）使用同一比例字体，与现代 Markdown 编辑器（Typora、
// Obsidian）惯例对齐。
constexpr const char* kEditorFontFamily = "Segoe UI";
constexpr const char* kPreviewFontFamily = "Segoe UI";
constexpr const char* kMonoFontFamily = "Consolas";

// 编辑器默认字体（INV-3）
// 注意：此函数返回的 pointSize 是 base + delta；MainWindow::applyFontSize 仍会调用
// balanceEditorFontSize 做视觉补偿（INV-10）——同 family 同 pt 下 balance 自然选
// same pt（所有度量 diff 都为 0），算法保留作为防御（万一未来又改 family 不一致）。
inline QFont defaultEditorFont(int sizeDelta = 0)
{
    QFont font(kEditorFontFamily, kDefaultBaseFontSizePt + sizeDelta);
    return font;
}

// 预览默认字体（基础正文字体）
inline QFont defaultPreviewFont(int sizeDelta = 0)
{
    return QFont(kPreviewFontFamily, kDefaultBaseFontSizePt + sizeDelta);
}

// 预览代码块/内联代码等宽字体（INV-9：必须随 sizeDelta 缩放）
inline QFont defaultMonoFont(int sizeDelta = 0)
{
    QFont font(kMonoFontFamily, kDefaultBaseFontSizePt + sizeDelta + kMonoDelta);
    font.setStyleHint(QFont::Monospace);
    return font;
}

// 视觉对齐：调整编辑器字号让多项度量综合接近预览字体（INV-2 / INV-10 修订四）
//
// 算法（2026-05-11 修订四：新增 avgCharWidth 主导项）：
//   在 [previewFont.pointSize() - 3, previewFont.pointSize() + 3] 窗口内（共 7 个候选）
//   对每个候选 pointSize，计算 4 项度量的加权综合 score：
//     score = 2.0 * |editor.avgCharWidth - preview.avgCharWidth|   ← 主导：水平字符密度
//           + 1.0 * |editor.xHeight      - preview.xHeight|
//           + 1.0 * |editor.capHeight    - preview.capHeight|
//           + 0.3 * |editor.height       - preview.height|
//
// **avgCharWidth 主导的理由**（2026-05-11 用户第四次反馈 + FontConsistencyTest 实测）：
//   前几轮算法（仅 xH/capH/height）在 delta=0 下选 editor=15pt：
//     - capH-diff=0.28 ✓ height-diff=1 ✓（"垂直度量"对齐）
//     - avgChar-diff=4（Courier New 15pt 字符宽 15px vs Segoe UI 12pt 字符宽 11px，36%）
//   等宽 Courier New 必须每个字符等宽 → 平均宽度天生大于比例 Segoe UI；强行让 capH
//   接近就要把 pt 推大很多 → 用户主观感受"编辑器字号明显大"——这是用户连续 4 轮报告
//   "字号还是不一致"的真正根因。
//
//   人眼对"字号一致"的判断综合了水平字符密度（行宽 / 字符间距）+ 垂直高度（行高 / 大写）。
//   旧算法只优化垂直，水平差异被忽视 → 视觉失衡。
//
//   修订四把 avgCharWidth 加入 score 并设最高权重 2.0：
//     12pt（same pt）: 2·1 + 1·0 + 1·2.58 + 0.3·6 = 6.38   ← 算法最优
//     13pt:           2·2 + 1·1 + 1·1.44 + 0.3·4 = 7.64
//     14pt:           2·3 + 1·1 + 1·0.86 + 0.3·3 = 8.76
//     15pt（旧算法选）: 2·4 + 1·1 + 1·0.28 + 0.3·1 = 9.58
//
//   12pt 胜出——avgChar-diff=1 与 xH-diff=0 都接近 0，capH/height 差异是字体族先天差异
//   （等宽 vs 比例），人眼可接受 18% 大写字母矮度差，远比 36% 字符宽度差更不显眼。
//
// filter 阈值同步放宽：原 capH≤1 / height≤3 在引入 avgChar 后不再适用——12pt 候选必然
// capH-diff≈2.58 / height-diff≈6 但视觉上反而最接近，放宽到 capH≤3 / height≤8 让综合
// 最优候选能通过 filter 而非走 fallback 全集路径。
//
// 若 device 为 null（如单元测试场景），退化到原 editor 字号不做补偿。
inline QFont balanceEditorFontSize(QFont editor, const QFont& preview, QPaintDevice* device)
{
    if (!device) {
        return editor;
    }
    QFontMetricsF prevFm(preview, device);
    const qreal targetXH = prevFm.xHeight();
    const qreal targetCapH = prevFm.capHeight();
    const qreal targetHeight = prevFm.height();
    const qreal targetAvgChar = prevFm.averageCharWidth();

    int basePt = preview.pointSize();
    if (basePt <= 0) {
        return editor;
    }

    // [Spec 80 INV-2 修订四 / 2026-05-11] filter 阈值放宽——容纳"字体族先天垂直差异"
    // 的同时让 avgChar 主导 score 选出 same-pt 类候选
    constexpr qreal kXhTolerance = 1.0;
    constexpr qreal kCapTolerance = 3.0;       // 修订四：1 → 3（字体族先天）
    constexpr qreal kHeightTolerance = 8.0;    // 修订四：3 → 8（字体族先天）
    constexpr qreal kAvgCharTolerance = 2.0;   // 修订四新增：水平字符密度约束

    int bestPtFiltered = -1;
    qreal bestScoreFiltered = std::numeric_limits<qreal>::max();
    int bestPtAny = editor.pointSize() > 0 ? editor.pointSize() : basePt;
    qreal bestScoreAny = std::numeric_limits<qreal>::max();

    for (int pt = basePt - 3; pt <= basePt + 3; ++pt) {
        if (pt <= 0) continue;
        QFont trial = editor;
        trial.setPointSize(pt);
        QFontMetricsF fm(trial, device);
        const qreal xhDiff = std::abs(fm.xHeight() - targetXH);
        const qreal capDiff = std::abs(fm.capHeight() - targetCapH);
        const qreal hDiff = std::abs(fm.height() - targetHeight);
        const qreal avgDiff = std::abs(fm.averageCharWidth() - targetAvgChar);
        // avgChar 权重 2.0 主导；xH/capH 各 1.0；height 0.3 仅作微调
        const qreal score = 2.0 * avgDiff + 1.0 * xhDiff + 1.0 * capDiff + 0.3 * hDiff;

        if (score < bestScoreAny) {
            bestScoreAny = score;
            bestPtAny = pt;
        }
        if (xhDiff <= kXhTolerance
            && capDiff <= kCapTolerance
            && hDiff <= kHeightTolerance
            && avgDiff <= kAvgCharTolerance) {
            if (score < bestScoreFiltered) {
                bestScoreFiltered = score;
                bestPtFiltered = pt;
            }
        }
    }

    int bestPt = (bestPtFiltered > 0) ? bestPtFiltered : bestPtAny;
    editor.setPointSize(bestPt);
    return editor;
}

} // namespace font_defaults

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
constexpr const char* kEditorFontFamily = "Courier New";
constexpr const char* kPreviewFontFamily = "Segoe UI";
constexpr const char* kMonoFontFamily = "Consolas";

// 编辑器默认字体（INV-3）
// 注意：此函数返回的 pointSize 是 base + delta；MainWindow::applyFontSize 必须
// 再调用 balanceEditorFontSize 做视觉补偿（INV-10）
inline QFont defaultEditorFont(int sizeDelta = 0)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(kDefaultBaseFontSizePt + sizeDelta);
    font.setStyleHint(QFont::Monospace);
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

// 视觉对齐：调整编辑器字号让多项度量综合接近预览字体（INV-2 / INV-10 修订）
//
// 算法（2026-05-06 plan #4 阶段 2 方向 A）：
//   在 [previewFont.pointSize() - 3, previewFont.pointSize() + 3] 窗口内（共 7 个候选）
//   对每个候选 pointSize，计算综合 score：
//     score = 1.0 * |editor.xHeight - preview.xHeight|
//           + 1.5 * |editor.capHeight - preview.capHeight|
//           + 0.5 * |editor.height - preview.height|
//   选 score 最小的 pointSize。capHeight 权重最高——大写字母决定主体视觉感受；
//   xHeight 是 INV-2 经典指标；height（含 leading）是行整体高度，权重最低。
//
// 历史：旧算法仅优化 xHeight，导致 Courier New（编辑器，等宽）vs Segoe UI（预览，
// 比例）在 xHeight 严格相等时 height 仍差 8px @ delta=0（FontConsistencyTest 实测），
// 用户主观感受"字号不一致"。新算法把 capHeight / height 一并纳入。
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

    int basePt = preview.pointSize();
    if (basePt <= 0) {
        // pointSize 未设置（用了 pointSizeF 或 pixelSize），fallback 不补偿
        return editor;
    }

    int bestPt = editor.pointSize() > 0 ? editor.pointSize() : basePt;
    qreal bestScore = std::numeric_limits<qreal>::max();

    for (int pt = basePt - 3; pt <= basePt + 3; ++pt) {
        if (pt <= 0) continue;
        QFont trial = editor;
        trial.setPointSize(pt);
        QFontMetricsF fm(trial, device);
        qreal score = 1.0 * std::abs(fm.xHeight() - targetXH)
                    + 1.5 * std::abs(fm.capHeight() - targetCapH)
                    + 0.5 * std::abs(fm.height() - targetHeight);
        if (score < bestScore) {
            bestScore = score;
            bestPt = pt;
        }
    }

    editor.setPointSize(bestPt);
    return editor;
}

} // namespace font_defaults

// src/preview/TextWrap.h
//
// Spec: specs/模块-preview/02-布局引擎.md（INV-WRAP-SHARED：布局估高与绘制换行共用同一套判定）
// Last synced: 2026-11-02
#pragma once

#include <QFontMetricsF>
#include <QString>

namespace textwrap {

// 判断一段（可能很长的）run 文本能否放进当前行剩余宽度，并给出用于后续比较的「整段宽度」。
//
// 旧实现每换一行都对「剩余全部文本」调用 horizontalAdvance(text)（整段整形，O(剩余长度)），
// 单个 100KB 段落共 ~1000 行 → O(n²)，实测 16KB 一行文本的一个 chunk 布局 ~350ms。
// 现在：文本超过 kProbeChars 时先只量前 kProbeChars 个字符——前缀已超过剩余宽度，整段必然放不下，
// 直接返回「放不下」；前缀没超才回落到整段测量。
// 布局估高（PreviewLayout::estimateParagraphHeight）与绘制（PreviewPainter::paintInlineRuns）
// 必须调用同一个函数，否则两份换行算法走样 → 表格越界（反模式 B）。
constexpr int kProbeChars = 2048;

inline bool fitsInLine(const QFontMetricsF& fm, const QString& text, qreal remaining, qreal* outWidth)
{
    if (text.length() > kProbeChars && fm.horizontalAdvance(text.left(kProbeChars)) > remaining) {
        *outWidth = remaining + 1.0;   // 仅用于「是否放得下」的比较，调用方不会拿它当真实宽度绘制
        return false;
    }
    *outWidth = fm.horizontalAdvance(text);
    return *outWidth <= remaining;
}

}  // namespace textwrap

---
title: 渲染区标题放大换行后与下方段落区域重叠
status: in_progress
created: 2026-04-27
related_specs: [specs/模块-preview/02-布局引擎.md, specs/模块-preview/03-绘制管线.md]
---

# Bug: 标题放大触发换行后，换行内容与下方段落重叠

## 复现
1. 打开任意含一级或二级标题的 markdown 文件
2. Ctrl+滚轮放大预览区字号（或调整字号缩放至触发标题文字换行）
3. 当标题文本超出可视宽度需要换行时，标题第二行的文字会与下方段落（或下一个块元素）的渲染区域**重叠**

## 期望
标题换行后，整个标题块的高度应正确反映多行文本的总高度（含行间距），下方段落顶部 y 坐标 = 标题块底部 y 坐标，不重叠。

## 怀疑根因（待 agent 调研后确认）
预览区自绘布局引擎中，标题块的 `bounds.height()` 可能仅按单行 `lineHeight` 计算，未考虑换行后实际占据的多行高度。
- 怀疑点：`PreviewLayout::layoutBlock` 计算 heading block bounds 时漏了 `wrapped_lines * lineHeight`
- 或 `QFontMetricsF::boundingRect(text, wrap)` 没传给度量
- 或 inline run 折行后没把额外行高累加到 block.height

## Spec 牵涉
- INV-2（preview 子块绝对坐标）：递归绘制时子块绝对坐标 = 父块绝对坐标 + child.bounds.x()/y()。如果 heading block bounds.height() 不准，下一个 block 的 absY 就错了
- 横切 40-高DPI适配：所有 fontMetrics 必须带 QPaintDevice* 参数

## 验收
- T-1 标题文本短不换行：渲染正常（回归保护）
- T-2 标题放大触发 1 次换行：标题块高度 ≈ 2 * lineHeight + paragraphSpacing；下方段落顶端不与标题第二行重叠
- T-3 标题放大触发 ≥ 2 次换行：高度按实际行数累计
- T-4 高 DPI（150%/200%）下行高/重叠正确
- T-5 不同字号缩放（Ctrl+滚轮各档）下都不重叠

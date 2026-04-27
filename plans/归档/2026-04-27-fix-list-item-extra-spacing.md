---
title: 预览区相邻列表项之间出现一行高度的多余空白
status: completed
created: 2026-04-27
completed: 2026-04-27
related_specs:
  - specs/模块-preview/02-布局引擎.md
  - specs/模块-preview/03-绘制管线.md
---

# 修复总结

**根因**：`PreviewLayout::estimateParagraphHeight` 把 N 行段落高度算为
`N * lineHeight (= fm.height() * 1.5)`，但 `paintInlineRuns` 实际占用是
`(N-1) * lineHeight + fm.height()`，最后一行画完即停不再 `curY += lineHeight`。
两者差 `0.5 * fm.height()` 即末行半行墨迹空白。

连续短列表项每项末尾都多 0.5 行空白，叠在一起视觉上形成"约一行高度"的间距。

`Item` 分支的 `qMax(y, m_lineHeight)` 同样把 ListItem 高度抬高到 `1.5x` 墨迹高，
共同作用导致 bug。

**修复**：
1. `estimateParagraphHeight` 公式改为 `(N-1) * lineHeight + glyphHeight`（与 paint 同构）
2. `Item` 分支的 height 下界保底改为 `baseFm.height()`（墨迹高度），不再用 `m_lineHeight`
3. Spec 新增 INV-11 描述末行不附 0.5 行空白的契约

测试：`tests/preview/PreviewLayoutListSpacingTest.cpp` (T1-T3)
更新：`PreviewLayoutHeadingWrapTest` 的下界公式同步到新的高度定义。

# Bug: 列表项之间多余空白

## 复现内容
在编辑器中输入：

```
- **DrawBoard**：基于 Qt Graphics Framework 的渲染和交互层
- **VirtualCanvas**：平台无关的画布数据模型和序列化层
```

## 现象
预览区渲染后，两个相邻列表项之间多出**约一行的空白**（远超正常的项间距）。视觉上像是每个列表项都有一个空尾行被渲染出来。

## 期望
相邻列表项之间应只有正常的小间距（与编辑器视觉一致），不应出现空一行。

## 怀疑根因（待 agent 验证）
- **怀疑 1**：刚修复的标题换行 bug（INV-10：layout 行高与 paintInlineRuns 同构）类似——list item 的 layout 高度估算与 paint 实际占用不一致，多估了一行
- **怀疑 2**：list item 末尾若有 inline runs 中尾随空白/换行符 token，被算作额外一行
- **怀疑 3**：粗体（`**...**`）emphasis 切换 inline font 时被错误折行
- **怀疑 4**：list item 间距常量（paragraphSpacing）设置过大；或 list item bounds 包含错误的 bottom margin

## 验收
- T-1 简单两项无粗体列表 → 项间距 ≈ 单倍 lineHeight 的 0.3~0.5 倍
- T-2 含 `**bold**` 的两项列表（用户复现样本）→ 项间距与无粗体的一致
- T-3 项内容超过单行换行 → 项内行高与项间距分别正确
- T-4 嵌套列表（缩进项）→ 子项间距同样合理

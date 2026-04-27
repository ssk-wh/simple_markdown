---
title: 列表项间距修复有时序漏洞——首次渲染仍有空白，滚轮缩放后消失
status: completed
created: 2026-04-27
completed: 2026-04-27
related_specs:
  - specs/模块-preview/02-布局引擎.md
related_plans:
  - plans/归档/2026-04-27-fix-list-item-extra-spacing.md
---

## 实际根因（不在原本怀疑列表中）

`PreviewLayout::cachedFontMetrics` 用 `qHash(QFont)` 作 cache key。
Qt 5.12 的 `qHash(QFont)` 对"同 family、同 weight、同 italic、不同 pointSize"
的字体返回相同 hash 值（实测 SimSun bold 16.2pt 与 SimSun bold 9pt 都得到
key=2476512596）。当文档同时存在 H1（1.8x base）和带粗体的列表项段落时，
H1 的 `QFontMetricsF` 先进入缓存，列表项 bold 段落随后查询命中 H1 项，
glyphHeight 被错读成 H1 高度（27 而非 15），`estimateParagraphHeight`
把单行段落估为多行，`ListItem.height` 多出近一行墨迹高。

缩放触发 `setFont(newPointSize)`，新字号 hash 不再撞车 → 重新插入正确度量 →
症状消失。这就是"缩放一次空白消失"的原因。文件打开样本（无 H1）不触发，
新建未命名 tab（默认模板含 H1）触发，与用户复现完全吻合。

## 修复

`src/preview/PreviewLayout.{h,cpp}`：cache key 改为
`(family, pointSizeF, weight, italic, stretch, styleStrategy)` 元组拼成的
`std::string`（Qt 5.12 不为 QString 提供 std::hash 特化）。

`specs/模块-preview/02-布局引擎.md` 增补 `[INV-12] 字体度量缓存 key 必须
包含所有影响度量的字体属性`。

## 回归测试

`tests/preview/PreviewLayoutListSpacingTest.cpp::T4_HeadingDoesNotPolluteListItemMetrics`
模拟"先 H1 再列表 bold"的真实顺序，验证 ListItem 高度不被 H1 度量污染。
撤回 fix（key 仍用 qHash）时该测试失败：item.height=27 vs 期望 15。

# Bug: 列表间距修复对首次渲染失效，滚轮缩放后才生效

## 复现样本
新建文档，粘贴：

```
- **AI 智能识别**：手写中英文识别、数学/化学公式识别、图形识别、AI 自由书写（表格/流程图）
- **学科工具**：150+ 种教学工具（几何尺规、化学器材、函数图像、计数器、钟表等）
```

## 现象
- 首次粘贴 / 加载文档后，预览区两个列表项之间**仍有空白**（与上次 bug 一样的现象）
- **滚轮放大或缩小字号一次**后，空白消失，恢复正常紧凑间距

## 关键诊断
"滚轮缩放后消失"= **首次 layout 与缩放后的 layout 用了不同的字体度量基准**：
- 缩放触发了完整 re-layout，用的是已稳定的最终字号
- 首次渲染时 layout 提前执行，字体度量可能还没正确计算（m_fontSizeDelta、inlineRuns[0].font 未初始化、DPI 转换未应用，或派生 lineHeight 走了 baseFont 而非 inline run font）

## 怀疑根因
1. **首次 layout 缺少 inline runs**：解析→layout 阶段还没把 emphasis（粗体/斜体）切分成 inlineRuns，runs[0].font 取到 nullptr 或 default font，估算回落到 m_baseFont 行高（INV-10 链路漏第二处）
2. **m_fontSizeDelta 未应用**：用户的 Ctrl+滚轮缩放偏移在 ctor 后才应用，但首次 layout 已经发生
3. **DPI 度量两阶段**：首次 layout 时 widget 未 show，QFontMetricsF 的 paintdevice 不准；缩放重新 layout 时 widget 已 show，度量正确
4. **重 layout 触发条件不全**：上次 list agent 改 estimateParagraphHeight 后，可能没把"字体应用变化"作为 re-layout trigger

## 验收
- T-1：粘贴用户样本到新建文档 → 首次渲染就紧凑，**无需任何缩放/操作**
- T-2：从文件打开同样列表 → 首次渲染紧凑
- T-3：缩放后切回原字号 → 仍紧凑（不退化）
- T-4：相邻 emphasis（粗体）+ 中文混排，首次渲染紧凑

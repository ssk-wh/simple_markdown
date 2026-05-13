---
date: 2026-05-12
status: completed
related_specs:
  - specs/模块-editor/
---

# B6 粘贴 URL 时智能转 Markdown 链接

## 背景

工业界主流 Markdown 编辑器（Typora、VS Code Markdown）的标准行为：
**选中一段文字 → Ctrl+V 粘贴形如 `https://...` 的 URL → 自动转为 `[选中文字](url)`**。

当前 SimpleMarkdown 估计是直接覆盖选中区域（默认 Qt 行为），用户必须手工敲 `[]()` 包裹。
高频写作场景（贴文档链接、引用资料）痛感强。

## 动作

- [ ] Step 1: 在 EditorWidget 拦截 paste 事件
- [ ] Step 2: 判定剪贴板内容是单个 URL（正则 `^https?://\S+$`，trim 后）且当前有选中
- [ ] Step 3: 若是 → 替换选中为 `[选中文字](剪贴板URL)`；否则走默认 paste 路径
- [ ] Step 4: 在选中区**没有内容**时保持默认行为（粘贴 URL 原文）
- [ ] Step 5: 提供 `编辑 → 智能粘贴` 菜单项可关闭（部分用户可能不喜欢）

## 验收

- 选中 "Anthropic 官网" + 粘贴 `https://anthropic.com` → 编辑器变成 `[Anthropic 官网](https://anthropic.com)`
- 无选中 + 粘贴 URL → 原文粘贴
- 粘贴非 URL 内容 → 行为不变
- 撤销（Ctrl+Z）能一步还原到选中状态
- 关闭智能粘贴后行为退化到默认

---
date: 2026-05-11
status: completed
related_specs:
  - specs/模块-preview/11-预览区查找.md
---

# 预览侧 SearchBar 主题同步缺失（背景色与编辑器不一致）

## 背景

第三轮 Ctrl+F 改"可见性兜底"路由后用户可以连续看到两侧搜索栏（之前 INV-9 互斥
掩盖了差异），观察到两侧背景色不一致，怀疑是否设计区分。

## 根因

`PreviewWidget::setTheme` 历史上漏调 `m_searchBar->setTheme(theme)`：

- `src/editor/EditorWidget.cpp:196` 同步给了它的 SearchBar
- `src/preview/PreviewWidget.cpp:296-310`（原版）只同步给 painter / layout / viewport，
  漏了 m_searchBar

SearchBar 内部 `Theme m_theme;` 默认构造为浅色空主题（输入框白底、浅边框、深色文字）。
预览侧从未收到主题同步 → 一直显示这个默认主题——与编辑器侧实际主题脱节。

INV-9 互斥设计让一次只一侧可见，长期掩盖了视觉差异。

## 动作

- [x] `PreviewWidget::setTheme` 末尾补 `if (m_searchBar) m_searchBar->setTheme(theme);`
- [x] Spec 新增 INV-14：所有复用 SearchBar 的宿主 Widget 必须在 setTheme 中同步
- [x] 编译 + ctest 全绿
- [x] CHANGELOG 1.1.2 Fixed 段追加 1 条
- [x] 归档本 plan

## 验收

- 切换主题（如浅色 → 暗色）后两侧搜索栏背景色一致跟随
- 默认主题下两侧搜索栏视觉一致
- 未来若新增第三个复用 SearchBar 的 Widget，grep `m_searchBar->setTheme` 数量应等于
  宿主 Widget 数量

## 进展

- 2026-05-11 用户反馈背景不一致 → 根因为 PreviewWidget::setTheme 漏同步
- 2026-05-11 单行修复 + Spec INV-14 + 构建通过 12/12 ctest

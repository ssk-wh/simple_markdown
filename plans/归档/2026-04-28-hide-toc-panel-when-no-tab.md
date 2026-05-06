---
id: plans/hide-toc-panel-when-no-tab
status: completed
date: 2026-04-28
related_specs:
  - specs/模块-app/22-空白引导页.md
  - specs/模块-preview/07-TOC面板.md
---

# 关闭所有 Tab 后右侧 TOC 面板自动隐藏

## 现象
关闭所有打开的页面后，右侧的目录 + 文档信息面板（同一 widget `m_tocPanel`）仍然挂在窗口右侧，显示空目录和空白文档信息卡片——此时已经没有打开的页面与之对应。

## 期望
`m_tabs.isEmpty()` 时 `m_tocPanel` 应当 `hide()`，让 WelcomePanel 占满中央以外的空间；任何 Tab 出现时恢复显示。

## 影响范围
- src/app/MainWindow.cpp::updateEmptyState（统一显隐控制器）
- specs/模块-app/22-空白引导页.md：补 INV-EMPTY-TOC-HIDE + T-10 / T-11

## 步骤
1. specs/模块-app/22-空白引导页.md 加 INV-EMPTY-TOC-HIDE，相关 T 编号
2. MainWindow::updateEmptyState() 加一行 `if (m_tocPanel && !m_focusMode) m_tocPanel->setVisible(!empty);`
3. 验证：启动后关掉所有 Tab → 右侧 TOC 不可见；点 + 新建 Tab → TOC 重现
4. 更新 CHANGELOG.md

## 风险
- 演示模式（F11）下 TocPanel 由 m_savedTocVisible 单独管控——加 `!m_focusMode` 守卫规避双管齐下导致的状态错乱

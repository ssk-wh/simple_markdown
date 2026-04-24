---
title: Ctrl+B 切换左侧资源管理器显隐
status: in_progress
created: 2026-04-24
related_specs: []
---

# Ctrl+B 切换左侧资源管理器显隐

## 需求
快捷键 Ctrl+B 切换左侧面板（文件夹树 + 侧边 Tab 栏）的显示/隐藏。

## 实现步骤
1. MainWindow.h: 新增 `m_toggleSidebarAct` 和 `m_sidebarHidden` 成员
2. MainWindow.cpp setupMenuBar(): 在 View 菜单添加 Toggle Sidebar 动作，快捷键 Ctrl+B
3. MainWindow.cpp: 实现 toggleSidebar() — 手动隐藏时记录状态，updateLeftPaneVisibility() 尊重该状态
4. MainWindow.cpp saveSettings/loadSettings: 持久化 sidebarHidden 状态
5. ShortcutsDialog.cpp: 快捷键列表新增条目
6. i18n: tr() 包裹

## 验证
- 编译通过
- Ctrl+B 切换生效
- 重启后状态保持

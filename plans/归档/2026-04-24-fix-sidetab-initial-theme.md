---
title: 侧边 Tab 栏首次启动时文字颜色未跟随深色主题
status: draft
created: 2026-04-24
related_specs: [specs/模块-app/20-左侧面板.md]
---

# Bug: 侧边 Tab 栏首次启动时文字颜色未跟随深色主题

## 复现
1. 清空 QSettings（全新启动）
2. 系统为深色主题
3. 打开一个文件夹，左下角侧边 Tab 区域出现
4. Tab 项文字为黑色，背景也是深色 → 看不清

## 预期
侧边 Tab 栏文字应跟随当前主题为白色。

## 根因分析
`setTabBarPosition(true, false)` 在 `loadSettings` 中调用时，`m_sideTabBar->setTheme(m_currentTheme)` 被执行。但此时主题可能尚未完全应用，或 `m_currentTheme` 还是默认值。切换主题再切回后触发 `setTheme` 刷新，文字颜色恢复正常。

## 修复方向
确保 `loadSettings` 中主题加载完成后，对 `m_sideTabBar` 再次调用 `setTheme`；或在 `applyTheme` 中统一刷新 sideTabBar。

---
title: Tab 栏 "+" 按钮深色主题下颜色不可见
status: draft
created: 2026-04-24
related_specs: []
---

# Bug: Tab 栏 "+" 按钮深色主题下颜色不可见

## 复现
1. 系统使用深色主题
2. Tab 栏右侧的 "+" 按钮显示为黑色，与深色背景融为一体

## 预期
深色主题下 "+" 按钮应为白色或浅色，与背景形成对比。

## 修复方向
在 `applyTheme` 中为 TabBarWithAdd 的 "+" 按钮设置跟随主题的前景色。

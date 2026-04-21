---
title: 文件夹和 TOC 面板改用 TreeView 实现
status: draft
created: 2026-04-20
related_specs: []
---

## 需求

文件夹面板和 TOC 面板应使用 QTreeView 实现（文件夹面板已经是 QTreeView，TOC 面板当前是 QScrollArea + QPushButton 列表）。
TOC 面板改为 QTreeView 可以更好地支持层级折叠、统一样式、减少自定义代码。

---
title: 用户调整资源管理器宽度后重启未恢复
status: draft
created: 2026-04-24
related_specs: [specs/模块-app/20-左侧面板.md]
---

# Bug: 用户调整资源管理器宽度后重启未恢复

## 复现
1. 打开文件夹，拖拽左侧面板 splitter 调整宽度
2. 关闭应用
3. 重新打开，左侧面板宽度未恢复到上次调整的值

## 预期
用户手动调整的宽度应在重启后恢复（通过 mainSplitter saveState/restoreState 持久化）。

## 可能根因
showEvent 中 restoreState 后的 sanity check 可能将恢复的合理宽度判定为"过小"并覆盖为默认值（1/8），或 updateLeftPaneVisibility 中"隐藏→可见"的宽度修正干扰了恢复的值。

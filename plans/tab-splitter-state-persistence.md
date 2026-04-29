---
title: Tab 分隔条位置持久化
status: in_progress
date: 2026-04-29
related_specs:
  - specs/模块-app/13-分隔条吸附刻度.md
---

## 问题描述

用户调整 Tab 内的编辑/预览分隔条位置（改变编辑区与预览区的宽度），关闭应用再打开后，该 Tab 的分隔条会恢复到默认 50/50 位置，用户的布局调整丢失。

### 现象
1. 打开一个 markdown 文件
2. 拖动编辑/预览分隔条，调整为例如 30/70（编辑区 30%，预览区 70%）
3. 关闭应用
4. 重新打开应用，打开同一文件
5. 分隔条恢复到 50/50，编辑区与预览区的用户偏好布局丢失

### 根因

`MainWindow::saveSettings()` 在保存 Tab 状态时，仅保存了：
- editor scroll 位置
- preview scroll 位置  
- 光标位置
- 但**没有**保存 splitter 的 `setSizes()` 状态（editor 与 preview 的宽度比例）

在 `restoreSession()` 时，Tab 被重新创建时 `tab.splitter->setSizes({640, 640})` 硬编码为 50/50。

## 修复方案

### 保存端

在 `MainWindow::saveSettings()` 的 `session/tabs` 数组循环中，保存每个 Tab 的分隔条状态：

```cpp
// 保存编辑/预览分隔条位置
auto splitterSizes = m_tabs[i].splitter->sizes();
if (splitterSizes.size() >= 2 && splitterSizes[0] > 0 && splitterSizes[1] > 0) {
    s.setValue("editorWidth", splitterSizes[0]);
    s.setValue("previewWidth", splitterSizes[1]);
}
```

### 恢复端

在 `MainWindow::restoreSession()` 的 `for (auto& st : states)` 循环中，恢复 Tab 创建后立即应用分隔条位置：

```cpp
// 从 settings 恢复分隔条宽度
int savedEditorW = s.value("editorWidth", -1).toInt();
int savedPreviewW = s.value("previewWidth", -1).toInt();
if (savedEditorW > 0 && savedPreviewW > 0) {
    tab->splitter->setSizes({savedEditorW, savedPreviewW});
} else {
    tab->splitter->setSizes({640, 640});  // 默认 50/50
}
```

## 验收

- [ ] T1: 调整 Tab 的编辑/预览分隔条位置 → 关闭应用 → 重启 → 分隔条位置恢复
- [ ] T2: 多个 Tab 各自不同的分隔条位置 → 重启后每个 Tab 各自恢复自己的位置
- [ ] T3: 没有保存位置信息的 Tab 创建时默认 50/50

## 受影响的代码

- `src/app/MainWindow.cpp::saveSettings()` - 第 2186-2205 行（session/tabs 循环）
- `src/app/MainWindow.cpp::restoreSession()` - Tab 创建与恢复逻辑

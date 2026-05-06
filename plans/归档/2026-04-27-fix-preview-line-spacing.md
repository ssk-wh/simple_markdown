---
title: 设置行间距只影响编辑区，预览区不变
status: completed
created: 2026-04-27
completed: 2026-04-27
related_specs:
  - specs/模块-preview/02-布局引擎.md
  - specs/模块-preview/03-绘制管线.md
  - specs/模块-app/15-状态栏布局.md
---

# Bug: 视图 → 行间距 切换后预览区不更新

## 复现
1. 打开任意 .md 文件
2. 视图 → 行间距 → 选择 1.0 / 1.2 / 1.5 / 1.8 / 2.0 任一档
3. 编辑区行间距立刻生效
4. **预览区行间距没变化**（与编辑区错位）

## 期望
两边行间距同步切换，视觉对齐。

## 真实根因
`MainWindow::onLineSpacingTriggered`（line 429-431）只调了：
```cpp
m_lineSpacingFactor = factor;
// 只对 editor 生效
tab.editor->setLineSpacing(factor);
```
预览区类（`PreviewWidget`）完全没有 `setLineSpacing` API；自绘 `PreviewLayout` 内部用固定的 `lineHeight = fm.height() * 1.5`，没消费 lineSpacingFactor。

## 修复方案
1. `PreviewLayout.h/.cpp` 增加 `void setLineSpacingFactor(qreal factor)` —— 替换内部硬编码 1.5 用此 factor
2. `PreviewWidget.h/.cpp` 增加同名转发 API
3. `MainWindow::onLineSpacingTriggered` 同步调用：
```cpp
tab.preview->setLineSpacingFactor(factor);
```
4. `loadSettings`、`createTab` / restoreSession 路径同步把保存的 lineSpacingFactor 应用到新创建的 preview
5. Spec 02-布局引擎 INV-10/INV-11 公式描述补 lineSpacingFactor 参数
6. 验收：T-1 切换 1.0 时预览区行高 = 单倍字体；T-2 切换 2.0 时预览行高翻倍；T-3 重启后预览区行间距与设置一致

## 阻塞
当前后台 `aa84c0db8068d7635` agent 正在改 `src/preview/PreviewLayout.cpp` 修复"未命名+粘贴 list 间距"。等它完成再 spawn 另一个 agent 做这个修复，避免同文件冲突。

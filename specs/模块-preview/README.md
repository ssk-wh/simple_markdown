# 模块：preview（自绘预览）

## 职责

把 `MarkdownAst` 渲染成可视化预览，支持主题、图片、代码高亮、TOC 导航、内容标记。基于 `QAbstractScrollArea` 完全自绘。

## 对应源码

`src/preview/`

## Spec 清单

| 编号 | 标题 | 状态 | 对应源文件 |
|------|------|------|-----------|
| 01 | 预览主控件 | draft | `PreviewWidget.h/cpp` |
| 02 | 布局引擎 | draft | `PreviewLayout.h/cpp` |
| 03 | 绘制管线 | draft | `PreviewPainter.h/cpp` |
| 04 | 块级渲染缓存 | draft | `PreviewBlockCache.h/cpp` |
| 05 | 图片缓存 | draft | `ImageCache.h/cpp` |
| 06 | 代码块渲染 | draft | `CodeBlockRenderer.h/cpp` |
| 07 | TOC 面板 | draft | `TocPanel.h/cpp`, `MainWindow.cpp` |
| 08 | 内容标记（Marking） | draft | `PreviewWidget.h/cpp`, `PreviewPainter.h/cpp` |
| 09 | 链接点击与导航 | draft | `PreviewWidget.h/cpp`, `PreviewPainter.cpp`, `MainWindow.cpp` |
| 10 | Frontmatter 渲染 | draft | `PreviewLayout.cpp`, `PreviewPainter.cpp`, `MarkdownAst.h`, `MarkdownParser.cpp`, `Theme.h/cpp` |

## 依赖关系

```
PreviewWidget
  ├─ PreviewLayout        ← AST → LayoutBlock 树
  ├─ PreviewPainter       ← LayoutBlock → 屏幕
  ├─ PreviewBlockCache    ← LRU 块级位图缓存
  ├─ ImageCache           ← 异步图片加载
  ├─ CodeBlockRenderer    ← 代码高亮
  ├─ TocPanel             ← 右侧目录导航（独立面板）
  └─ core::Theme          ← 主题色读取
```

## 性能预算

| 操作 | 目标 |
|------|------|
| 预览更新（含 30ms 防抖） | < 80 ms |
| 滚动一屏 | < 16 ms（60fps） |
| 图片加载（本地） | < 30 ms |

## 全局约束

- **必须遵守** `横切关注点/40-高DPI适配.md` 中所有 INV 条目
- 块级渲染必须缓存位图，避免重复绘制
- 图片加载必须异步，不阻塞主线程
- 标记高亮必须在选区高亮**之前**绘制，避免覆盖

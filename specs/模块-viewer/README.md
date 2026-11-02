# 模块：viewer（只读分块查看器）

> 2.0.0 起 SimpleMarkdown 是**只读查看器**：编辑器、分屏、滚动同步均已移除。本模块承担「打开 → 首屏 → 滚动 → 目录跳转 → 查找」全链路，设计目标是**打开与滚动代价与文件大小无关**。

## 职责

把任意大小的 Markdown 文件切成可独立解析/布局的 chunk，用虚拟化滚动只处理可视 chunk；文件读取与扫描在后台线程完成。

## 对应源码

`src/viewer/`（`sm_viewer`：纯逻辑与线程；`sm_viewerwidget`：控件，依赖 preview/parser）

## Spec 清单

| 编号 | 标题 | 状态 | 对应源文件 |
|------|------|------|-----------|
| 01 | 分块扫描 | draft | `ChunkScanner.h/cpp` |
| 02 | 滚动模型 | draft | `ScrollModel.h/cpp` |
| 03 | 异步加载 | draft | `DocumentLoader.h/cpp` |
| 04 | 查看器控件 | draft | `ViewerWidget.h/cpp` |
| 05 | 查找 | draft | `BufferSearch.h/cpp`、`ViewerWidget`（查找部分） |

## 数据流

```
文件 ──(专用 QThread: 读取+扫描)──► LoadBatch(chunks/headings/水位线) ──(队列信号)──► ViewerWidget
ViewerWidget: ScrollModel(锚点+Fenwick) → 可视 chunk → [parseChunk → layoutChunk](主线程毫秒级) → paintChunk
```

## 性能预算（2026-11-02 本机实测，Release/MSVC）

| 指标 | 预算 | 实测 |
|---|---|---|
| 进程启动到首屏内容（含 50MB 文件） | ≤ 1.2s | 0.8~0.9s（中位数） |
| 目录跳转（50MB） | ≤ 100ms | 5~16ms |
| 滚动单帧 P95 | ≤ 16ms | 7~9ms |
| 50MB 读取+扫描（后台） | ≤ 1.5s | ~0.1s |
| 百万 chunk 滚动模型构建 / 随机跳转 | — | 26ms / 2000 次 1.4ms |

## 全局约束

- 绝不对整篇文档构建 AST 或布局（只处理可视 chunk，LRU 缓存有上限）
- 布局/高度回写/滚动条同步只允许在 `prepare()` 内发生，**`paintEvent` 内禁止触发**（INV-VIEW-NO-LAYOUT-IN-PAINT，历史崩溃 1.1.27/1.1.28 的教训）
- 禁止使用全局 QThreadPool（进程退出死锁，见 03 INV-LOAD-NOPOOL）
- 不 mmap 源文件（Windows 下会锁文件，外部编辑器保存失败）

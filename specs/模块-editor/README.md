# 模块：editor（自绘编辑器）

## 职责

基于 `QAbstractScrollArea` 的完全自绘 Markdown 编辑器，提供文本编辑、语法高亮、搜索替换、IME、滚动等能力。追求 < 3ms 的打字延迟。

## 对应源码

`src/editor/`

## Spec 清单

| 编号 | 标题 | 状态 | 对应源文件 |
|------|------|------|-----------|
| 01 | 编辑器主控件 | draft | `EditorWidget.h/cpp` |
| 02 | 行布局引擎 | draft | `EditorLayout.h/cpp` |
| 03 | 绘制管线 | draft | `EditorPainter.h/cpp` |
| 04 | 输入与 IME | draft | `EditorInput.h/cpp` |
| 05 | 语法高亮 | draft | `SyntaxHighlighter.h/cpp` |
| 06 | 查找替换 | draft | `SearchBar.h/cpp`, `SearchWorker.h/cpp` |
| 07 | 行号绘制 | draft | `GutterRenderer.h/cpp` |
| 08 | 带缓冲绘制 | draft | `BandBuffer.h/cpp` |

## 依赖关系

```
EditorWidget
  ├─ EditorLayout       ← 行布局与坐标转换
  ├─ EditorPainter      ← 绘制逻辑
  ├─ EditorInput        ← 键盘/鼠标/IME
  ├─ SyntaxHighlighter  ← 正则高亮
  ├─ SearchBar          ← UI 浮层
  │    └─ SearchWorker  ← 后台搜索
  ├─ GutterRenderer     ← 行号列
  └─ BandBuffer         ← 可见区域位图缓存
     └─ core::Document  ← 文本源
```

## 关键不变量

### [INV-EDITOR-PAINT-UNIFIED] 行号与文本绘制必须在同一 per-line loop 中推进（2026-05-13 A8）

`EditorWidget::paintEvent` 的主绘制循环必须按行迭代，本地维护 cursorY 推进；每行**同时**画：

1. 行号（gutter 区域，X=0..gutterWidth）—— 用 baseline 对齐到该行第一视觉行的
   `tl->lineAt(0).ascent()`，避免字号差异引入视觉错位
2. 文本（通过 `EditorPainter::paintLine`，X=gutterWidth..viewportWidth）—— 在同一 cursorY 处画
3. 推进：`cursorY += layout->lineHeight(line)`（精算值，wrap 多行天然累加）

**禁止**把行号和文本拆成两个独立 loop 共享 `lineY` —— 这种契约脆弱：yCache 在估算和
精算之间漂移时（wrap 多行、新增行、视口边界），两个 loop 拿到的 lineY 不一致 → 行号与
文本错位。历史 bug 见 `plans/归档/2026-05-13-编辑区新增内容时行号闪烁.md`（aborted）
和 `plans/归档/2026-05-13-A8编辑器行号与文本绘制合一.md`（completed）。

光标 / IME 预编辑可在 per-line loop **之后**画（基于 `cursorRect` → `lineY`）—— 它们
单独依赖 yCache 不影响行间对齐。

## 性能预算

| 操作 | 目标 |
|------|------|
| 打字响应 | < 3 ms |
| 滚动一屏 | < 10 ms |
| 切换 10k 行文件 | < 50 ms |
| 全文档语法高亮 | < 200 ms |

## 全局约束

- 所有绘制必须在主线程
- 不得在 `paintEvent` 中触发布局重算，布局必须预先完成
- 搜索必须在 worker 线程，UI 不阻塞

## 语法染色 Inline Token 清单（INV 摘要）

`SyntaxHighlighter` 至少覆盖以下 inline 标记，均尊重 inline code 范围（即 `` `...` `` 内部不触发）：

| Token | 触发 | 格式 | INV |
|-------|------|------|-----|
| heading | `^#{1,6}\s` | `syntaxHeading` + Bold | INV-EDIT-HEADING |
| blockquote | `^>\s?` | `syntaxBlockQuote` | INV-EDIT-QUOTE |
| list marker | `^\s*(-\|\*\|\+\|\d+\.)\s` | `syntaxList` | INV-EDIT-LIST |
| inline code | `` `([^`]+)` `` | `syntaxCode` + bg | INV-EDIT-CODE |
| bold-italic | `\*\*\*([^*]+)\*\*\*` | Bold + Italic | **INV-EDIT-BOLDITALIC** |
| bold | `\*\*([^*]+)\*\*` | Bold | INV-EDIT-BOLD |
| italic | `(?<!\*)\*([^*]+)\*(?!\*)` | Italic | INV-EDIT-ITALIC |
| strikethrough | `~~([^~]+)~~` | FontStrikeOut | **INV-EDIT-STRIKE** |
| link | `\[...\]\(...\)` | `syntaxLink` + Underline | INV-EDIT-LINK |
| fence | `^\s*` ` ``` ` | `syntaxFence` + bg | INV-EDIT-FENCE |

- **INV-EDIT-BOLDITALIC**：`***text***` 必须在 bold / italic 之前匹配，其范围内不再额外产生 bold 或 italic token，避免子串重复着色。
- **INV-EDIT-STRIKE**：`~~text~~` 视觉等价于预览侧 `AstNodeType::Strikethrough` 的渲染——编辑器必须产生一个 `FontStrikeOut==true` 的 token，让用户在编辑时就能看到删除效果（与 `Ctrl+D` 快捷键呼应）。
- 所有 inline token 与 inline code 互斥：code 范围内不触发 bold / italic / bold-italic / strikethrough / link。

### 验收 T 条目

- **T-STRIKE-1**：`a ~~foo~~ b` 高亮包含一个 `fontStrikeOut==true` 的 token 覆盖 `~~foo~~`
- **T-BOLDITALIC-1**：`pre ***foo*** post` 高亮包含一个粗体+斜体 token 覆盖 `***foo***`
- **T-BOLDITALIC-NoDuplicate**：`***foo***` 高亮只产生 1 个粗斜体 token，不同时产生 bold 和 italic
- **T-PRIORITY-1**：`` `~~foo~~ and ***bar***` `` 整段在 inline code 范围内，不产生 strikethrough 或 bold-italic token

测试文件：`tests/editor/SyntaxHighlighterTokensTest.cpp`

# SimpleMarkdown 设计规格

## 1. 概述

SimpleMarkdown 是一款轻量、高性能的跨平台（Windows/Linux）Markdown 阅读/编辑器。采用分屏模式（左侧源码编辑 + 右侧实时预览），通过 QPainter 自绘实现编辑器和预览渲染，追求极致的启动速度和输入响应。

### 1.1 核心指标

| 指标 | 目标 |
|------|------|
| 冷启动 | < 0.5 秒 |
| 空载内存 | < 30 MB |
| 打字延迟 | < 3 ms |
| 预览更新 | < 80 ms（含 30ms 防抖）|

### 1.2 功能清单

- 分屏模式：左侧 Markdown 源码编辑，右侧实时渲染预览
- 多 Tab 支持
- 拖拽文件到窗口自动打开
- Markdown 特性：标题、粗体、斜体、列表、链接、引用、分割线、行内代码、围栏代码块+语法高亮、GFM 表格、图片（本地+网络）
- 搜索替换
- 亮色/暗色主题切换
- 最近文件列表

### 1.3 技术栈

| 组件 | 选型 | 理由 |
|------|------|------|
| 语言 | C++17 | 性能、跨平台 |
| UI 框架 | Qt 5.15 | 成熟稳定、跨平台 GUI |
| 构建系统 | CMake >= 3.16 | Qt 官方推荐、生态丰富 |
| Markdown 解析 | cmark-gfm | GitHub 官方 C 库，GFM 支持，性能优秀 |
| 代码高亮 | KSyntaxHighlighting | KDE 官方，300+ 语言，输出 QTextCharFormat |
| 单元测试 | Google Test | C++ 最流行，与 CMake 集成好 |

---

## 2. 架构

### 2.1 线程模型

```
主线程（UI）                         解析线程
├── 编辑器输入处理                    ├── cmark-gfm 解析
├── PieceTable 修改                  ├── AST 构建
├── 语法高亮（增量）                  └── 预览布局计算
├── 编辑器自绘渲染                         │
├── 预览自绘渲染（从缓存合成）              │
└── 滚动同步                         emit astReady(ast)
                                     → 主线程处理
```

编辑器侧操作完全在主线程完成，不等待解析线程，保证打字零延迟。

### 2.2 模块依赖图

```
app/MainWindow
  ├── widgets/TabBar
  ├── widgets/SplitView
  │     ├── editor/EditorWidget
  │     │     ├── core/Document
  │     │     │     ├── core/PieceTable
  │     │     │     ├── core/UndoStack
  │     │     │     └── core/Selection
  │     │     ├── editor/EditorLayout
  │     │     ├── editor/EditorPainter
  │     │     ├── editor/EditorInput
  │     │     ├── editor/BandBuffer
  │     │     └── highlight/SyntaxHighlighter
  │     └── preview/PreviewWidget
  │           ├── preview/PreviewLayout
  │           ├── preview/PreviewPainter
  │           ├── preview/PreviewBlockCache
  │           ├── preview/ImageCache
  │           └── preview/CodeBlockRenderer
  └── sync/ScrollSync
        └── sync/PositionMap

parser/ParseScheduler (解析线程)
  ├── parser/MarkdownParser
  └── parser/MarkdownAst
```

### 2.3 数据流

**打字路径（关键路径，< 3ms）**：
```
keyPressEvent
  → Document::insert(offset, text)
    → PieceTable::insert()              // O(1)
    → UndoStack::push()
    → emit textChanged(offset, 0, len)
      → SyntaxHighlighter::invalidateFromLine(N)  // 增量，1-3行
      → EditorLayout::updateLayout(N, N)           // 增量，受影响行
      → viewport()->update(dirtyRect)              // 排队
  → paintEvent
    → EditorPainter::paint()            // 只绘制可见 dirty 行
```

**预览更新路径（异步，30ms 防抖后）**：
```
textChanged
  → ParseScheduler::debounce(30ms)
    → [解析线程] MarkdownParser::reparse()
    → emit astReady(ast)
      → [主线程] PreviewLayout::updateLayout()
      → PositionMap::build()
      → PreviewWidget::viewport()->update()
```

---

## 3. 核心模块设计

### 3.1 PieceTable 文本存储

**选型理由**：插入/删除 O(1)、天然支持撤销、内存效率高（原始文本不复制）。

**数据结构**：
```cpp
struct Piece {
    enum Source : uint8_t { Original, Add };
    Source source;
    uint32_t start;      // 在对应 buffer 中的起始偏移
    uint32_t length;     // 字符数
    uint32_t lineFeeds;  // 此 piece 中的换行数（缓存）
};

class PieceTable {
    QString m_original;              // 原始文本，只读
    QString m_add;                   // 追加缓冲区，只追加
    std::vector<Piece> m_pieces;     // piece 表
    std::vector<int> m_lineFeedPrefix; // 行号前缀和

    // 大文件 (>1MB)：m_original 替换为 MappedFile (mmap)
};
```

**关键接口**：
- `insert(offset, text)` / `remove(offset, length)` / `replace(offset, length, text)`
- `text()` / `lineText(line)` / `lineCount()`
- `offsetToLine(offset)` / `lineToOffset(line)` — 二分查找，O(log n)

**升级路径**：初始用 vector，如需支持超大文件可将 pieces 升级为红黑树。

### 3.2 UndoStack

```cpp
struct EditOperation {
    int offset;
    QString removedText;
    QString addedText;
    int64_t timestamp;
};
```

- **合并策略**：连续单字符输入，间隔 < 300ms 且偏移连续，合并为一个操作。空格/换行/删除断开合并。
- **savePoint**：标记当前为"已保存"状态，用于文档修改标记判断。

### 3.3 Selection 选区模型

```cpp
struct TextPosition { int line; int column; };
struct SelectionRange { TextPosition anchor; TextPosition cursor; };
```

- `anchor`：选择起点（不动端），`cursor`：光标位置（移动端）
- `preferredColumn`：上下移动时记忆的列位置
- 操作：moveCursor、setSelection、selectAll、selectWord（双击）、selectLine

### 3.4 Document 文档模型

整合 PieceTable + UndoStack + Selection：
- 所有文本操作通过 Document 入口
- `textChanged(offset, removedLen, addedLen)` 信号驱动下游更新
- 文件 I/O：loadFromFile / saveToFile
- 修改标记：isModified，基于 UndoStack::savePoint

---

## 4. 自绘编辑器

### 4.1 EditorWidget

继承 QAbstractScrollArea，viewport 分为：
- 行号栏（gutter）：左侧固定宽度
- 文本编辑区：右侧可滚动
- SearchBar：浮动在顶部的搜索替换栏

### 4.2 EditorLayout 行布局引擎

使用 QTextLayout 处理每行的文本排版：
- 支持折行（word wrap）、CJK 字符宽度、Tab 对齐
- 缓存每行的 QTextLayout 对象，文本变化只重算受影响行
- 核心转换：
  - `hitTest(x, y) → TextPosition`（像素→文本位置）
  - `positionToPoint(TextPosition) → QPointF`（文本位置→像素）
  - `cursorRect(TextPosition) → QRectF`（光标矩形，IME 定位用）

**虚拟化**：大文件只为可见行 ± 100 行创建 QTextLayout，超出范围用默认行高估算滚动条总高度。

### 4.3 三级渲染缓存

1. **行布局缓存**（Level 1）：行号 → {QTextLayout, height, dirty}，只在文本变化时标记 dirty
2. **带区缓冲**（Level 2）：可见区域 + 上下各 0.5 屏预渲染到 QPixmap，滚动时 bitblt
3. **脏行追踪**（Level 3）：编辑只标记变化行为 dirty，paintEvent 只重绘 dirty 行到 Band Buffer

### 4.4 EditorPainter 绘制顺序

1. 背景色填充
2. 当前行高亮背景
3. 选区蓝色背景
4. 文本绘制（QTextLayout::draw，带语法高亮格式）
5. 光标竖线（500ms 闪烁）
6. 搜索匹配高亮背景

### 4.5 EditorInput 输入处理

**键盘快捷键**：
| 快捷键 | 动作 |
|--------|------|
| Ctrl+Z / Ctrl+Y | 撤销/重做 |
| Ctrl+X/C/V | 剪切/复制/粘贴 |
| Ctrl+A | 全选 |
| Ctrl+F / Ctrl+H | 搜索/替换 |
| Tab / Shift+Tab | 缩进/反缩进 |
| Home/End | 行首/行尾 |
| Ctrl+Home/End | 文档首/尾 |

**IME 中文输入**：
- `inputMethodEvent`：处理 preeditString（下划线显示）和 commitString（插入）
- `inputMethodQuery`：返回 ImCursorRectangle（输入法窗口定位）、ImSurroundingText、ImCursorPosition
- `setAttribute(Qt::WA_InputMethodEnabled, true)` 启用

**鼠标**：点击定位光标、拖拽选择、双击选词、三击选行

### 4.6 SyntaxHighlighter

继承 KSyntaxHighlighting::AbstractHighlighter：
- `highlightLine(lineIndex, lineText)` → Token 列表 [{start, length, format}]
- 缓存每行的 Token 列表和解析器 State
- **增量高亮**：编辑第 N 行时，从第 N-1 行的 State 开始重新高亮，如果某行结束 State 与缓存相同则停止

---

## 5. 自绘预览渲染器

### 5.1 PreviewLayout 布局引擎

将 cmark-gfm AST 转换为 LayoutBlock 树：

```cpp
struct LayoutBlock {
    Type type;               // Paragraph, Heading, CodeBlock, BlockQuote, Table, Image, ...
    QRectF bounds;           // 相对父块的矩形
    int sourceStartLine;     // 源码起始行（滚动同步用）
    int sourceEndLine;

    // Paragraph/Heading/ListItem: 行内元素
    struct InlineRun {
        QString text;
        QFont font;
        QColor color;
        QColor bgColor;     // 行内代码背景
        QString linkUrl;
        bool isStrikethrough;
    };
    std::vector<InlineRun> inlineRuns;
    std::vector<TextLine> textLines;  // 折行后的行

    // CodeBlock: 代码文本 + 高亮行
    // Image: URL + QPixmap* + 尺寸
    // Table: 列宽 + 对齐
    std::vector<LayoutBlock> children;
};
```

**行内元素折行算法**：遍历 InlineRun，按字/CJK字符累加宽度，超过 maxWidth 换行。

### 5.2 块级渲染缓存

每个顶层 AST 块渲染为独立 QPixmap：
- AST 变化时，通过 sourceStartLine/endLine 判断哪些块 dirty
- dirty 块重新渲染到新 QPixmap
- 未变化的块直接 drawPixmap
- LRU 总量限制 ~4MB

### 5.3 各元素渲染规格

| 元素 | 渲染方式 |
|------|---------|
| H1-H6 | 字号 1.8/1.5/1.3/1.1/1.0/0.9 倍基准，H1/H2 下方 1px 分割线 |
| 段落 | 正文字体，段间距 0.8em |
| 粗体/斜体 | QFont::Bold / QFont::StyleItalic |
| 行内代码 | 等宽字体 + 浅色背景矩形 + 2px padding |
| 代码块 | 深色背景 + 等宽字体 + KSyntaxHighlighting + 8px padding |
| 引用块 | 左侧 3px 竖线 + 16px 缩进 + 浅灰背景 |
| 无序列表 | ●/○/■ 标记（按嵌套层级）+ 24px 缩进 |
| 有序列表 | 数字编号 + 24px 缩进 |
| 链接 | 蓝色 + 下划线 |
| 图片 | 异步加载，缩放到 maxWidth，居中，加载中灰色占位框 |
| 表格 | 网格线 + 单元格 8px padding + 表头加粗 + 列对齐 |
| 分割线 | 1px 浅灰水平线，上下 16px margin |
| 删除线 | 文字中间画横线 |

### 5.4 ImageCache 图片异步加载

- `get(url)` 同步查缓存，命中返回 QPixmap*，未命中返回 nullptr 并触发异步加载
- 本地图片：QThreadPool + QImageReader 解码
- 网络图片：QNetworkAccessManager 下载 + 线程池解码
- LRU 缓存上限 20MB
- `imageReady(url)` 信号通知预览重绘

### 5.5 CodeBlockRenderer

- 使用 KSyntaxHighlighting::Repository 查找语言语法定义
- 延迟初始化 Repository（`std::call_once`），避免拖慢启动
- 输出 HighlightedLine：[{text, color, bold, italic}, ...]

---

## 6. 编辑器-预览同步

### 6.1 PositionMap

从 AST 构建 (sourceLine, previewY) 映射表：
- 遍历每个块级 AST 节点，将 startLine 与对应 LayoutBlock 的 bounds.y() 配对
- 按 sourceLine 排序的有序数组
- 查询时二分查找 + 线性插值

### 6.2 ScrollSync

**锚点映射法**：
1. 编辑器滚动时，找到可见第一行行号 N
2. 通过 PositionMap 查行 N 在预览中的 y 坐标
3. 计算行 N 距 viewport 顶部的偏移 delta
4. 预览滚动到：previewY(N) - delta * (previewLineHeight / editorLineHeight)

**反向同步**：预览滚动时反向查找源码行号，平滑滚动编辑器。

**防抖**：16ms 节流 + `m_syncing` 标志防止循环触发。

**平滑滚动**：QPropertyAnimation，150ms，OutCubic 缓动。

---

## 7. 应用层

### 7.1 MainWindow

- 菜单栏：文件（新建/打开/保存/另存/最近文件）、编辑（撤销/重做/搜索/替换）、视图（主题切换）
- 工具栏：常用操作按钮
- 状态栏：行号:列号、字符数、编码

### 7.2 TabBar

- 每个 Tab 对应一个 (Document, EditorWidget, PreviewWidget) 三元组
- Tab 标题显示文件名 + 修改标记 (*)
- 关闭 Tab 时检查未保存修改
- 拖拽文件到 TabBar 新建 Tab 打开

### 7.3 拖拽支持

- `MainWindow::setAcceptDrops(true)`
- `dragEnterEvent`：检查 mimeData 包含文件 URL 且扩展名为 .md/.markdown/.txt
- `dropEvent`：调用 openFile() 打开

### 7.4 RecentFiles

- 存储最近 10 个文件路径到 QSettings
- 菜单中显示最近文件列表
- 点击快速打开

### 7.5 Theme 主题

```json
{
    "name": "Light",
    "editor": {
        "background": "#FFFFFF",
        "foreground": "#333333",
        "currentLine": "#F5F5F5",
        "selection": "#B5D5FF",
        "lineNumber": "#999999",
        "cursor": "#333333"
    },
    "preview": {
        "background": "#FFFFFF",
        "foreground": "#333333",
        "heading": "#1A1A1A",
        "link": "#0366D6",
        "codeBackground": "#F6F8FA",
        "blockQuoteBorder": "#DFE2E5",
        "tableBorder": "#DFE2E5"
    }
}
```

支持亮色/暗色两套主题，从 JSON 加载，可扩展自定义主题。

---

## 8. 性能优化策略

### 8.1 启动优化

| 阶段 | 耗时 | 工作 |
|------|------|------|
| Phase 0 | < 50ms | QApplication + 主窗口 show（空白画布）|
| Phase 1 | < 200ms | 创建编辑器、加载最近文件 |
| Phase 2 | < 500ms | 启动解析线程、初始化完整功能 |

KSyntaxHighlighting::Repository 延迟到第一个代码块出现时初始化（`std::call_once`）。

### 8.2 内存预算

| 组件 | 预算 |
|------|------|
| Qt 框架 + Widget | ~14 MB |
| 编辑器 Band Buffer | ~8 MB |
| 预览块 QPixmap 缓存 | ~4 MB |
| 行布局缓存 | ~1 MB |
| **空载总计** | **~27 MB** |
| 图片缓存（按需）| 上限 20 MB LRU |

### 8.3 大文件优化（> 1MB）

- PieceTable：原始文本用 mmap，不消耗物理内存
- 编辑器：虚拟化，只为可见行 ± 100 行创建 QTextLayout
- 行号扫描：memchr 快速扫描换行符建行索引
- 预览：可选关闭实时预览，改为手动触发

---

## 9. 跨平台处理

| 差异 | Windows | Linux | 策略 |
|------|---------|-------|------|
| 字体 | Consolas / Microsoft YaHei | Monospace / Noto Sans CJK | 运行时按优先级列表检测 |
| 高 DPI | Per-monitor DPI | X11/Wayland 缩放 | Qt::AA_EnableHighDpiScaling |
| 换行符 | CRLF | LF | 内部统一 LF，保存按原文件风格 |
| IME | TSF/IMM32 | IBus/Fcitx | Qt 抽象层 + inputMethodQuery |
| 文件路径 | `\` + 盘符 | `/` | QFileInfo / QDir |
| 配置路径 | %APPDATA%/easy_markdown | ~/.config/easy_markdown | QStandardPaths |
| mmap | CreateFileMapping | mmap() | MappedFile 封装 |

---

## 10. 单元测试覆盖

| 测试文件 | 覆盖范围 |
|---------|---------|
| test_PieceTable | 插入/删除/替换、行号计算、空文本、大文本性能基准 |
| test_UndoStack | push/undo/redo、合并策略（时间+连续偏移）、savePoint |
| test_Selection | 光标移动（含折行）、Shift 扩展、selectWord/selectLine/selectAll |
| test_Document | 文件加载/保存、修改标记、undo/redo 集成、换行符处理 |
| test_MarkdownParser | 各 Markdown 元素、GFM 扩展、行号映射准确性、异常输入 |
| test_EditorLayout | 折行计算、hitTest 坐标转换、CJK 宽度、Tab 对齐 |
| test_PreviewLayout | 各块类型布局、嵌套列表缩进、表格列宽、图片占位 |
| test_ScrollSync | 源码行→预览Y 映射、反向映射、插值精度、边界情况 |
| test_ImageCache | 缓存命中/未命中、LRU 淘汰、并发加载安全 |

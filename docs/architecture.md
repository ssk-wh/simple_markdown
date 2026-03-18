# SimpleMarkdown 架构文档

## 1. 整体架构

```
┌─────────────────────────────────────────────┐
│                 MainWindow                   │
│  ┌─────────┐  ┌───────────────────────────┐ │
│  │ MenuBar │  │        QTabWidget         │ │
│  └─────────┘  │  ┌─────────┬────────────┐ │ │
│               │  │ Editor  │  Preview    │ │ │
│               │  │ Widget  │  Widget     │ │ │
│               │  └────┬────┴─────┬──────┘ │ │
│               └───────┼──────────┼────────┘ │
│                       │          │          │
│              ┌────────┴──┐  ┌────┴───────┐  │
│              │ Document  │  │ ParseSched │  │
│              │(PieceTable│  │ (cmark-gfm)│  │
│              │ UndoStack │  └────┬───────┘  │
│              │ Selection)│       │ AST      │
│              └───────────┘  ┌────┴───────┐  │
│                             │ Preview    │  │
│                             │ Layout     │  │
│                             └────────────┘  │
│              ┌────────────┐                 │
│              │ ScrollSync │                 │
│              └────────────┘                 │
└─────────────────────────────────────────────┘
```

## 2. 模块说明

### 2.1 core/ — 核心数据模型
- **PieceTable**: Piece Table 文本存储引擎
- **UndoStack**: 撤销/重做栈，支持自动合并连续输入
- **Selection**: 选区模型（anchor + cursor + preferredColumn）
- **Document**: 整合 PieceTable + UndoStack + Selection，提供文件 I/O
- **MappedFile**: 跨平台 mmap 只读文件映射
- **RecentFiles**: 最近文件管理（QSettings 持久化）
- **Theme**: 亮色/暗色主题定义

### 2.2 editor/ — 自绘编辑器
- **EditorWidget**: QAbstractScrollArea 主控件
- **EditorLayout**: 行布局引擎（QTextLayout 缓存 + 坐标转换）
- **EditorPainter**: 绘制逻辑（背景 → 当前行 → 选区 → 文本 → 光标 → 搜索匹配）
- **EditorInput**: 键盘/鼠标/IME 输入处理
- **SyntaxHighlighter**: Markdown 正则语法高亮
- **SearchBar**: 浮动搜索替换栏

### 2.3 parser/ — Markdown 解析
- **MarkdownAst**: AST 节点 C++ 包装
- **MarkdownParser**: cmark-gfm 封装
- **ParseScheduler**: 防抖 30ms + 后台线程解析

### 2.4 preview/ — 自绘预览
- **PreviewWidget**: QAbstractScrollArea 主控件
- **PreviewLayout**: AST → LayoutBlock 布局树
- **PreviewPainter**: LayoutBlock 绘制
- **PreviewBlockCache**: LRU 块级渲染缓存
- **ImageCache**: 图片异步加载与 LRU 缓存
- **CodeBlockRenderer**: 代码块高亮

### 2.5 sync/ — 同步
- **ScrollSync**: 编辑器-预览滚动同步

### 2.6 app/ — 应用层
- **MainWindow**: 多 Tab 主窗口、菜单栏、拖拽支持

## 3. 线程模型

- **主线程**: UI 渲染、用户交互、编辑器绘制、预览绘制
- **解析线程**: cmark-gfm 解析（ParseScheduler 管理）

## 4. 数据流

```
用户输入 → Document::insert() → PieceTable + UndoStack
  ├→ SyntaxHighlighter → EditorLayout → EditorPainter (即时, < 3ms)
  └→ ParseScheduler (30ms 防抖) → [解析线程] cmark-gfm
      → AST → PreviewLayout → PreviewPainter
```

## 5. 第三方依赖

| 库 | 版本 | 用途 |
|----|------|------|
| Qt | 5.15 | GUI 框架 |
| cmark-gfm | 0.29.0.gfm.13 | Markdown 解析 |
| Google Test | 1.14.0 | 单元测试 |

# SimpleMarkdown Phase 3: Markdown 解析 + 自绘预览

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development to implement this plan.

**Goal:** 实现 Markdown 解析（cmark-gfm）和自绘预览渲染，完成分屏编辑模式（左编辑右预览）。

**Architecture:** ParseScheduler 监听 Document::textChanged，防抖 30ms 后在后台线程用 cmark-gfm 解析为 AST，通过信号传递给 PreviewWidget。PreviewLayout 将 AST 转为 LayoutBlock 树，PreviewPainter 绘制到 viewport。

**Tech Stack:** C++17, Qt 5.15 (Core + Widgets + Network), CMake, cmark-gfm, Phase 1/2 的库

---

## Tasks

### Task 1: CMake 脚手架
- 创建 src/parser/ 和 src/preview/ 目录及 CMakeLists.txt
- easy_parser 链接 cmark-gfm，easy_preview 链接 easy_parser + Qt5::Network
- 根 CMakeLists.txt 添加 Network 组件
- 所有空桩文件
- Commit: `chore: 搭建Phase3解析和预览模块`

### Task 2: MarkdownAst — AST 节点包装
- AstNode 类：type, literal, children, startLine/endLine, headingLevel, fenceInfo, url 等
- 纯 C++ 数据结构，不依赖 cmark-gfm
- 测试: test_MarkdownAst
- Commit: `feat: 实现MarkdownAst节点包装`

### Task 3: MarkdownParser — cmark-gfm 封装
- parse(QString) → AstNodePtr
- 注册 GFM 扩展（table, strikethrough, tasklist）
- convertNode 递归转换 cmark_node → AstNode
- 测试: test_MarkdownParser（各 Markdown 元素、行号、GFM）
- Commit: `feat: 实现MarkdownParser cmark-gfm封装`

### Task 4: ParseScheduler — 防抖线程调度
- 连接 Document::textChanged，30ms 防抖
- ParseWorker 在 QThread 中执行解析
- editSeq 序号丢弃过期结果
- astReady(shared_ptr<AstNode>) 信号
- Commit: `feat: 实现ParseScheduler防抖线程调度`

### Task 5: PreviewLayout — 布局引擎
- AST → LayoutBlock 树
- 各块类型布局：Paragraph（InlineRun + 折行）、Heading、CodeBlock、BlockQuote、List、Table、Image、ThematicBreak
- sourceLineToY / yToSourceLine（滚动同步用）
- Commit: `feat: 实现PreviewLayout布局引擎`

### Task 6: PreviewPainter + PreviewBlockCache + ImageCache + CodeBlockRenderer
- PreviewPainter 绘制 LayoutBlock 树，按 spec 规格渲染各元素
- PreviewBlockCache LRU 4MB 块级缓存
- ImageCache LRU 20MB 异步图片加载（本地+网络）
- CodeBlockRenderer 简单关键字高亮
- Commit: `feat: 实现预览渲染和缓存系统`

### Task 7: PreviewWidget — 预览主控件
- QAbstractScrollArea，接收 AST 并渲染
- updateAst() slot
- scrollToSourceLine() / topSourceLine() 滚动同步接口
- Commit: `feat: 实现PreviewWidget预览控件`

### Task 8: 分屏集成
- main.cpp 用 QSplitter 左右分屏
- ParseScheduler 连接 Document → PreviewWidget
- 更新测试 CMakeLists.txt
- Commit: `feat: 集成分屏编辑预览模式`

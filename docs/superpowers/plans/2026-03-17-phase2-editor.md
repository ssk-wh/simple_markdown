# SimpleMarkdown Phase 2: 自绘编辑器

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现基于 QPainter 的自绘 Markdown 源码编辑器，支持文字输入、光标移动、选区、IME 中文输入、语法高亮、搜索替换。

**Architecture:** EditorWidget（QAbstractScrollArea）作为主控件，内部组合 EditorLayout（行布局引擎）、EditorPainter（绘制）、EditorInput（输入处理）、GutterRenderer（行号）、SyntaxHighlighter（高亮）。Document 模型来自 Phase 1。

**Tech Stack:** C++17, Qt 5.15 (Core + Widgets), CMake, Phase 1 的 easy_core 库

**Spec:** `docs/superpowers/specs/2026-03-17-easy-markdown-design.md` Section 4

---

## 文件结构

```
src/editor/
├── CMakeLists.txt
├── EditorLayout.h / .cpp       # 行布局引擎（QTextLayout 缓存、坐标转换）
├── EditorWidget.h / .cpp       # 主控件（QAbstractScrollArea）
├── EditorPainter.h / .cpp      # 绘制逻辑
├── EditorInput.h / .cpp        # 键盘/鼠标/IME 输入
├── GutterRenderer.h / .cpp     # 行号栏
├── BandBuffer.h / .cpp         # 离屏缓冲（性能优化，后期引入）
├── SyntaxHighlighter.h / .cpp  # Markdown 语法高亮（桩实现）
└── SearchBar.h / .cpp          # 搜索替换栏

app/
├── CMakeLists.txt
└── main.cpp                    # 可运行入口

tests/
├── test_EditorLayout.cpp
└── test_EditorInput.cpp
```

---

### Task 1: CMake 更新 + 最小可运行窗口

**Files:**
- Modify: `CMakeLists.txt` (根)
- Modify: `src/CMakeLists.txt`
- Create: `src/editor/CMakeLists.txt`
- Create: `src/editor/*.h` / `*.cpp` (全部空桩)
- Create: `app/CMakeLists.txt`
- Create: `app/main.cpp`

- [ ] 修改根 CMakeLists.txt: `find_package(Qt5 REQUIRED COMPONENTS Core Widgets)`, 加 `add_subdirectory(app)`
- [ ] 修改 src/CMakeLists.txt: 加 `add_subdirectory(editor)`
- [ ] 创建 src/editor/CMakeLists.txt: easy_editor 静态库，链接 easy_core + Qt5::Widgets
- [ ] 创建所有 editor 空桩头文件和实现文件
- [ ] 创建 app/CMakeLists.txt + main.cpp（EditorWidget 空壳 + QApplication）
- [ ] 验证编译通过，运行显示空白窗口
- [ ] Commit: `chore: 搭建Phase2编辑器模块和可运行窗口`

---

### Task 2: EditorLayout 行布局引擎

**Files:**
- Create: `src/editor/EditorLayout.h`
- Create: `src/editor/EditorLayout.cpp`
- Create: `tests/test_EditorLayout.cpp`

- [ ] 实现 EditorLayout: setFont, rebuild, updateLines, ensureLayout (惰性 QTextLayout)
- [ ] 实现坐标转换: hitTest(QPointF→TextPosition), positionToPoint, cursorRect
- [ ] 实现行几何: lineY, lineHeight, totalHeight, lineAtY, rebuildYCache (前缀和)
- [ ] 实现虚拟化: setVisibleRange, 只为可见行±100行创建 QTextLayout
- [ ] 编写测试: 空文档、单行hitTest、多行lineY/lineAtY、positionToPoint互逆
- [ ] 验证测试通过
- [ ] Commit: `feat: 实现EditorLayout行布局引擎`

---

### Task 3: EditorWidget + EditorPainter 文本绘制

**Files:**
- Modify: `src/editor/EditorWidget.h` / `.cpp`
- Modify: `src/editor/EditorPainter.h` / `.cpp`
- Modify: `app/main.cpp`

- [ ] 实现 EditorWidget: setDocument, paintEvent, resizeEvent, scrollContentsBy
- [ ] 连接 Document::textChanged → onTextChanged → updateLines + viewport()->update()
- [ ] 实现 updateScrollBars: 根据 totalHeight 设置 verticalScrollBar range
- [ ] 实现 EditorPainter::paint: 背景填充 + 遍历可见行 QTextLayout::draw
- [ ] 更新 main.cpp: 加载示例文本或命令行文件
- [ ] 验证: 运行窗口显示文本，可滚动
- [ ] Commit: `feat: 实现EditorWidget基础文本绘制`

---

### Task 4: GutterRenderer 行号栏

**Files:**
- Modify: `src/editor/GutterRenderer.h` / `.cpp`
- Modify: `src/editor/EditorWidget.cpp`

- [ ] 实现 GutterRenderer: calculateWidth (根据行号位数), paint (背景+行号+当前行高亮)
- [ ] 修改 EditorWidget: updateGutterWidth, paintEvent 中先绘制 gutter 再绘制文本
- [ ] 验证: 运行可见行号栏，行号对齐
- [ ] Commit: `feat: 实现行号栏渲染`

---

### Task 5: EditorInput 键盘输入 + 光标

**Files:**
- Modify: `src/editor/EditorInput.h` / `.cpp`
- Modify: `src/editor/EditorWidget.h` / `.cpp`
- Modify: `src/editor/EditorPainter.cpp`
- Create: `tests/test_EditorInput.cpp`

- [ ] 实现 EditorInput: keyPressEvent 分发
- [ ] 实现文字输入: insertText, deleteChar(Backspace/Delete), insertNewLine(自动缩进)
- [ ] 实现光标移动: Left/Right/Up/Down/Home/End/Ctrl+Home/Ctrl+End/PageUp/PageDown
- [ ] 实现快捷键: Ctrl+Z/Y (undo/redo), Ctrl+A (selectAll), Tab/Shift+Tab (缩进)
- [ ] TextPosition↔offset 互转辅助方法
- [ ] 在 EditorPainter 中添加光标绘制 (2px 竖线, QTimer 500ms 闪烁)
- [ ] 在 EditorWidget 中添加 keyPressEvent override + 光标闪烁 timer
- [ ] 编写测试: 插入后光标位置、Backspace、方向键移动
- [ ] 验证: 可打字、光标移动、闪烁可见
- [ ] Commit: `feat: 实现键盘输入和光标移动`

---

### Task 6: 选区绘制 + 鼠标选择

**Files:**
- Modify: `src/editor/EditorPainter.cpp`
- Modify: `src/editor/EditorWidget.h` / `.cpp`
- Modify: `src/editor/EditorInput.cpp`

- [ ] EditorPainter: 选区蓝色背景绘制 (#B5D5FF), 当前行浅色高亮 (#F5F5F5)
- [ ] EditorWidget: mousePressEvent (hitTest定位光标), mouseMoveEvent (拖选), mouseDoubleClickEvent (选词)
- [ ] EditorInput: cut (Ctrl+X), copy (Ctrl+C), paste (Ctrl+V) 实现
- [ ] 验证: 鼠标点击定位、拖选高亮、双击选词、Ctrl+C/V/X
- [ ] Commit: `feat: 实现选区绘制和鼠标选择`

---

### Task 7: IME 中文输入

**Files:**
- Modify: `src/editor/EditorWidget.h` / `.cpp`
- Modify: `src/editor/EditorPainter.cpp`

- [ ] EditorWidget: setAttribute(Qt::WA_InputMethodEnabled)
- [ ] 实现 inputMethodEvent: commitString 插入, preeditString 存储+重绘
- [ ] 实现 inputMethodQuery: ImCursorRectangle, ImSurroundingText, ImCursorPosition
- [ ] EditorPainter: preedit 文本绘制 (下划线样式)
- [ ] 验证: 中文输入法可用，候选窗口跟随光标
- [ ] Commit: `feat: 支持IME中文输入`

---

### Task 8: SyntaxHighlighter Markdown 语法高亮

**Files:**
- Modify: `src/editor/SyntaxHighlighter.h` / `.cpp`
- Modify: `src/editor/EditorLayout.cpp`

- [ ] 实现 SyntaxHighlighter: highlightLine → Token 列表, invalidateFromLine
- [ ] 桩高亮规则 (正则): 标题(粗体深色)、粗体、斜体、行内代码(灰背景)、链接(蓝色)、围栏代码块(绿色)、列表标记
- [ ] 增量高亮: 从变化行开始逐行高亮，State 相同则停止
- [ ] 集成到 EditorLayout: ensureLayout 中调用 highlightLine, Token → QTextLayout::FormatRange
- [ ] 验证: Markdown 标题、代码块、粗体等有不同颜色
- [ ] Commit: `feat: 实现Markdown语法高亮`

---

### Task 9: 滚动优化 + 光标可见性

**Files:**
- Modify: `src/editor/EditorWidget.cpp`

- [ ] 实现 ensureCursorVisible: 输入/移动后自动滚动使光标可见
- [ ] 优化 scrollContentsBy: 小幅滚动用 viewport()->scroll() bitblt
- [ ] 更新虚拟化范围: 滚动时调用 setVisibleRange
- [ ] 水平滚动支持 (不折行模式)
- [ ] 验证: 打字超出可见区域自动滚动，大文件滚动流畅
- [ ] Commit: `feat: 完善滚动和光标可见性`

---

### Task 10: SearchBar 搜索替换

**Files:**
- Modify: `src/editor/SearchBar.h` / `.cpp`
- Modify: `src/editor/EditorWidget.h` / `.cpp`
- Modify: `src/editor/EditorInput.cpp`
- Modify: `src/editor/EditorPainter.cpp`

- [ ] 实现 SearchBar: QLineEdit + 按钮 + 选项，浮动在 viewport 右上角
- [ ] 搜索逻辑: findNext/findPrev (从光标位置搜索，选中匹配)
- [ ] 替换逻辑: replaceNext/replaceAll
- [ ] 支持: 大小写敏感、全词匹配
- [ ] EditorPainter: 搜索匹配高亮 (黄色背景)
- [ ] 快捷键: Ctrl+F(搜索), Ctrl+H(替换), Escape(关闭), F3/Shift+F3(下/上一个)
- [ ] 验证: 搜索高亮、替换功能工作
- [ ] Commit: `feat: 实现搜索替换功能`

---

## 验证方案

每个 Task 完成后：
1. 编译通过: `cmake --build build`
2. 运行 SimpleMarkdown 手动验证功能
3. 相关单元测试通过

Phase 2 整体验收：
- 可打开 Markdown 文件编辑
- 光标移动、选区、复制粘贴正常
- 中文输入法可用
- Markdown 语法高亮
- 搜索替换可用
- 行号栏显示
- 大文件滚动流畅

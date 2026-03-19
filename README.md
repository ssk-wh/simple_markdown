# SimpleMarkdown

轻量、高性能的跨平台 Markdown 编辑器，基于 C++17 / Qt 5.15 全自绘渲染。

## 界面预览

应用采用分屏布局：左侧为 Markdown 源码编辑区（带行号和语法高亮），右侧为实时渲染预览区。顶部支持多 Tab 同时打开多个文件，菜单栏提供文件操作和主题切换。

## 功能特性

### 编辑器
- 基于 QPainter 全自绘文本编辑器，极低资源占用
- 行号显示 + Markdown 语法高亮（标题、粗体、斜体、代码、链接、列表、引用）
- 光标闪烁、选区高亮
- 撤销/重做（Ctrl+Z/Y）、剪切/复制/粘贴（Ctrl+X/C/V）、全选（Ctrl+A）
- Tab 缩进 / Shift+Tab 反缩进、Enter 自动缩进
- 搜索替换（Ctrl+F / Ctrl+H）
- IME 中文输入支持

### 预览
- 基于 QPainter 全自绘 Markdown 预览渲染
- 支持：标题（H1-H6）、段落、粗体、斜体、行内代码、围栏代码块、引用块、有序/无序列表、GFM 表格、本地图片、分割线、链接、删除线
- 编辑后 30ms 防抖自动刷新
- 编辑器-预览滚动同步

### 多 Tab 与文件管理
- 同时打开多个文件，Tab 显示文件名和修改标记
- 新建、打开、保存、另存为
- 拖拽 .md/.markdown/.txt 文件到窗口自动打开
- 最近文件列表（最多 10 个）

### 主题
- 亮色 / 暗色主题，菜单一键切换

## 性能指标

| 指标 | 目标 |
|------|------|
| 冷启动 | < 0.5 秒 |
| 空载内存 | < 30 MB |
| 打字延迟 | < 3 ms |
| 预览更新 | < 80 ms |

## 项目结构

```
simple_markdown/
├── app/                    # 应用入口（main、MainWindow）
├── src/
│   ├── core/               # 核心数据模型（PieceTable、Document、UndoStack、Selection、Theme）
│   ├── editor/             # 自绘编辑器（EditorWidget、布局、绘制、输入、语法高亮、搜索栏）
│   ├── parser/             # Markdown 解析（cmark-gfm 封装、AST、ParseScheduler）
│   ├── preview/            # 自绘预览（PreviewWidget、布局、绘制、图片缓存、代码块渲染）
│   └── sync/               # 编辑器-预览滚动同步
├── tests/                  # 单元测试（Google Test）
├── 3rdparty/               # 第三方库（cmark-gfm、googletest）
├── docs/                   # 项目文档
│   ├── requirements.md     # 需求文档
│   ├── architecture.md     # 架构文档
│   └── build.md            # 构建说明
└── CMakeLists.txt
```

## 构建

### 前置依赖
- C++17 编译器（MSVC 2019+ / GCC 9+ / Clang 10+）
- Qt 5.15
- CMake >= 3.16

### 编译步骤

```bash
git clone --recursive <repo-url>
cd simple_markdown
cmake -S . -B build
cmake --build build --config Release
```

### 运行测试

```bash
cd build
ctest -C Release --output-on-failure
```

详细构建说明参见 [docs/build.md](docs/build.md)。

## 技术栈

- C++17 / Qt 5.15 / CMake
- cmark-gfm（Markdown 解析）
- Google Test（单元测试）

## 文档

- [需求文档](docs/requirements.md)
- [架构文档](docs/architecture.md)
- [构建说明](docs/build.md)

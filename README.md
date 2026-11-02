# SimpleMarkdown

轻量、高性能的跨平台 **Markdown 查看器**（2.0.0 起只读），基于 C++17 / Qt 5 全自绘渲染。没有 `QTextDocument`、没有 `QWebEngineView`；分块 + 虚拟化架构让大文件（50MB+）也能秒开、滚动丝滑、点击目录即时跳转。

> 当前版本：见 [CHANGELOG.md](CHANGELOG.md) 首条目

## 功能特性

### 预览
- QPainter 全自绘 Markdown 预览渲染
- 支持：标题（H1-H6）、段落、粗体、斜体、行内代码、围栏代码块、引用块、有序/无序列表、GFM 表格、本地图片、分割线、链接、删除线
- 大文件：后台读取与索引，首屏与文件大小无关；外部修改后静默刷新并保持阅读位置
- 全文查找（Ctrl+F）、目录面板（万级标题）
- 内容标记功能（黄色荧光笔效果）
- 高 DPI 屏精确适配（1x / 1.25x / 1.5x / 2x）

### 多 Tab 与文件管理
- 同时打开多个文件，Tab 显示文件名和修改标记（*）
- 自定义 Tab 关闭按钮，适配深色/浅色主题
- Tab 右键菜单（关闭当前/其他/左侧/右侧）
- 拖拽 `.md` / `.markdown` / `.txt` 文件到窗口自动打开
- 最近文件列表（显示完整路径）
- 文档外部修改检测，弹窗提示重新加载
- 单实例模式
- 会话状态实时持久化，进程被杀后可恢复光标位置和滚动位置

### 主题与界面
- 亮色 / 暗色主题，支持跟随系统
- 深色模式下所有弹窗标题栏自动跟随
- 目录（TOC）面板导航，点击标题快速跳转
- 多语言支持（中文 / 英文 / 日文）

## 项目结构

```
simple_markdown/
├── src/
│   ├── app/         应用入口（main、MainWindow、ChangelogDialog、ShortcutsDialog）
│   ├── core/        核心（Theme、字体默认值、最近文件、性能探针）
│   ├── viewer/      只读分块查看器（扫描、滚动模型、异步加载、查找、ViewerWidget）
│   ├── parser/      Markdown 解析（cmark-gfm 封装、AST）
│   └── preview/     自绘渲染（布局、绘制、图片缓存、代码块渲染、TOC 面板）
├── specs/           规范（Spec），SDD 核心 — 先改 Spec 再改代码
├── plans/           实施计划，记录"如何从 A 迁移到 B"
├── docs/            人类向文档（构建说明等）
├── 3rdparty/        第三方库（cmark-gfm、googletest）
├── resources/       图标、资源文件
├── translations/    Qt 翻译文件（.ts / .qm）
├── installer/       Windows 安装包配置（NSIS）
├── debian/          Debian 打包配置
├── scripts/         工具脚本
└── tools/           辅助工具
```

## 构建

### 前置依赖

- C++17 编译器（MSVC 2019+ / GCC 9+ / Clang 10+）
- Qt 5.12+（推荐 5.15.x）
- CMake ≥ 3.16

### Windows

```bat
build_on_win.bat release
```

### Linux

```bash
./build_on_linux.sh release
```

### 手动 CMake

```bash
git clone --recursive https://github.com/ssk-wh/simple_markdown.git
cd simple_markdown
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

详细编译、测试、打包、诊断说明见 [docs/构建说明.md](docs/构建说明.md)。

## 打包

**Windows（NSIS 安装包）**

```bat
pack_on_win.bat
```

**Linux（deb 包）**

```bash
./pack_on_linux.sh
```

版本号从 `CHANGELOG.md` 首条目自动提取，Windows 与 Linux 保持一致。

## 开发策略

本项目采用**规范驱动开发（SDD）**：规范（Spec）是第一公民，代码是 Spec 的投影。

- **新功能 / 改动** → 先写/改 `specs/` 下的 Spec，再生成代码和测试
- **Bug 修复** → 先问 Spec 里是否定义过此行为，没有定义则先补 Spec 的不变量（INV）与验收条件（T）
- **源文件头部**注明所遵循的 Spec 路径，**测试用例名**嵌入 Spec 的 T 编号，保证代码与规范双向可追溯

想了解项目目标、模块划分、全局不变量？入口是 [specs/README.md](specs/README.md)。

AI 协作者的工作流入口是 [CLAUDE.md](CLAUDE.md)。

## 技术栈

- C++17 / Qt 5 / CMake
- [cmark-gfm](https://github.com/github/cmark-gfm)（Markdown 解析）
- [Google Test](https://github.com/google/googletest)（单元测试）

## 链接

- [变更日志](CHANGELOG.md)
- [构建说明](docs/构建说明.md)
- [规范目录](specs/README.md)
- [AI 协作入口](CLAUDE.md)

## 许可证

项目主体的许可证待定；第三方依赖（cmark-gfm、Google Test 等）遵循各自许可证，见 `3rdparty/` 下对应目录。

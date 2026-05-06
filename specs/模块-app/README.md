# 模块：app（应用层）

## 职责

应用入口、主窗口、多 Tab、菜单栏、文件操作、拖拽、单实例、会话恢复、崩溃处理等应用级能力。

## 对应源码

`src/app/`

## Spec 清单

| 编号 | 标题 | 状态 | 对应源文件 |
|------|------|------|-----------|
| 01 | 应用入口 | draft | `main.cpp` |
| 02 | 主窗口与多 Tab | draft | `MainWindow.h/cpp` |
| 03 | 文件操作与拖拽 | draft | `MainWindow.h/cpp` |
| 04 | 窗口焦点管理 | draft | `MainWindow.h/cpp` |
| 05 | 单实例模式 | draft | `main.cpp` |
| 06 | 会话恢复 | draft | `MainWindow.h/cpp` |
| 07 | 快捷键弹窗 | draft | `ShortcutsDialog.h/cpp`, `MainWindow.cpp` |
| 08 | 更新日志弹窗 | draft | `ChangelogDialog.h/cpp` |
| 09 | 崩溃处理 | draft | `main.cpp` |
| 10 | 菜单栏样式 | draft | `MainWindow.cpp` |
| 11 | 演示模式 | draft | `MainWindow.h/cpp`, `PreviewWidget.cpp` |
| 12 | 主题插件系统 | draft | `Theme.h/cpp`, `ThemeLoader.h/cpp`, `MainWindow.cpp` |
| 13 | 编辑/预览分隔条吸附刻度 | draft | `SnapSplitter.h/cpp`, `MainWindow.cpp` |
| 14 | 自动保存 | draft | `MainWindow.h/cpp` |
| 15 | 状态栏布局 | draft | `MainWindow.h/cpp` |
| 16 | 崩溃报告收集 | draft | `main.cpp`, `MainWindow.h/cpp` |
| 17 | 性能监控 | draft | `core/PerfProbe.h`, `main.cpp`, `MainWindow.cpp` + 埋点源 |
| 18 | 区域卡片化 | aborted | —（已中止，见 plan/抛弃CSS改全自绘） |
| 19 | Linux 深色主题检测 | draft | `MainWindow.cpp::isSystemDarkMode` |
| 21 | 启动窗口几何 | draft | `MainWindow.cpp` ctor + loadSettings/saveSettings, `main.cpp` |
| 22 | 空白引导页 | draft | `WelcomePanel.h/cpp`, `MainWindow.cpp` (restoreSession / onCloseTab / updateEmptyState) |

## 依赖关系

```
main.cpp
  ├─ QApplication 初始化
  ├─ 单实例检测（QLocalSocket/QLocalServer）
  ├─ 崩溃处理器（Windows: dbghelp minidump）
  ├─ 翻译器加载（QTranslator）
  └─ MainWindow 创建

MainWindow
  ├─ QMenuBar
  ├─ QTabWidget
  │    └─ Tab 内容 = EditorWidget + PreviewWidget + TocPanel + ScrollSync
  ├─ core::RecentFiles
  └─ 会话状态持久化
```

## 性能预算

| 操作 | 目标 |
|------|------|
| 冷启动到首窗口 | < 500 ms |
| 打开 Tab | < 100 ms |
| 切换 Tab | < 50 ms |

## 全局约束

- 单实例模式下，第二个进程必须把命令行参数转发给已有进程并退出
- 文件打开后**必须**调用 `raise()` + `activateWindow()`，否则窗口可能在后台
- 会话持久化必须在主线程 idle 时写入，避免卡顿
- 崩溃处理器必须生成足够信息的 minidump，便于事后分析
- **[INV-RELOAD-DIALOG-DEDUP]** 任意 Tab 在任意时刻最多只能存在一个「文件被外部修改，是否重新加载？」对话框；该对话框打开期间发生的额外 `fileChanged` 信号只更新内部 `pendingReload` 标志，不再叠加新对话框。用户对当前对话框给出响应后，新一轮外部修改才能再触发新对话框。删除提示（File Deleted）走另一个路径，但同样遵循"每 Tab 至多一个外部状态对话框"的精神。
- **[INV-WIN-VERSIONINFO]** Windows 构建产物 `SimpleMarkdown.exe` 必须包含 `VS_VERSION_INFO` 资源块，至少填充 `FileDescription` / `ProductName` / `CompanyName` / `FileVersion` / `ProductVersion` / `OriginalFilename` 字段；版本号由 CMake 从 `CHANGELOG.md` 头条自动提取并通过 `configure_file` 注入 `app.rc.in`，禁止在 .rc 中硬编码版本字面量。同时提供英文（040904B0）和中文（080404B0）两套 `StringFileInfo` 块。缺失任一必填字段时，Windows 任务管理器和 Explorer 属性页会退化为显示裸文件名，破坏专业品质。
- **[INV-PANEL-WIDTH-DRAG-CAP]** 用户拖拽 `m_mainSplitter` 时，左侧资源管理器（index 0）和右侧目录面板（最后一项）的宽度上限均为**窗口所在屏幕** `availableGeometry().width() / 4`；超过则在 `splitterMoved` 信号回调中实时夹紧。"所在屏幕"用窗口几何中心点 `screenAt(mapToGlobal(rect().center()))` 决定，跨屏移动后按新屏幕重新计算。持久化的旧宽度若超过当前屏幕 1/4，在 `showEvent` 恢复路径中同样被夹紧。本约束**优先于** `INV-TOC-WIDTH-USER-OVERRIDE` 和 `INV-LP-WIDTH-USER`：用户拖拽不再无上限，但默认建议宽度（1/8 / 1/5 自适应）保持不变；用户主动选择的更宽空间最大可达 1/4。

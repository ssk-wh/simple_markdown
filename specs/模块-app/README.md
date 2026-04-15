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

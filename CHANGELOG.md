# Changelog

All notable changes to this project will be documented in this file.

## [0.2.4] - 2026-04-14

### Added
- **首次启动欢迎页**：首次启动弹窗展示项目定位 + 7 条核心特性 + 快捷键提示；
  菜单 帮助 → 重新显示欢迎页 可手动重新打开
- **Tab 右键"打开所在目录"**：Windows 下用 `explorer /select` 高亮该文件，Linux 下打开包含目录
- **文档统计信息弹窗**：视图菜单 "Document Statistics..." 显示当前文档的
  字数/字符数/行数/段落数/标题分级数/代码块数/图片数/链接数/表格数/引用块数/阅读时间
- **一键打包**：`pack_on_win.bat` / `pack_on_linux.sh` 自动先调用 `build_on_*` release，
  无需先手动 build。Build 失败立即终止并返回非零退出码。
- **演示模式（Presentation Mode）**：F11 全屏化预览区（而非原来的编辑区），
  菜单项 "Focus Mode" 改名为 "Presentation Mode" / "演示模式"
- **预览区链接 Ctrl+点击跳转**：Markdown 中的链接按住 Ctrl+左键点击可直接打开
  - 本地 `.md/.markdown/.txt` → 在新 Tab 打开
  - `http/https/mailto/ftp` → 系统默认应用
  - 其他文档类型 → 系统默认应用
  - `.exe/.bat/.sh` 等可执行文件 → 安全拒绝并提示
  - 文件不存在 → 友好提示不崩溃
  - 悬停在链接上时光标变为 PointingHandCursor

### Changed
- 搜索高亮改为亮黄色（浅色 alpha 128→220，深色从暗棕 #613214 改为金黄 rgba(218,165,32,220)），搜索结果视觉上更显眼
- 菜单项 checkable indicator 与文字间距：padding-left 从 12px 调到 32px，✓ / radio button 不再紧贴文字

### Fixed
- 预览区 Ctrl+滚轮缩放对部分元素无效：代码块、列表序号、表格单元格、图片占位符等
  11 处硬编码 QFont 全部改为从 `PreviewLayout::baseFont()/monoFont()` 派生
  （新增 INV-8 / INV-9：PreviewLayout::setFont 同步 m_monoFont + 新增 getter）
- 拖拽文件到编辑区：支持无后缀纯文本文件（TODO/LICENSE/README 等，通过前 4KB sniff
  判定），拒绝明确二进制扩展（.exe/.zip/.pdf/...），不支持时统一弹窗列出而非静默丢弃
- 首次启动欢迎对话框在中文 UI 下仍显示英文：根因是 CMake 的 `qt5_create_translation` 把 .qm
  生成到构建目录，而 `resources.qrc` 引用的是源码 `translations/` 下的旧 .qm，导致新翻译未被
  打进资源。修复通过 `set_source_files_properties(... OUTPUT_LOCATION)` 让 lrelease 直接输出到
  源码 translations/，每次 build 自动刷新；同步补 `specs/横切关注点/60-国际化.md` 的 INV-1.1
  与新 UI 字符串 checklist，避免第三次翻车

## [0.2.3] - 2026-04-13

### Added
- 新增快捷键速查弹窗（ShortcutsDialog），列出常用编辑、预览、导航快捷键

### Changed
- 废弃 TODO 文件，所有工作项迁入 `plans/`；新增"极简 Plan 模板"适用于小改动；项目级 CLAUDE.md override 全局 TODO 规则
- 项目转为规范驱动开发（SDD）：所有改动先写/改 `specs/` 下的 Spec 再生成代码
- 新增 `specs/` 目录承载规范（产品愿景、系统架构、约束与不变量、模块 Spec、横切关注点）
- 新增 `plans/` 目录承载实施计划，原 `docs/superpowers/` 历史文档迁入 `plans/归档/`
- `docs/` 瘦身为人类向文档：`build.md` 合并打包诊断内容后更名为 `构建说明.md`
- `CLAUDE.md` 从 437 行精简到约 200 行，重定位为 SDD 工作流入口 + 索引
- 旧的 `docs/architecture.md` / `requirements.md` / `高DPI适配指南.md` 内容迁入 `specs/` 后删除
- 编辑器与预览基础字号统一从中心化常量派生（FontDefaults.h），避免两侧字号不一致
- README 整体重写，聚焦项目定位、特性与快速上手
- Spec 中的 C++ 源文件编码规则从"UTF-8 BOM"修正为"UTF-8 无 BOM"，对齐项目现状
- 文档中的 `<repo-url>` 占位符替换为实际的 GitHub 仓库地址

### Fixed
- 关于弹窗中部分英文文案未本地化为中文
- ShortcutsDialog 在深色主题下背景、文字、分隔线颜色未跟随主题
- ShortcutsDialog 表格 viewport（QAbstractScrollArea 内容区）和表头（QHeaderView）
  在深色主题下仍是白底，用 QPalette + stylesheet 混合方案彻底覆盖
- ShortcutsDialog 部分分组标题和快捷键描述未走 tr() 导致中文系统下显示为英文
  （已审计：当前所有用户文本均已 tr() 包装且翻译条目齐全）
- 编辑器与预览基础字号视觉对齐：等宽字体（Consolas）vs 比例字体（Segoe UI）在同
  pointSize 下视觉差异显著，新增 `balanceEditorFontSize()` 基于 `xHeight` 度量
  在 ±2pt 窗口内动态补偿编辑器字号；Spec 80 INV-2/INV-6 修订为"视觉对齐"，
  新增 INV-10 强制走补偿函数
- CI Linux 构建缺少 `Qt5LinguistTools` 导致 i18n 引入后 CMake configure 失败，
  补 apt 包 `qttools5-dev` / `qttools5-dev-tools`

## [0.2.2] - 2026-04-08

### Changed
- 更新日志弹窗改用项目自身的 Markdown 渲染引擎，与主预览区域效果一致
- 更新日志弹窗自动跟随当前主题（深色/浅色）

### Fixed
- 默认行间距从 1.0 改为 1.5，提升阅读体验

## [0.2.1] - 2026-04-07

### Fixed
- CI 打包缺少 Qt5PrintSupport.dll 导致程序无法启动
- CI 版本号未从 CHANGELOG.md 提取，与本地打包不一致
- 滚轮缩放字体后右侧预览未及时刷新布局
- 深色模式下编辑器和预览区域有白色边框
- 左右侧区域字体大小不一致（预览基础字号 10pt → 12pt）

## [0.2.0] - 2026-04-01

### Added
- 文档外部修改检测，弹窗提示是否重新加载
- Tab 页自定义关闭按钮，适配深色/浅色主题
- 文档修改标记：编辑后 Tab 标题前显示 * 号，保存后消失
- 深色模式下所有弹窗标题栏自动跟随深色主题
- Recent Files 菜单显示文件完整路径
- 单实例模式，防止重复打开
- TOC 面板重构为右侧独立面板
- Markdown 格式化快捷键
- Tab 页右键菜单（关闭当前/其他/左侧/右侧）
- 会话状态实时持久化，进程被杀后可恢复

### Fixed
- Tab 右键菜单适配深色模式
- 去掉菜单栏与 Tab 栏之间的多余分隔线
- WordWrap 关闭后编辑器内容消失
- 深色模式下菜单栏/Tab 栏/滚动条颜色统一
- 表格单元格复制内容错位
- 目录面板高度自适应内容，消除下方空白
- H6 标题字体大小略大于正文
- 列表/引用块渲染坐标双倍偏移及 DPI 切屏空白
- 列表项序号与文字对齐

## [0.1.0] - 2026-03-30

### Added
- 预览区内容标记功能（黄色荧光笔效果），支持清除标记
- 目录（TOC）面板导航，快速跳转到标题位置
- TOC 条目中显示被标记内容所在的标题
- 主题自动跟随系统深色/浅色模式设置
- Tab 页标签顺序持久化存储

### Fixed
- 修复代码块和普通文本选区坐标偏差问题
- 修复 DPI 切换时行间距异常增大的问题
- 修复深色主题下内联代码前后空白显示不完整
- 修复鼠标选中文本无高亮色显示
- 修复选区绘制优先级导致文本被覆盖

### Changed
- 双向滚动同步算法优化，减少卡顿
- 设置持久化改进，支持更多自定义选项
- 退出时确认对话框优化

### Improved
- 字符级精确换行支持，改善长行显示
- 表格行高自适应渲染
- DPI 缩放适配性增强
- 预览区文本选择精确定位

---

## [0.0.1] - 2026-01-01

### Added
- 初始版本发布
- 跨平台支持（Windows / Linux）
- 双面板编辑器（编辑 + 预览）
- Markdown 实时渲染
- 代码高亮支持
- 双向滚动同步
- 查找和替换功能
- 明亮和深色主题
- 会话恢复功能
- 最近文件列表

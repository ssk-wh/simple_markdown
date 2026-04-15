# Changelog

All notable changes to this project will be documented in this file.

## [0.2.5] - 2026-04-15

### Fixed
- **TOC DocInfoCard 与折叠按钮未国际化**：新增的 `Document Info` / `Words: %1` / `Chars: %1 (no space %2)` /
  `Lines: %1` / `Size: %1` / `Modified: %1` / `Expand` / `Collapse` / `just now` / `%1 min ago` /
  `%1 hr ago` / `%1 day(s) ago` 共 16 条字符串未翻译，zh_CN 下显示英文。lupdate 扫描 + 补齐 .ts +
  lrelease 生成 .qm 全链路处理；`TocPanel` context 内与 `MainWindow` 同文字符串（如 Words/Lines）
  单独翻译避免 "Same-text heuristic" 残留 `type=unfinished`。
  Reference: feedback memory 「新增 UI 必须走完 i18n 全链路」

### Added
- **TOC 视觉对齐薄荷清新 mock**：TocPanel 条目按 mock_e_mint_breeze.html 设计重做：
  - 每条目左侧绘制 bullet 圆点（lvl1 `6×6`、lvl≥2 `5×5`），active 态 bullet 变柠檬黄 + 半透明光晕
  - hover 只改背景色不改字色（Spec T-VISUAL-2）
  - active 态字色加深 + font-weight 600 + active 底色
  - 条目 border-radius 7px、行间距 gap 2px
  - 缩进阶梯 `12px/级`
  - bullet icon 按 devicePixelRatio 高分辨率绘制，高 DPI 屏幕不模糊
- **TOC 下方文档信息卡**：TocPanel 底部常驻文档摘要卡片（Document Info），展开显示字数/字符/行数/大小/修改时间，折叠显示一行摘要。
  - 数据复用 `updateStatusBarStats` 已算好的 wordCount/charCount/lineCount，不重复 parse markdown（INV-TOC-DOCCARD-NO-REPARSE）
  - 折叠态持久化到 `toc/docCardCollapsed`，重开应用保持（INV-TOC-DOCCARD-COLLAPSE-PERSIST）
  - 颜色全部从 Theme 派生
  - frontmatter title/tags v2 扩展（当前不显示，字段已在 DocInfo struct 中预留）
- **TOC 宽度自适应**：目录面板宽度按标题内容动态计算，不再固定 220px。
  - 公式：`max(文本宽 + 缩进) + padding + bullet + scrollbar + margin`，夹在 `[120px, 屏幕 1/5]`
  - 短标题文档自动收窄、长标题扩张到完整显示（无省略号）
  - 折叠后的子节点不参与宽度计算
  - 用户手动拖拽分隔条后切换为手动模式，不再被 setEntries 覆盖（INV-TOC-WIDTH-USER-OVERRIDE）
  - 窗口 resize 夹紧到屏幕 1/5 上限（INV-TOC-WIDTH-MAX）
  - 高 DPI 通过 `QFontMetricsF(font, paintDevice)` 测算，遵循 INV-DPI-METRICS
- **TOC 节点折叠**：深层文档 TOC 支持折叠父节点以隐藏后代，减少浏览干扰。
  - parent 节点左侧显示 `−` / `+` 切换按钮（叶子节点无按钮，保持对齐）
  - 折叠状态按文件路径 MD5 持久化到 `toc/collapse/<hash>`，重开文件自动恢复
  - TocPanel 获得键盘焦点后：`←` 折叠当前 / 跳父、`→` 展开当前、`Enter` 跳转标题行
  - 折叠子节点自动从 `preferredWidth` 公式中排除（T-COLLAPSE-5）
  - Spec: specs/模块-preview/07-TOC面板.md INV-TOC-COLLAPSE；
    新增 `tests/preview/TocPanelTest.cpp` 8 个单元测试（T-COLLAPSE-1/5, T-WIDTH-1/2）

### Changed
- **TOC 面板垂直对齐编辑/预览区**：Tab 栏横跨整窗宽度，TOC 顶端与编辑/预览区顶端对齐，不再占据 Tab 栏所在行。
  架构改动：`MainWindow` 拆 `QTabWidget` 为 `QTabBar + QStackedWidget`，central widget 换成
  `QVBoxLayout(m_tabBar + m_mainSplitter(m_contentStack | m_tocPanel))`。信号、tab 拖拽重排、
  演示模式 tabBar 显隐、Splitter 状态持久化均一致迁移；showEvent 里加 sanity clamp 防止
  旧 splitter state 把 TOC 宽度挤到 0。Spec: specs/模块-preview/07-TOC面板.md INV-TOC-VALIGN

## [0.2.4] - 2026-04-14

### Added
- Tab 栏「+」新建按钮：紧贴最后一个 Tab 右侧（Chrome/Edge 风格），随 Tab 数量水平移动；
  自定义 `TabBarWithAdd` 子类实现 hover 态与点击 hit-test，点击等价 Ctrl+N
- 主题 schema 新增行内代码 / 代码块独立字段 `preview.inline_code_*` /
  `preview.code_block_*`，默认 fallback 到通用 `preview.code_*`，向后兼容

### Fixed
- 弹窗按钮跨主题 accent 色：`applyTheme` 非深色分支原本完全没有 QDialog/QMessageBox
  stylesheet，6 款浅色主题下弹窗按钮全是原生灰白不体现主题。现在补齐弹窗样式并让按钮
  `:hover` / `:pressed` / `:default` 使用 `theme.accentColor`；深色分支同样升级
  （保留 Spec INV-4 深色底色统一，只在 accent 上体现差异）
- Session 恢复的 Tab 不跟随当前主题：restoreSession 结束时重新 applyTheme 一次，
  清掉 Preview 块缓存中被固化的默认浅色
- menuBar 与 TabBar 背景色相同时视觉融成一块：QMenuBar 加 1px 底边分割线
  （色取 theme.editorGutterLine），深色/浅色主题下都生效
- 预览区 frontmatter 块无法框选/复制：为整个 frontmatter block 注册 TextSegment
  （粗粒度整体可选），extractBlockText 补 Frontmatter 分支输出原始 YAML rawText；
  加整块选区高亮视觉反馈。遵循 Spec INV-13
- Theme 子菜单中文缺失："主题 / 打开主题目录 / 重新扫描主题 / 字符 / 专注模式 / 退出专注模式" 补充 zh_CN 翻译，lrelease 生成 .qm 并打包进 qrc
- 深色主题外壳不跟主题：`MainWindow::applyTheme` dark 分支 stylesheet 数据化，menuBar/TabBar/StatusBar/Splitter/ScrollBar 从 Theme 字段派生；深夜极光等深色主题的外壳跟随主题（保留 QDialog/QMessageBox 硬编码以满足 Spec INV-4）
- TocPanel 不跟主题：`setTheme` 与 `buildList` 移除硬编码 `QColor(37,37,38)` 等数字，改为从 Theme 派生（previewBg/previewFg/accentColor 等）
- 预览区代码块字号偏小：`kMonoDelta` 由 `-3` 改为 `0`，预览代码块字号与编辑器代码字号视觉等价
- 多主题代码块配色去反色：冰霜蓝白/琥珀桃暖/墨禅单色三个浅色主题的代码块改为浅底深字，与整体色温一致
- 主题切换后编辑器代码块/行内代码出现残留"行背景"色：`SyntaxHighlighter` 的
  行缓存 `m_cache` 里 `HighlightToken::format` 是 `QTextCharFormat` value copy，
  把旧主题的背景色固化进了 token；主题切换时 cache 不 invalidate，旧 token 被复用
  导致在新主题背景上看到旧主题的浅色/深色 block。`setTheme` 里增加 `m_cache.clear()`
  修复。回归测试：`tests/editor/SyntaxHighlighterThemeCacheTest.cpp`
- `Theme::applyFrontmatterColors` 无条件覆盖 TOML 里显式声明的 frontmatter_* 字段，
  导致反色代码块主题（如 Monochrome Zen，previewCodeFg 是浅色）的 frontmatter value
  被强制设成浅色，在浅背景上不可见。改为"TOML 未显式声明时才派生"。

### Added
- **主题插件系统（Theme Plugin System）**：主题从硬编码升级为 TOML 配置文件驱动
  - 新增 `core::ThemeLoader`：零外部依赖的 TOML 子集解析器（支持 `[section]` 嵌套 table、
    `key = value`（字符串/数字/布尔）、`#` 注释、`#RGB/#RRGGBB/#RRGGBBAA/rgb()/rgba()` 色值）
  - **内置 6 款新主题**（display name 全中文）：
    - iOS 26 Liquid Glass 组：**冰霜蓝白**（arctic-frost）、**琥珀桃暖**（sunset-haze）、**深夜极光**（midnight-aurora）
    - 清新 / 极简组：**纸面极简**（paper-mist，GitHub 风）、**薄荷清新**（mint-breeze，日系）、**墨禅单色**（monochrome-zen，宣纸墨书）
  - 内置 light / dark 主题也走同一 loader（消除硬编码双重真相）
  - 菜单 视图 → Theme 动态列出所有已发现主题 + "打开主题目录" / "重新扫描主题"
  - 用户自定义主题：放到 `%APPDATA%/SimpleMarkdown/themes/*.toml` 即可被菜单扫到
  - `applyTheme` 重构：非深色主题的 menuBar/TabBar/StatusBar/Splitter/ScrollBar stylesheet
    从 Theme 字段动态派生，所有主题切换时外壳一起变色
  - 11 个单元测试覆盖解析正例/反例/色值格式/继承（TODO V2）/8 款内置资源加载/serialize round-trip
  - Spec: specs/模块-app/12-主题插件系统.md、specs/横切关注点/30-主题系统.md INV-6
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
- **预览区 Frontmatter 渲染**：文档头部 YAML 元数据（`---` 包裹的 key/value 块）识别为带
  圆角背景的 2 列表格，与水平分割线 (ThematicBreak) 不冲突。key 等宽字体 + accent 色，
  value 与代码块同色；背景/边框由系统强调色与预览背景按浅/深色主题差异化混合得到。
  Spec: specs/模块-preview/10-Frontmatter渲染.md（19 条 T 编号，12 个 parser 单元测试全过）

### Changed
- 搜索高亮改为亮黄色（浅色 alpha 128→220，深色从暗棕 #613214 改为金黄 rgba(218,165,32,220)），搜索结果视觉上更显眼
- 菜单项 checkable indicator 与文字间距：padding-left 从 12px 调到 32px，✓ / radio button 不再紧贴文字

### Fixed
- Tab 右键菜单"Close Others / Close to the Left / Close to the Right"按上下文禁用无可操作对象的项（只 1 个 tab、最左、最右时对应项置灰）
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
- 行内代码（反引号包围片段）垂直基线漂移：`paintInlineRuns` 原按每个 run 自身 `fm.ascent()`
  定位 drawText，导致小字号 run（inline code 字号为正文 0.9）基线上移；改为以行主字体
  ascent 作为统一 `lineAscent`，所有 run 共享基线（新增 INV-10 / T-10）

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

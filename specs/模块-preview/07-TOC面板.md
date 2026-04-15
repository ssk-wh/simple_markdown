---
id: 模块-preview/07-TOC面板
status: draft
owners: [@pcfan]
code: [src/preview/TocPanel.h, src/preview/TocPanel.cpp, src/app/MainWindow.cpp]
tests: [tests/preview/TocPanelTest.cpp]
depends:
  - specs/模块-preview/01-预览主控件.md
  - specs/横切关注点/30-主题系统.md
  - specs/横切关注点/40-高DPI适配.md
  - specs/模块-app/12-主题插件系统.md
last_reviewed: 2026-04-14
---

# TOC 面板（Table of Contents）

## 1. 目的

在主窗口右侧提供文档目录导航。从 `MarkdownAst` 抽取标题节点，按层级展示；点击条目跳转到预览区对应位置；当前滚动位置对应的标题呈 active 态。面板同时承载可选的「文档信息卡」（字数/时间/frontmatter 等）。

## 2. 输入 / 输出

- **输入**：
  - `setEntries(QVector<TocEntry>)`：由 `PreviewWidget::tocEntriesChanged` 信号驱动
  - `setHighlightedEntries(QSet<int>)`：当前滚动位置对应的 active 条目集合
  - `setTheme(Theme)`：主题切换
  - `setDocumentInfo(DocInfo)`（V1 预留）：信息卡数据
- **输出**：
  - `headingClicked(int sourceLine)`：用户点击条目
  - `preferredWidthChanged(int px)`：宽度自适应信号
  - `collapseStateChanged(QString fileKey, QSet<int> collapsed)`：折叠状态变更（持久化）

## 3. 行为契约

### [INV-TOC-VALIGN] TOC 顶端垂直对齐编辑/预览区

TOC 面板顶端必须与编辑/预览区顶端对齐，**不得占据 Tab 栏所在行**。

- 实现上，`MainWindow` 的 central widget 为 `QVBoxLayout(tabBar + mainSplitter)`；`mainSplitter` 的左右两栏分别是 `QStackedWidget`（content：editor|preview）和 `TocPanel`
- Tab 栏横跨整个窗口宽度
- TOC 面板顶端 y 坐标 - 编辑器/预览顶端 y 坐标 ≤ 2px（取整容差）

### [INV-TOC-WIDTH-AUTO] TOC 宽度按标题内容自适应

`TocPanel::preferredWidth()` 在每次 `setEntries` 后重新计算，并以 `preferredWidthChanged(int)` 信号通知宿主。宿主（`MainWindow`）未检测到用户手动拖拽 splitter 时，自动套用该宽度。

公式（px @ logicalDpi）：

```
max_width     = floor(screen_available_width / 5)
text_width_i  = fm.horizontalAdvance(entry_i.title) + (entry_i.level-1) * step_indent
content_width = max(text_width_i) + padding_left + padding_right
              + scrollbar_reserved + card_margin
min_width     = 120
final_width   = clamp(content_width, min_width, max_width)
```

- `fm` 必须用 `QFontMetricsF(font, this)` 带 `QPaintDevice` 的 overload（INV-DPI-METRICS）
- `step_indent`、`padding_*`、`card_margin` 派生自字号和 DPI，不得硬编码
- 折叠不可见的子节点**不参与**宽度计算（INV-TOC-COLLAPSE 下定义）

### [INV-TOC-WIDTH-MAX] TOC 宽度不超过屏幕 1/5

窗口 resize 时，若当前 TOC 宽度 > `screen_available_width / 5`，夹紧到上限。

### [INV-TOC-WIDTH-USER-OVERRIDE] 用户手动拖拽后不再自动调整

用户拖拽 `m_mainSplitter` 分隔条视为"用户意图"，此后 `preferredWidthChanged` 只更新 `minimumWidth`，不强制改动 splitter sizes。提供菜单项「重置 TOC 宽度」可恢复自动行为。

### [INV-TOC-COLLAPSE] 支持按层级折叠

TocPanel 内部以 tree model 表示条目，每个有子节点的条目展示 ▸/▾ 折叠箭头。折叠后其后代不渲染（不计入布局）。

- 折叠状态按文件路径 key 持久化（`QSettings` 下 `toc/collapse/<normalized_path>`）
- 未命名/空路径的文件不持久化折叠状态
- 快捷键：TOC 内焦点条目上 `←` 折叠、`→` 展开、`Space`/`Enter` 触发 headingClicked

### [INV-TOC-VISUAL] 视觉规格（默认薄荷清新 mock 基准）

面板采用「card-in-panel」布局：

- 条目自绘（而非 QPushButton + stylesheet），状态：normal / hover / active
- 每条目左侧有 **bullet 圆点**（lvl1 `6×6`、lvl>=2 `5×5`）
- hover 仅改背景色，不改字色
- active 条目：深字色 + 粗体 + `accentSecondary` 色 bullet + 半透明光晕
- 颜色全部从 `Theme` 派生，严禁硬编码（INV-1 唯一数据源）

### [INV-TOC-THEME-ONLY] 颜色来源

TocPanel 所有颜色字段必须来自 `Theme`。若需扩展的语义字段（`tocHoverBg`、`tocActiveFg`、`tocBulletColor`、`tocBulletActive`）在 TOML 中缺省时，必须 fallback 到已有字段（如 `editorCurrentLine` / `previewHeading` / `previewTableBorder` / `accentSecondary`）。

### [INV-TOC-DPI] 所有度量 DPI 感知

- `QFontMetricsF` 必须带 `QPaintDevice*`
- padding/indent/bullet 大小由字号 × ratio 派生，严禁硬编码 px（INV-DPI-NO-HARDCODE）

### [INV-TOC-DOCCARD-NO-REPARSE] 信息卡不重复解析

DocInfoCard 的字数、字符、段落、标题计数必须复用 `MarkdownAst` 或 `Document` 已有缓存，**禁止**二次解析 markdown。

### [INV-TOC-DOCCARD-COLLAPSE-PERSIST] 信息卡折叠态持久化

信息卡折叠/展开状态与 Tab 共享会话键，重开应用保持。

## 4. 算法

### 4.1 preferredWidth 计算

```
w = min_width
for each visible entry (折叠项跳过):
    fm = QFontMetricsF(fontForLevel(entry.level), this)
    tw = ceil(fm.horizontalAdvance(entry.title))
    indent = (entry.level - 1) * step_indent
    w = max(w, tw + indent + padding_left + padding_right)
w = w + scrollbar_reserved + card_margin
return clamp(w, min_width, floor(screen_available_width / 5))
```

### 4.2 active（highlight）判定

由 `PreviewWidget` 根据当前视口顶部 y 反向查找最近一个标题 block，通过 `tocHighlightChanged(QSet<int>)` 信号推送 TocPanel。

### 4.3 折叠展开渲染

Entry tree 拍平成 display list：递归父→子，遇到 `collapsed=true` 的父节点则不添加其后代。`buildList` 基于 display list 布局。

## 5. 数据结构

```cpp
struct TocEntry {
    QString title;
    int level = 1;          // 1..6（h1..h6）
    int sourceLine = 0;
    // V2：折叠支持
    int parentIndex = -1;   // setEntries 时由 level 推导
    bool collapsed = false; // 恢复自持久化
};

struct DocInfo {
    int wordCount = 0;
    int charCount = 0;
    int paragraphCount = 0;
    QMap<int, int> headingCountByLevel;  // level -> count
    QDateTime modifiedAt;
    QDateTime createdAt;
    qint64 sizeBytes = 0;
    QString frontmatterTitle;
    QStringList frontmatterTags;
};
```

## 6. 与其他模块协同

- **PreviewWidget**：`tocEntriesChanged` 信号推送目录；`tocHighlightChanged` 推送 active 索引；点击 `headingClicked` 后调用 `smoothScrollToSourceLine`
- **MainWindow**：
  - 管理全局单例 `TocPanel`（不是每 Tab 一个）
  - 按 `currentTab()` 切换 entries 来源
  - 监听 `preferredWidthChanged` 应用到 `m_mainSplitter->setSizes`
  - 演示模式下 TOC 可见性保存/恢复
- **Theme / ThemeLoader**：视觉色值来源；新增字段 fallback 到旧字段

## 7. 验收条件

### 7.1 垂直对齐（T-VALIGN）

- **T-VALIGN-1** Tab 栏横跨整窗：最大化后 tabBar 右端贴近窗口右边
- **T-VALIGN-2** TOC 顶端对齐内容区：TOC 顶端 y 坐标与编辑/预览顶端 y 坐标差 ≤ 2px
- **T-VALIGN-3** 拖 mainSplitter 分隔条不影响 tabBar
- **T-VALIGN-4** 切 Tab 不影响 TOC 位置
- **T-VALIGN-5** 关闭 Tab 不影响 TOC
- **T-VALIGN-6** 窗口 resize 时 tabBar 随宽度收缩始终横跨
- **T-VALIGN-7** 拖 Tab 顺序，stack 内 widget 顺序同步
- **T-VALIGN-8** 演示模式 tabBar 隐藏/恢复
- **T-VALIGN-9** 主题切换下 tabBar 外观与旧 QTabWidget 视觉一致
- **T-VALIGN-10** Splitter 状态持久化仍可恢复

### 7.2 宽度自适应（T-WIDTH）

- **T-WIDTH-1** 极短标题 TOC 宽度 ≈ min_width(120px)
- **T-WIDTH-2** 极长标题 TOC 宽度扩张到完整显示（无省略号）且不超过屏幕 1/5
- **T-WIDTH-3** 窗口缩小时 TOC 宽度夹紧到屏幕 1/5
- **T-WIDTH-4** 切 Tab 重算宽度
- **T-WIDTH-5** 高 DPI（200%）下宽度测算正确
- **T-WIDTH-6** 用户手动拖宽后不再被 setEntries 覆盖

### 7.3 折叠（T-COLLAPSE）

- **T-COLLAPSE-1** h1..h4 多层嵌套文档可按层级折叠
- **T-COLLAPSE-2** 折叠状态按文件路径持久化
- **T-COLLAPSE-3** 折叠/展开不影响预览区滚动
- **T-COLLAPSE-4** ← 收起、→ 展开、Enter 跳转
- **T-COLLAPSE-5** 折叠项不参与宽度公式计算

### 7.4 视觉（T-VISUAL）

- **T-VISUAL-1** lvl1/lvl2 字号、缩进、bullet、hover、card 圆角与 `tmp/theme_mocks/mock_e_mint_breeze.html` 差距 ≤ 2px
- **T-VISUAL-2** hover 只改背景不改字色
- **T-VISUAL-3** active 含 accentSecondary bullet + 光晕 + 粗字
- **T-VISUAL-4** 8 款内置主题下所有颜色来自 Theme
- **T-VISUAL-5** 175% 高 DPI 下等比例放大

### 7.5 信息卡（T-DOCCARD）

- **T-DOCCARD-1** 带 frontmatter 文件展开信息卡显示 title/tags + 统计
- **T-DOCCARD-2** 编辑实时更新（节流 200ms）
- **T-DOCCARD-3** 折叠态重启后保持
- **T-DOCCARD-4** 未保存文件显示"尚未保存"占位但字数正常

## 8. 非目标

- 不做树形分组排序（按 AST 顺序保留）
- 不在 TOC 内编辑标题
- 不支持同一文档同时在多个 TocPanel 显示（全局单例）

## 9. 演化

- **V1（当前）**：扁平列表 + QPushButton 样式；无 bullet、无折叠、固定宽度 220px
- **V2（本轮改造）**：tree model + 自绘 item + 宽度自适应 + 折叠持久化 + 信息卡 + 薄荷清新视觉
- **V3（未来）**：拖拽重排、TOC 过滤搜索、多文档聚合

## 10. 常见坑

- **QFontMetrics 无 device 参数**：多屏 DPI 切换下测宽偏差 → 必须 `QFontMetricsF(font, this)`
- **QTabWidget::tabBar 无法独立搬家**：拆成 `QTabBar + QStackedWidget` 才能实现 INV-TOC-VALIGN
- **QPushButton stylesheet 不支持 ::before 伪类**：bullet 必须自绘
- **QGraphicsDropShadowEffect 性能开销**：大量条目用 paintEvent 自绘阴影更快
- **setEntries 与 collapse 持久化竞态**：setEntries 必须在 restore collapsed set 之前完成 parentIndex 推导

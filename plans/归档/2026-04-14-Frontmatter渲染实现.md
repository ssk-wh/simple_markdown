---
date: 2026-04-14
status: completed
related_specs: [specs/模块-preview/10-Frontmatter渲染.md]
---

# Frontmatter 预处理剥离 + 渲染

## 背景

`specs/模块-preview/10-Frontmatter渲染.md`(485 行)存在但 `status: draft`,源码零实现。
本次按 Spec §4 的完整接口、§5 伪代码、§7 的 19 条 T 编号验收条件从零实现并激活。

## 实现

- **Theme 扩展**:`accentColor` + 4 个 frontmatter 色字段。`resolveSystemAccent` 从
  `QGuiApplication::palette().color(QPalette::Highlight)` 取,palette 全灰时 fallback `#0078D4`。
  按 §8.5 陷阱建议,浅色主题用 `0.12/0.50` 混合比例,深色主题用 `0.22/0.60`,避免固定 `0.5/0.7`
  在深色下视觉过重
- **AST 扩展**:`AstNodeType::Frontmatter` + `AstNode::frontmatterEntries / frontmatterRawText`
- **Parser §5.2**:`MarkdownParser::extractFrontmatter` 预处理剥离,严格按伪代码实现
  INV-1..INV-7 + INV-14。剥离后记录 `frontmatterLineCount`,在 AST 回填时对所有节点
  `shiftLineNumbers` 补偿,保证 preview→editor 跳转行号正确(§8.9)
- **Layout §5.3**:`PreviewLayout::layoutFrontmatter`,`QFontMetricsF(m_monoFont, m_device)`
  带 device(高 DPI INV-2),列宽按 `qMin(maxKeyW + 2*innerCellPad, innerWidth*0.5)` 实现
  INV-10 50% 上限,value 按字符换行实现 INV-12
- **Painter §4.6**:`paintFrontmatter`,圆角背景 + 边框(半径 `fm.height() * 0.25` 与 CodeBlock 一致),
  key 用 `fm.elidedText(..., Qt::ElideRight, keyDrawW)` 超宽截断

## 验证

1. **单元测试**:`tests/preview/FrontmatterRenderTest.cpp` 12 个 case 全过,覆盖
   T-1/T-2/T-3/T-7/T-8/T-9/T-16/T-18/T-19 + 行号偏移 + BOM 容忍 + rawText 保留
2. **构建**:`build_on_win.bat release` + `ctest`:全过
3. **GUI**:`tmp/fm_test.md` 启动加载,PrintWindow 截图(`tmp/fm_crop2.png`)目视确认:
   - 深色主题下 frontmatter 块带清晰的淡蓝背景 + 蓝色圆角边框
   - 7 行 key-value 两列布局,key 偏 accent 色,value 与 codeFg 一致
   - 注释行 `#` 未渲染(INV-6)
   - 数组字面量 `[@pcfan, @alice]` 原样显示(INV-4)
   - URL `https://example.com` 第一个 `:` 正确拆分(T-19)
   - 文档中段独立 `---` 仍按水平分割线渲染(T-3 未冲突)

## 未覆盖 T(留 future)

- T-14 选区复制返回 raw YAML:需 PreviewWidget 选区处理扩展,非核心渲染
- T-15 不进 TOC:需 TOC 面板侧扩展
- T-4/T-5/T-6/T-10/T-11/T-12/T-13/T-17:GUI/DPI/主题切换集成场景,parser 层测试 +
  layout 层 `qMin` 上限逻辑已间接覆盖;如后续发现视觉异常再补 layout-level test

## 备注

本 plan 由 agent C(独立 worktree,Opus 4.6)在阶段 6/12 后因 API 500 中断,
主 worktree 接力完成阶段 7/8 + 主题色混合比例微调 + GUI 验证 + commit。

---
date: 2026-05-06
status: completed
completed: 2026-05-06
related_specs:
  - specs/模块-preview/10-Frontmatter渲染.md
  - specs/模块-preview/03-绘制管线.md
---

# Frontmatter 多行 YAML 列表项渲染未从最左侧起排

## 背景
用户反馈：在 Markdown 文档 frontmatter 中含多行 YAML 列表时，渲染后**列表项内容没有从最左边起排**，视觉上明显错位。

样例：
```yaml
related_specs:
  - specs/模块-app/13-分隔条吸附刻度.md
```

期望（用户感知层面）：
- key `related_specs:` 显示在 key 列
- value 部分（含 `- specs/...`）应当从一致的起始 X 位置开始，而不是被缩进到键值列的右侧或某个意外的位置

仅记录待办，本次不修复。

## 复现要点（待补充精确步骤）
- 触发条件：含多行 YAML 列表的 frontmatter
  ```yaml
  ---
  related_specs:
    - foo/bar.md
    - baz/qux.md
  ---
  ```
- 现象：第二行起的 `- specs/...` 在预览区**没有从 value 列最左侧开始**
- 不确定项：
  - 是仅多行列表第一项错位、所有项错位，还是首项后缩进累加
  - 是 X 偏移（横向缩进错）还是行换 X 重置错
  - 是否与 frontmatter 列宽自适应（`frontmatterKeyColumnWidth`）相关

## 初步怀疑方向（仅记录，未验证）

定位代码：`src/preview/PreviewPainter.cpp::paintFrontmatter`（line 715+）+ 对应 layout 阶段
`PreviewLayout::layoutFrontmatter`。

关键变量：
- `block.frontmatterEntries`：以 `(key, value)` pair 形式存储
- `block.frontmatterKeyColumnWidth`：key 列宽（layout 阶段计算并写入，INV-10）

可能的根因：

1. **value 字段的多行换行被串成一行**：parser 把多行 YAML 的 value 拼接成一个长字符串，然后渲染时按 wordWrap 自动换行——换行后 X 起点正确为 value 列最左，但**显示为单行而非保留 YAML 列表结构**，让用户感知到"内容错位"
2. **value 多行被保留**但每行 X 起点用了"value 列最左 + 缩进"——导致第二行起被缩进
3. **paintInlineRuns 用 `\n` 重置 X**（参考 PreviewPainter line 606-610），但 frontmatter 路径可能没把 YAML 多行 value 中的换行符转成 `\n` 节点
4. **frontmatter 是按 key/value 表格形式渲染**，多行 list 在 value 单元格内未做"展开为多项"处理

## 动作（修复时）
- [ ] 准确复现：构造一个 frontmatter 含 `related_specs: [- a, - b, - c]` 多行列表的样例文档，截屏对比预期 vs 实际
- [ ] 读 `src/preview/PreviewPainter.cpp::paintFrontmatter` + `PreviewLayout::layoutFrontmatter`，确认 value 多行 list 走的是哪条分支
- [ ] 在 `specs/模块-preview/10-Frontmatter渲染.md` 中明确多行 YAML 列表的渲染规则（候选 INV：
      "多行列表项每条独立成行，X 起点 = value 列最左侧；列表项目符号 `-` 与文本之间有固定间距"）
- [ ] 实施修复（layout 阶段拆分多行 list 为子项 / 绘制阶段每行重置 X 到 value 列最左）
- [ ] 写测试：对含多行列表的 frontmatter，断言每个列表项的渲染矩形 X 起点 == value 列最左侧
- [ ] 更新 CHANGELOG（用户视角措辞，例：含 YAML 列表的 frontmatter 渲染时列表项现在与值列对齐，不再被多余缩进）

## 验收
- 用户提供的样例 frontmatter 渲染后，列表项 `- specs/模块-app/13-分隔条吸附刻度.md` 的开头 `-` 与 value 列最左侧对齐
- 多行列表（≥3 项）每行的 X 起点一致
- 单行 value（如 `title: foo`）的渲染不受影响
- 高 DPI（125% / 150% / 175%）下复测一致

## 实施结果（2026-05-06）

**根因纠正**——修复时发现初步怀疑方向部分错误：parser **已经把** YAML 多行列表的每个子项
解析成独立的"无 key entry"（如 `("", "- specs/A.md")`），层级在 parser 阶段已展平。
真正的问题不在"layout/paint 没识别 \n"，而是：

- 旧 paint 把所有 entry 的 value 都绘制到 value 列起点（valColX + innerCellPad）
- 无 key entry（YAML 列表子项）于是被视觉上"缩进到 value 列右侧"，与 key 列起点无法对齐
- 用户感知的"specs 部分内容没有从最左边开始"——指的是希望列表子项**回到 key 列最左**

**修复**：
1. `PreviewLayout::layoutFrontmatter`：无 key entry 用 `innerWidth` 字符预算（更宽），
   有 key entry 仍用 `valColW`（保持 key/value 双列布局）
2. `PreviewPainter::paintFrontmatter`：无 key entry 的 value 起绘 X = `innerX + innerCellPad`
   （key 列起点），让多行 YAML 列表子项在 frontmatter 卡片左侧对齐
3. `LayoutBlock` 增 `frontmatterValueLines` 字段——layout 阶段拆好的可视行 paint 直接读取，
   消除两端分别拆行算法走样的风险
4. specs/模块-preview/10-Frontmatter渲染.md `last_reviewed` 同步至 2026-05-06

**新增测试** `tests/preview/FrontmatterRenderTest.cpp::FrontmatterLayoutTest`：
- T_FM_LAYOUT_1: YAML 列表子项各自成为独立无 key entry
- T_FM_LAYOUT_2: 无 key entry 用 fullCharsPerLine 预算
- T_FM_LAYOUT_3: 总行数与 block.height 估算一致

## 备注
- 本项目自身的 plan 文件就是 frontmatter 含多行列表的真实样例（每个 plan 都用 `related_specs: - ...`），用 SimpleMarkdown 打开任意一个 plan 即可触发
- 与 `specs/模块-preview/10-Frontmatter渲染.md` INV-10（列宽截断+ellipsis）+ INV-12（value 按字符换行）相互作用，需要小心区分"列宽截断"与"YAML 列表展开"两种不同需求

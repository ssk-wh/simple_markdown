---
title: 代码块超长行不自动换行
status: completed
created: 2026-04-27
completed: 2026-04-27
related_specs:
  - specs/模块-preview/02-布局引擎.md
  - specs/模块-preview/03-绘制管线.md
---

# Bug: 预览区代码块中超出宽度的行不自动换行

## 复现
1. 打开任意 .md 文件
2. 写入一个含超长行的代码块（远超预览区宽度），例如：

````
```bash
echo "this is a very very very very very very very very very long line that must exceed the preview width"
```
````

3. 观察预览区代码块

## 现象
- 长行**直接被裁剪**或溢出到代码块右边界外（依赖 viewport 是否裁剪）
- 即使开启了"视图 → 自动换行"，代码块内部仍不换行（自动换行只作用于正文段落）

## 期望
代码块超长行按可视宽度软换行，与正文段落 wordWrap 行为一致：
- 在字符边界换行（代码块通常没空格分词点）
- 续行有合理缩进或对齐显示
- 块高度自动撑开容纳所有软行

## 推测根因（待修时再核实）
`src/preview/PreviewLayout.cpp` CodeBlock 分支：
```cpp
int lineCount = qMax(1, block.codeText.count('\n') + (block.codeText.endsWith('\n') ? 0 : 1));
qreal h = lineCount * m_codeLineHeight + 16.0;
block.bounds = QRectF(0, 0, maxWidth, h);
```
`lineCount` 只算物理 `\n` 数量，没把"超出 maxWidth 后的视觉续行"计入；`PreviewPainter::paintCodeBlock` 同样按 `\n` 拆分循环绘制，`drawText` 单行写出后越过右边界。

## 候选修复方向（不预先固化）
- **方案 A：layout 端预测 wrap 行数**：用 `QFontMetricsF::horizontalAdvance` 把每个物理行按 `maxWidth - 2*hPad` 估算软换行数，累加成 `visualLineCount`，块高度 = `visualLineCount * m_codeLineHeight + 16`
- **方案 B：painter 端按字符 wrap 绘制**：仿 `paintInlineRuns` 的 fitCount 切片逻辑，逐字符填充 + 越界换行；layout 必须同步预测，不然块高错位（违反 INV-10/INV-11 同构原则）
- **方案 C：仅水平滚动**（保留现状但裁切干净）：低优先级，仅作为"用户主动关闭代码块换行"开关。需要新增菜单项
- 注意 `CodeBlockRenderer::highlight` 返回 token 序列；wrap 时要保证语法高亮的颜色边界不被错位

## 待补 Spec（修时同步）
- specs/模块-preview/02-布局引擎.md：CodeBlock layout 必须消费 `m_viewportWidth`，新增 INV 描述软换行预测公式
- specs/模块-preview/03-绘制管线.md：paintCodeBlock 的换行算法 + 与 layout 同构

## 优先级
中（视觉破坏明显，但不影响数据；可暂用横向滚动 / 长行手动断行规避）

## 阻塞
无。

---
date: 2026-05-06
status: draft
related_specs:
  - specs/模块-preview/10-Frontmatter渲染.md
  - specs/模块-preview/02-布局引擎.md
  - specs/模块-preview/03-绘制管线.md
---

# Frontmatter 卡片顶部边缘与预览区边界视觉重合

## 背景
用户反馈：当文档以 frontmatter 开头时（这是含 frontmatter 文档的常见情况），
**frontmatter 卡片的上边框紧贴预览区可视区域的最顶部**——两条线**视觉上重合**，
卡片像被切了一刀，没有透气间距。

用户提出两种修法（择一或并用）：
1. **frontmatter 卡片单独往下移 2px**（仅影响这一类 block）
2. **整个预览界面内容往下移 2px**（统一抬高 content 起点，所有首块都受益）

仅记录待办，本次不修复。

## 现状定位（已粗读代码）

预览区绘制管线（`PreviewWidget::paintEvent` + `PreviewPainter::paint`）：

- 内容起点 X 由 `painter.translate(20 - scrollX, 0)` 设置——**X 有 20px padding**，**Y 没有**
- root LayoutBlock 的 children 第一个就是 Frontmatter block（`bounds.y() = 0`）
- `scrollY = 0` 时 drawY 也是 0 → frontmatter 卡片矩形从 viewport 顶端 0 像素开始绘制
- 上方没有任何间距 → 卡片边框与 viewport top 重合

## 候选修法对比

| 方案 | 实现位置 | 影响范围 | 优缺点 |
|------|---------|---------|--------|
| A. frontmatter 单独加上方 margin | `layoutFrontmatter` 给 block 加 top offset，或 paint 时抬 absY | 仅 frontmatter | 改动最小；但段落/标题等首块仍贴顶 |
| B. content 整体加 top padding | `PreviewWidget::paintEvent` 把 `painter.translate(20, 0)` 改成 `painter.translate(20, 2)`（或 4），且 `scrollY` 计算同步保留 | 所有首块 | 统一抬高；但若 padding ≠ scroll 起点偏移会导致滚动错位 |
| C. layout root 加 top margin | `PreviewLayout::buildFromAst` 把 root.bounds.y() 起点设为 N | 所有首块 | 与方案 B 等价但走 layout 层；scrollBar.maximum 自动包含 padding |

**初步建议**：方案 C（layout 层加 top margin）——
- 把"内容上方留白"作为 layout 不变量沉淀到 Spec（INV-PREVIEW-TOP-PAD），统一管理
- 所有首块自动受益（不只是 frontmatter）
- 滚动条范围自动算上 padding，不需要手动调 scrollY 起点

## 动作（修复时）
- [ ] 选择候选方案（推荐 C）；如果只关心 frontmatter 一种 block，方案 A 也行
- [ ] 在 specs/模块-preview/02-布局引擎.md 增 INV-PREVIEW-TOP-PAD（方案 C）或
      specs/模块-preview/10-Frontmatter渲染.md 增 INV-FM-TOP-MARGIN（方案 A）
- [ ] 实施 + 视觉验证（截图对比修复前后）
- [ ] 测试：构造首块为 frontmatter / Heading / Paragraph 的文档，断言 first block 的 bounds.y > 0
- [ ] 检查行为对边界：
  - 滚动到顶时是否有 N px 空白带（应当有，符合修复目的）
  - 滚动条 maximum 是否正确包含 padding
  - print/export 输出（如有 PDF 导出）是否一致
- [ ] 更新 CHANGELOG（用户视角措辞，例：含 frontmatter 的文档预览区顶部新增视觉间距，卡片边缘不再贴住边界）

## 验收
- 含 frontmatter 文档的预览区顶部留出 2-4px 间距，frontmatter 卡片不再贴顶
- 不含 frontmatter 的文档（首块是 Heading / Paragraph）也获得同等顶部间距（如选方案 B/C）
- 高 DPI（125%/150%/175%）下间距按逻辑像素一致，不被缩放
- 滚动到顶部时 viewport 顶部能看到完整间距，滚动到底部时 maximum 范围正确

## 备注
- 像素值 2px 是用户提的最小可见间距；实际可考虑用字号派生（如 `fm.height() * 0.2`），
  让高 DPI 下间距随字体度量缩放。但 2-4px 是普遍可接受的"留白"
- 与 `specs/横切关注点/40-高DPI适配.md` INV-DPI-NO-HARDCODE 冲突——硬编码 2px 是反模式，
  建议字号派生
- 如果方案 C：`PreviewLayout` 的 totalHeight() / sourceLineToY() 等也要同步加 padding 偏移
- 这是**纯视觉间距**的 bug，不影响功能正确性，可与其他 frontmatter / 渲染相关 plan 合并修

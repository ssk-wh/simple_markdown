---
date: 2026-05-06
status: completed
completed: 2026-05-06
related_specs:
  - specs/模块-preview/10-Frontmatter渲染.md   # INV-12 拆行规则
related_plans:
  - plans/归档/2026-05-06-frontmatter多行列表项对齐bug.md   # 同模块刚修过的相邻 bug
---

# Frontmatter 超长 value 换行后仍有部分越出卡片右边框

## 背景
用户反馈：frontmatter 中较长的 value 行（典型例子是含中英文混排 + 注释 `# ...` 的 YAML
列表项），即使按 layout 算法换行后，仍有**部分文本越出 frontmatter 卡片右边框**——
整段没有完全留在卡片内。

样例：
```yaml
related_specs:
  - specs/模块-app/README.md
  - specs/模块-app/02-主窗口与多Tab.md          # 待创建/补充：Tab 休眠规则
```

仅记录待办，本次不修复。

## 根因（已基本定位，待修复时核实）

`PreviewLayout::layoutFrontmatter` 当前使用：

```cpp
const int charsPerLine = qMax(1, qFloor(innerWidth / avgCharW));   // 无 key entry
// ... 按 charsPerLine 字符数切 value
```

**问题**：`avgCharW = fm.averageCharWidth()` 是字体里**所有字符**的平均宽度。
但实际字符宽度差异巨大：

- ASCII 字符 ≈ 字号 × 0.5~0.6
- 中文字符 ≈ 字号 × 1.0
- 中文比 ASCII 宽近 2 倍

按平均宽度估算 charsPerLine：
- 纯 ASCII 行：N 个字符实际宽度 < N × avgCharW → 留出空白（不会越框，但浪费空间）
- 含中文行：N 个字符实际宽度 > N × avgCharW → **越出 innerWidth**（越框！）

用户的样例 `"# 待创建/补充：Tab 休眠规则"` 这种短中文段落，挤在含 ASCII 路径的同一行末尾，
正好踩到这个失算坑。

刚归档的 `frontmatter多行列表项对齐bug` plan 已经把切行逻辑改用 `fullCharsPerLine`（无 key entry
不用 valColW 而用 innerWidth），但**仍是 averageCharWidth 估算**——根因没消除，只是把切的位置
往后挪了一点。

## 动作（修复时）
- [ ] 把 `layoutFrontmatter` 中的 value 字符切分**改为逐字符累加 horizontalAdvance**，
      直到接近 `availableWidth`（与 `PreviewLayout::estimateParagraphHeight` 修复表格越界
      时引入的字符级换行算法同型——参考 specs/模块-preview/02 INV-15）
- [ ] 把这个换行算法抽出来作为 file-static helper（layoutFrontmatter 与 estimateParagraphHeight
      共享），避免双份实现走样
- [ ] paint 端如果要重新做字符级裁剪也按同一算法（**或**继续依赖 layout 阶段拆好的 valueLines）
- [ ] 在 specs/模块-preview/10 增 INV：value 拆行**必须**逐字符级精确换行（含中文场景），
      禁用 averageCharWidth × N 的估算切法
- [ ] 测试：构造一个含中英文混排 + 长 ASCII 路径 + 中文注释的 frontmatter，
      在多个 viewportWidth 下断言每行渲染宽度 ≤ 可用宽度（不越框）
- [ ] 更新 CHANGELOG（用户视角措辞，例：含中英文混排的长 frontmatter 字段在卡片内完整显示，不再溢出）

## 验收
- 用户给的样例 frontmatter（含中英文混排和注释）在所有窗口宽度下完全留在 frontmatter 卡片内
- 纯 ASCII 长行 + 纯中文长行 + 混排长行三种 case 都通过
- 高 DPI（125% / 150% / 175%）下复测一致

## 实施结果（2026-05-06）

**修复**：`PreviewLayout::layoutFrontmatter` 把 charsPerLine 估算改为字符级 horizontalAdvance 累加：

```cpp
auto wrapByPixelWidth = [&fm](const QString& s, qreal availPx) -> QStringList {
    QStringList out;
    qreal acc = 0;
    int lineStart = 0;
    for (int i = 0; i < s.length(); ++i) {
        const qreal w = fm.horizontalAdvance(s[i]);
        if (acc + w > availPx && i > lineStart) {
            out << s.mid(lineStart, i - lineStart);
            lineStart = i;
            acc = w;
        } else {
            acc += w;
        }
    }
    if (lineStart < s.length()) out << s.mid(lineStart);
    return out;
};
```

可用宽度（左右对称内边距）：
- 有 key entry：`valColW - 2 * innerCellPad`
- 无 key entry：`innerWidth - 2 * innerCellPad`

**新增测试** `T_FM_LAYOUT_4_MixedCJK_AsciiNeverOverflowsCardWidth`：
在 5 个不同 viewportWidth（300/400/500/600/800）下，对每个 frontmatterValueLines 行
重新用 `fm.horizontalAdvance(line)` 测量实际渲染宽度，断言 ≤ 可用宽度（0.5px 浮点容差）。
覆盖中英文混排 + ASCII 路径 + 中文注释的真实场景。

**未做**：尚未把字符级换行算法抽到 file-static helper 与 `estimateParagraphHeight` 共享——
两份独立实现暂时各自工作但语义同构；如未来再发现走样可合并。

## 备注
- 这条本质和 plan「2026-05-06-表格单元格内容超出与下方重合」是同根问题——
  「按 averageCharWidth 估算字符填充」假设字符宽度均匀，对中英文混排不成立
- 表格 bug 的修复（INV-15）已经把这个教训沉淀了，但 frontmatter 路径独立写了一份切行算法
  没受惠于那次重写。本次正好把两份算法合并到 file-static helper，根除走样
- 用户之前提的 frontmatter 多行列表对齐 bug 已修（视觉对齐到卡片左侧），本 bug 是修对齐**之后**才暴露的
  ——之前列表项缩在 value 列窄空间里，看着也"超"，但用户对比之下意识到对齐是关键问题；
  对齐修了，下一关注点就变成"右边界"

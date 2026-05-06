---
id: 模块-preview/10-Frontmatter渲染
status: active
owners: [@pcfan]
code: [src/parser/MarkdownParser.cpp, src/parser/MarkdownAst.h, src/preview/PreviewLayout.cpp, src/preview/PreviewPainter.cpp, src/core/Theme.h, src/core/Theme.cpp]
tests: [tests/preview/FrontmatterRenderTest.cpp]
depends: [specs/模块-preview/02-布局引擎.md, specs/模块-preview/03-绘制管线.md, specs/横切关注点/30-主题系统.md, specs/横切关注点/80-字体系统.md, specs/横切关注点/40-高DPI适配.md, specs/模块-parser/README.md]
last_reviewed: 2026-05-06
last_fix: 2026-05-06   # frontmatter value 字符级精确换行
---

# 预览区 Frontmatter 渲染

## 1. 目的

把 Markdown 文档开头常见的 YAML frontmatter（被 `---` 包裹的 key/value 元数据块）识别出来，并在预览区以带背景的"信息表"样式渲染——而不是作为一大坨纯文本或者水平分割线 + 普通段落混合显示。目标是让 Spec/Obsidian/Jekyll/Hugo 风格的文档头在预览区具备可读性和层次感，而不破坏 Markdown 正文的解析。

## 2. 输入 / 输出

### 输入

- 原始 Markdown 文本（`QString`）——尤其是**开头**的 `---` 分隔块
- `PreviewLayout` 的当前字体（`baseFont()` / `monoFont()`）
- 当前 `Theme`（需要新增 `accentColor` / `frontmatterBackground` / `frontmatterBorder` 字段）
- 当前 `QPaintDevice*`（用于字体度量）
- 可视区宽度（用于列宽上限）

### 输出

- 一个新的 `LayoutBlock::Frontmatter` 块（或复用 `CodeBlock` 并带子类型标记，二选一见 §5）
- 块内承载 `std::vector<std::pair<QString, QString>>` 形式的 entries（key/value 都是未经 YAML 解析的原始字符串）
- 通过 `PreviewPainter` 绘制到 QPainter 上：
  - 圆角边框
  - accent + 50% 混合背景
  - n 行 2 列的表格布局（无表头）

## 3. 行为契约

### [INV-1] 识别条件必须严格

只有以下条件**全部**满足才识别为 frontmatter：

1. 文档的**首个非空字符行**（允许前置 BOM，允许行首/行尾空白）整行仅为 `---`
2. 在其之后某一行出现**独立**的 `---` 作为结束标记（同样允许首尾空白）
3. 起止两行之间的内容为 frontmatter 的 YAML 内容

否则按普通内容处理。**严禁**把文档中间的 `---` 误识为 frontmatter 起点。

### [INV-2] 前缀空白容忍

`---` 前后允许可选的 UTF-8 BOM 和首尾空白字符（空格、Tab），但同一行**不得**有其他字符。下面都是合法的结束标记：

```
---
 ---
\t---\t
```

下面**不**是合法的 frontmatter 起止标记（仍按普通 Markdown 处理）：

```
----    (4 个 dash 是 setext h2)
--- x   (后面有其他字符)
```

### [INV-3] 不与水平分割线冲突

一旦文档首个 `---` 被 frontmatter 消费，它连同结束 `---` 一起从送入 cmark-gfm 的原文中剥离。frontmatter 之后位置的 `---` 仍按 `ThematicBreak`（水平分割线）渲染。两者互不影响。

### [INV-4] 解析策略（浅层）

按行解析 `key: value`：

- **Key** = 第 1 次出现的 `:` 之前的子串（去首尾空白）
- **Value** = 第 1 次 `:` 之后到行尾的子串（去首尾空白）
- 不递归解析嵌套 YAML（数组、对象、多行字符串）
- 数组 / 对象字面量原样保留，例如 `owners: [@pcfan, @alice]` 的 value 就是字符串 `[@pcfan, @alice]`
- 多行 value（YAML 的 `|` / `>` / 续行缩进）**不支持**，该行后续缩进行被当作独立行处理（在 MVP 中可忽略或合并为 value 里一个空格分隔的续行，二选一；推荐"独立行当作 key 为空的行"）

### [INV-5] 空行忽略

frontmatter 内部的空行（仅含空白或完全空）**不生成**表格行。

### [INV-6] 注释忽略

以 `#` 开头（去前导空白后）的行视为 YAML 注释，**不生成**表格行。

### [INV-7] 解析失败回退

如果从首行 `---` 开始扫描到文档末尾都没有找到结束 `---`，**整个文档**不按 frontmatter 处理——首行 `---` 退回原样送入 cmark-gfm 解析（通常会变成 `ThematicBreak` 或 setext 标题的底线）。不得把剩余文档全部变成一个 frontmatter 块。

### [INV-8] 渲染样式

整体是一个独立 block，具备：

- **圆角边框**：与 `CodeBlock` 相同的圆角半径（遵守 `横切关注点/80-字体系统.md` INV-5，由字体度量派生，例如 `fm.height() * 0.25`）
- **背景色**：`frontmatterBackground`，由 `accentColor` 与 `previewBackground` 做 **50% RGB 线性混合**得到（见 §5 公式）
- **边框色**：`frontmatterBorder`，由 `accentColor` 与 `previewBackground` 做 **70% accent + 30% bg** 混合得到（比背景更突出）
- **内边距**：上下左右的 padding 与普通代码块保持一致（都由字体度量派生；禁止硬编码像素）

### [INV-9] 字体

- **字体族**使用 `PreviewLayout::monoFont()`（等宽，便于两列纵向对齐）
- **字号**与 `PreviewLayout::baseFont()` 一致（正文字号），**不使用** monoFont 自带的
  `kMonoDelta` 缩减。原因：frontmatter 是文档头部元数据，需要清晰可读；沿用 codeBlock 的
  小字号(9pt vs baseFont 12pt)在混排场景下视觉过淡
- **严禁**硬编码字号（遵守 `模块-preview/03-绘制管线.md` INV-8、`横切关注点/80-字体系统.md` INV-8）
- 字号随 Ctrl+滚轮缩放一起变化（因 baseFont 字号随 `setFont` 同步变化）

实现上：
```cpp
QFont fmFont = m_layout->monoFont();
fmFont.setPointSizeF(m_layout->baseFont().pointSizeF());
```

### [INV-10] 表格列宽规则

- 第一列宽度 = `max(QFontMetricsF(monoFont, device).horizontalAdvance(key) for key in entries) + 2 * cellHPadding`
- **上限**：第一列宽度不得超过可视区（扣除 frontmatter block 左右 padding）的 **50%**
- 若最长 key 的测量宽度超过 50%，第一列**截断**为 50%，过长 key 显示 ellipsis（`...`）
- 第二列宽度 = 剩余可用宽度

### [INV-11] 行高

每一行的行高 = `PreviewLayout::codeLineHeight()`（与普通代码块一致，已经由 monoFont 的度量派生）。

### [INV-12] value 换行

value 过长时按**字符**换行（不是按单词），这样能更严格地保持两列对齐。换行后的后续行左侧留空（第一列不重复 key）。

### [INV-13] 选区与复制

- frontmatter block 整体可选（从起始行到结束行，遵循 `specs/模块-preview/01-预览主控件` 的选区规则）
- 复制到剪贴板的文本是**原始** YAML 格式——包含起止 `---`、原始 key/value 行、原始空行/注释——**不是**渲染后的表格文本
- 这保证用户能复制粘贴回其他 Markdown 编辑器而不丢失元数据

### [INV-14] 无 key 的行

若某非空行既不是注释、又不含 `:`（例如纯字符串 `TODO`），整行作为 value 放入第二列，第一列**留空**。这是为了对异常输入的宽容。

### [INV-15] 与 TOC / Marking 的交互

- frontmatter 不是标题 → **不**进入 TOC 面板
- frontmatter 允许被 marking 高亮（鼠标右键标记）——与代码块一致
- 查找（Ctrl+F）应能搜索到 frontmatter 内的文本

### [INV-16] 与链接点击（Spec 09）的交互（future，非 MVP）

frontmatter 内的字符串 value 如果包含可识别的路径或 URL，**未来**可考虑支持 Ctrl+点击。但 value 是 YAML 字符串字面量（不是 Markdown 链接语法），需要专门的正则识别 + 路径校验，不在 MVP 范围，标为 future/draft。

## 4. 接口

### 4.1 AST 扩展

```cpp
// src/parser/MarkdownAst.h
enum class AstNodeType {
    Document, Paragraph, Heading, CodeBlock, BlockQuote,
    List, Item, Table, TableRow, TableCell,
    Text, Emph, Strong, Link, Image, Code,
    SoftBreak, LineBreak, ThematicBreak,
    HtmlBlock, HtmlInline, Strikethrough,
    Frontmatter,   // ← 新增
};

class AstNode {
public:
    // ... 现有字段 ...

    // Frontmatter 专用
    std::vector<std::pair<QString, QString>> frontmatterEntries;  // 新增
    QString frontmatterRawText;  // 原始文本（用于复制时还原）
};
```

### 4.2 Parser 扩展

```cpp
// src/parser/MarkdownParser.h
class MarkdownParser {
public:
    AstNodePtr parse(const QString& markdown);

private:
    // 新增：尝试从原文剥离 frontmatter；返回是否剥离成功
    // 成功时：outBody 为剥离后的剩余 Markdown 正文，outNode 为 Frontmatter AST 节点
    // 失败时：outBody 保持原样，outNode 为 nullptr
    bool extractFrontmatter(
        const QString& rawMarkdown,
        QString& outBody,
        AstNodePtr& outNode
    );
};
```

### 4.3 LayoutBlock 扩展

```cpp
// src/preview/PreviewLayout.h
struct LayoutBlock {
    enum Type {
        Document, Paragraph, Heading, CodeBlock, BlockQuote,
        List, ListItem, Table, TableRow, TableCell,
        Image, ThematicBreak, HtmlBlock,
        Frontmatter,   // ← 新增
    };

    // Frontmatter 专用
    std::vector<std::pair<QString, QString>> frontmatterEntries;
    qreal frontmatterKeyColumnWidth = 0;  // 由 layout 阶段写入
    QString frontmatterRawText;           // 用于复制
};
```

### 4.4 Theme 扩展

```cpp
// src/core/Theme.h
struct Theme {
    // ... 现有字段 ...

    QColor accentColor;              // 新增：系统强调色（Windows: QPalette::Highlight）
    QColor frontmatterBackground;    // 新增：accent 与 previewBg 50% 混合
    QColor frontmatterBorder;        // 新增：accent 与 previewBg 70/30 混合
    QColor frontmatterKeyForeground; // 新增：第一列 key 文字色（偏强调色）
    QColor frontmatterValueForeground; // 新增：第二列 value 文字色（与 codeFg 一致）
};
```

> 注：本 Spec **只规定这些字段必须存在**，字段的默认值与构造流程由 `specs/横切关注点/30-主题系统.md` 负责扩展。本 Spec 不直接修改 30-主题系统.md。

### 4.5 Layout 扩展

```cpp
// src/preview/PreviewLayout.cpp
LayoutBlock PreviewLayout::layoutFrontmatter(const AstNode* node, qreal maxWidth);
```

职责：
- 遍历 `node->frontmatterEntries`
- 使用 `QFontMetricsF(m_monoFont, m_device)` 计算最长 key 宽度（遵守 `横切关注点/40-高DPI适配.md` INV-2）
- 应用 INV-10 的列宽上限
- 按 INV-12 规则对 value 按字符换行，累加行数
- 写入 `LayoutBlock::frontmatterKeyColumnWidth` 与 `bounds.height()`

### 4.6 Painter 扩展

```cpp
// src/preview/PreviewPainter.cpp
void PreviewPainter::paintFrontmatter(
    QPainter* p,
    const LayoutBlock& block,
    qreal absX,
    qreal absY,
    const RenderContext& ctx
);
```

职责：
- 绘制圆角背景矩形（`ctx.theme.frontmatterBackground`）
- 绘制边框（`ctx.theme.frontmatterBorder`，1px 或按 fm.height 比例派生）
- 对每个 entry，按 `(absX + hPad, absY + i * lineHeight)` 绘制 key（第一列，截断时加 ellipsis）和 value（第二列，按字符换行）
- **严禁**在此构造带字面量字号的 `QFont`

## 5. 核心算法

### 5.1 提取方案选择

**候选 A（预处理剥离）**：在 `MarkdownParser::parse` 进入 cmark-gfm 之前，先在 `QString` 层面用正则扫描首个 frontmatter 块，将其剥离成独立的 `Frontmatter` AST 节点，然后把剩余正文送 cmark-gfm。

**候选 B（AST 后处理）**：让 cmark-gfm 按正常逻辑解析完整文档，然后在 AST 根节点的首个 child 上做模式匹配——如果是 `ThematicBreak + 若干段落 + ThematicBreak` 的序列，则吞掉并替换为 `Frontmatter` 节点。

**对比**：

| 维度 | 预处理剥离 | AST 后处理 |
|------|-----------|------------|
| 实现复杂度 | 简单：QString 扫行 | 复杂：AST 节点序列匹配 + cmark 节点 API |
| 误识风险 | 低（只看首行） | 中（cmark 可能已把第二行当 setext 标题） |
| cmark 交互 | 无 | 多，可能需要 cmark_node_unlink/free |
| 行号对齐 | 需要在剥离后记录 offset，后续映射回编辑器 | 原生保持，但要删除已消耗的节点 |
| 对 INV-1/INV-3 的支持 | 天然支持 | 要精确匹配首节点才能避免误识 |

**结论**：选 **候选 A（预处理剥离）**。理由：

1. cmark-gfm 没有原生 frontmatter 扩展，两个方案都要我们自己写
2. 预处理方案在 `QString` 层面完成，测试成本低
3. AST 后处理对 cmark 的 `ThematicBreak + Heading` 歧义（setext H2 底线也是 `---`）处理非常麻烦
4. 行号对齐问题可以通过在剥离时记录 `frontmatterLineCount`，后续 AST 节点的 `startLine` 统一偏移补偿解决

### 5.2 预处理剥离伪代码

```
function extractFrontmatter(raw):
    // 1. 跳过可选的 BOM
    text = raw.stripBomPrefix()

    // 2. 扫首个非空行
    lines = text.split('\n')
    i = 0
    while i < lines.size() and lines[i].trimmed().isEmpty():
        i += 1
    if i == lines.size(): return (null, raw)

    // 3. 必须是 '---'
    if lines[i].trimmed() != '---':
        return (null, raw)

    start = i
    i += 1

    // 4. 向下找结束 '---'
    end = -1
    while i < lines.size():
        if lines[i].trimmed() == '---':
            end = i
            break
        i += 1
    if end == -1:
        // INV-7 回退
        return (null, raw)

    // 5. 构造 entries
    node = new AstNode(Frontmatter)
    node.frontmatterRawText = lines[start..end].join('\n')

    for k in [start+1 .. end-1]:
        line = lines[k]
        trimmed = line.trimmed()
        if trimmed.isEmpty(): continue            // INV-5
        if trimmed.startsWith('#'): continue       // INV-6

        colon = trimmed.indexOf(':')
        if colon == -1:
            // INV-14
            node.frontmatterEntries.push_back({"", trimmed})
        else:
            key = trimmed.left(colon).trimmed()
            value = trimmed.mid(colon + 1).trimmed()
            node.frontmatterEntries.push_back({key, value})

    // 6. 剥离后的 body
    body = lines[end+1..].join('\n')

    return (node, body)
```

### 5.3 列宽计算伪代码

```
function layoutFrontmatter(node, maxWidth):
    fm = QFontMetricsF(monoFont, device)
    hPad = fm.height() * 0.2    // 与 inline code 一致（INV-8）
    vPad = fm.height() * 0.4    // 上下内边距

    // 最长 key 宽度
    maxKeyW = 0
    for (k, v) in entries:
        maxKeyW = max(maxKeyW, fm.horizontalAdvance(k))

    // INV-10 上限
    innerWidth = maxWidth - 2 * hPad
    cap = innerWidth * 0.5
    keyColW = min(maxKeyW + 2 * hPad, cap)
    valColW = innerWidth - keyColW

    // 累加行数（value 按字符换行）
    totalLines = 0
    for (k, v) in entries:
        valCharsPerLine = max(1, floor(valColW / fm.averageCharWidth()))
        valLines = ceil(v.length() / valCharsPerLine)
        totalLines += max(1, valLines)

    height = 2 * vPad + totalLines * codeLineHeight()

    block.frontmatterKeyColumnWidth = keyColW
    block.bounds = QRectF(0, 0, maxWidth, height)
```

### 5.4 背景色混合公式

```
function blendColor(accent, bg, accentRatio):
    r = accent.redF()   * accentRatio + bg.redF()   * (1 - accentRatio)
    g = accent.greenF() * accentRatio + bg.greenF() * (1 - accentRatio)
    b = accent.blueF()  * accentRatio + bg.blueF()  * (1 - accentRatio)
    return QColor::fromRgbF(r, g, b)

frontmatterBackground = blendColor(accentColor, previewBg, 0.5)
frontmatterBorder     = blendColor(accentColor, previewBg, 0.7)
```

使用 RGB 线性混合而**不是** alpha compositing，这样绘制时可以直接填充不透明颜色，避免选区/marking 高亮叠加时的双重混合问题。

### 5.5 Accent 色来源

Windows 下优先从 `QPalette::Highlight`（反映系统强调色）读取；Linux 下同样使用 `QPalette::Highlight`（随桌面环境变化，Gtk/Plasma 各有行为）。若用户未设置任何强调色（palette 返回全灰），fallback 到默认色 `#0078D4`（Windows 经典蓝）。

## 6. 性能预算

- `extractFrontmatter` 扫描 < **1 ms**（典型 frontmatter 不超过 20 行，逐行扫描）
- `layoutFrontmatter` < **0.5 ms**（entries 遍历 + 字体度量）
- `paintFrontmatter` < **2 ms**
- **整体**：不得增加预览更新端到端延迟（应 < 80ms，见 `模块-preview/README.md`）中可感知的时间

## 7. 验收条件

- **[T-1]** 文档首行为 `---`，中间 n 行 key-value，末行 `---` → 渲染为带背景的 n 行 2 列 frontmatter 块
- **[T-2]** 文档首行非 `---`（例如首行是 `# 标题`） → 不触发 frontmatter，后续内容正常渲染
- **[T-3]** 文档中间出现独立 `---` → 仍按 `ThematicBreak`（水平分割线）渲染，不与 frontmatter 冲突（INV-3）
- **[T-4]** frontmatter 的 key 有 10 个以上 → 表格无重叠、两列宽度稳定、整体高度正确
- **[T-5]** 某个 key 极长（例如 80 个字符）→ 第一列宽度被截断到可视区 50%，过长 key 以 ellipsis 结尾
- **[T-6]** 某个 value 极长（例如 500 字符）→ 按字符换行，表格两列严格对齐，不外溢
- **[T-7]** value 是数组字面量 `[src/core/Theme.h, src/core/Theme.cpp]` → 原样显示为字符串，不解析
- **[T-8]** 注释行 `# last updated 2026-04-13` → 不生成表格行，不可见
- **[T-9]** 空行 → 不生成表格行
- **[T-10]** 深色主题下，背景使用 `blend(accentColor, dark_bg, 0.5)`，视觉上是"淡化的 accent"
- **[T-11]** 浅色主题下，背景使用 `blend(accentColor, light_bg, 0.5)`，视觉上是"淡化的 accent"
- **[T-12]** 菜单切换主题后，frontmatter 的背景/边框/文字颜色同步更新，无残留
- **[T-13]** Ctrl+滚轮缩放字号时，frontmatter 的 key/value 字号、内边距、行高全部按比例变化（遵守 `横切关注点/80-字体系统.md` INV-8）
- **[T-14]** 鼠标选中整个 frontmatter 块并复制，剪贴板内容是 `---\nkey: value\n...\n---`（原始 YAML），**不是**渲染后的表格文字
- **[T-15]** frontmatter 块**不**出现在右侧 TOC 面板
- **[T-16]** 文档首行为 `---` 但缺少结束 `---` → 不渲染为 frontmatter，全部回退到普通渲染（INV-7）
- **[T-17]** 1x / 1.25x / 1.5x DPI 下 frontmatter 的视觉比例一致（圆角半径、内边距、行高都按字体度量派生）
- **[T-18]** 一行只有字符串没有冒号 → 作为纯 value 放入第二列，第一列留空（INV-14）
- **[T-19]** 一行 value 含 URL `site: https://example.com` → key = `site`，value = `https://example.com`（INV-4 的"第一个冒号"规则）

## 8. 已知陷阱 / 设计取舍

### 8.1 cmark-gfm 不原生支持 frontmatter

cmark-gfm 社区 fork 里没有 frontmatter extension，`CMARK_OPT_*` 常量中也不存在相关位。必须由我们在 parser 外层做预处理（本 Spec §5 的候选 A）。

### 8.2 YAML 完整语法非常复杂

本功能**只支持**简单 key-value，且 value 当字符串处理。不支持：

- 嵌套对象（`a:\n  b: 1`）
- 多行字符串 `|` 块和 `>` 折叠
- 锚点 `&ref` 和别名 `*ref`
- 显式类型标签 `!!int`
- YAML 流式语法的完整解析

如果用户写了嵌套 YAML，子行会被当作"无 key 的行"处理（INV-14），视觉上可接受，语义上当然有损。对 Spec 文档这个场景够用。

### 8.3 `---` 与水平分割线的历史冲突

这是 Markdown 生态里最经典的 frontmatter 陷阱——cmark 会把首个 `---` 解析成 `ThematicBreak`，而紧随其后的文字如果形如 `key: value`，它会被当作段落。必须**在进入 cmark 前**剥离，否则 AST 后处理需要逐节点匹配 + 合并，非常脆弱。

此外 setext H2 的底线（`---`）也是 `---`，但它出现在标题文字**之后**，不影响首行的判定。

### 8.4 `Theme::accentColor` 可能需要新增

当前 `src/core/Theme.h` 没有 `accentColor` 字段。本 Spec **要求**它被新增，但具体新增由"横切关注点/30-主题系统.md"的后续改动负责，本 Spec 不直接修改 30-主题系统.md。实施 Plan 中应给出"依赖主题系统加字段"的前置条件标注。

### 8.5 透明度叠加在深色 vs 浅色上视觉差异大

`blend(accent, bg, 0.5)` 在浅色主题下看起来是"淡化的高亮"，在深色主题下看起来是"混入主色的深灰"。两种主题下的视觉效果不完全对称，可能需要根据 `theme.isDark` 微调混合比例（例如浅色 0.3，深色 0.5）。若 T-10/T-11 人审不过，放宽 INV-8 的"固定 50%"改为"系数可调"。

### 8.6 跨平台 accent 色获取

- **Windows**：`QGuiApplication::palette().color(QPalette::Highlight)` 通常反映系统 accent
- **Linux**：取决于桌面环境，KDE/Plasma 比 Gnome 更可靠，XFCE 常常是固定蓝色
- **macOS**：虽然本项目暂不支持，但 `NSColor.controlAccentColor` 是标准 API

未设置时的 fallback 见 §5.5。

### 8.7 字体度量必须带 device

`QFontMetricsF::horizontalAdvance` 在没有 `device` 参数时返回逻辑像素，会导致在 1.5x DPI 屏上 `keyColW` 偏小，列对不齐。遵守 `横切关注点/40-高DPI适配.md` INV-2：**所有**度量调用都带 `m_device` / `p->device()`。

### 8.8 value 含多个 `:` 字符

value 里可能出现 `:`（最常见的是 URL，如 `https://example.com`）。INV-4 规定**只取第一个** `:` 作为 key/value 分隔符，剩余部分原样归入 value。所以 `site: https://example.com` → key = `site`, value = `https://example.com`（不是 `https` + `//example.com`）。

### 8.9 行号映射

剥离 frontmatter 后，cmark 看到的文档行号从 `frontmatterLineCount + 1` 开始。当前 preview → editor 的光标映射（`sourceLineToY` / `yToSourceLine`）依赖 AST 节点的 `startLine`。剥离后需要把所有 AST 节点的 `startLine` 统一加上偏移，否则点击预览会跳到编辑器错误行。

**实现策略**：在 `MarkdownParser::parse` 内部记录 `frontmatterLineCount`，然后在 `convertNode` 递归时对每个节点的 `startLine/endLine` 加偏移。

### 8.10 复制为原始 YAML 的实现

INV-13 要求复制文本是原始 `---\nkey: value\n---` 而**不**是渲染后的表格。实现上在 `PreviewWidget` 处理选区 → 文本转换时，遇到 frontmatter 块要直接输出 `LayoutBlock::frontmatterRawText` 字段，而不是拼接渲染 run。此字段由 parser 填入。

## 9. 参考

- `specs/模块-preview/02-布局引擎.md`（扩展 layout）
- `specs/模块-preview/03-绘制管线.md`（扩展 paint，遵守 INV-8）
- `specs/横切关注点/30-主题系统.md`（需新增 `accentColor` 等字段，由该 Spec 的后续改动负责）
- `specs/横切关注点/80-字体系统.md`（monoFont 派生规则）
- `specs/横切关注点/40-高DPI适配.md`（字体度量必须带 device）
- `specs/模块-parser/README.md`（parser 需新增 frontmatter 预处理步骤）
- [Jekyll Front Matter](https://jekyllrb.com/docs/front-matter/)——行业事实标准
- [Obsidian Properties (YAML)](https://help.obsidian.md/Editing+and+formatting/Properties)——另一常见消费者
- [Hugo Front Matter](https://gohugo.io/content-management/front-matter/)——静态站生成器场景

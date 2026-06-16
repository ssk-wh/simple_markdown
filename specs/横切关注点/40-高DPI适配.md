---
id: 横切关注点/40-高DPI适配
status: stable
owners: [@pcfan]
code: [src/preview/PreviewLayout.cpp, src/preview/PreviewPainter.cpp, src/preview/PreviewWidget.cpp, src/editor/EditorLayout.cpp, src/editor/EditorPainter.cpp]
tests:
  - tests/app/FontConsistencyTest.cpp                # 编辑/预览字号一致（QFontMetricsF 带 device）
  - tests/preview/PreviewSelectionHeightTest.cpp     # 带 device 的字体度量驱动高亮高度
  - tests/preview/PreviewTableCellOverflowTest.cpp   # 带 device 的换行估算不越界
  # 原引用 HighDpiTest.cpp 从未落地；INV-DPI-METRICS（度量必带 device）由上述测试间接覆盖。
  # 1.25x/1.5x/2x 多 DPI 矩阵：headless 难模拟物理 DPI，当前手动验证（截图对比），待补
depends: [specs/20-约束与不变量.md]
last_reviewed: 2026-04-13
---

# 高 DPI 适配

## 1. 目的

定义所有涉及字体度量、行高、坐标的代码在 1x / 1.25x / 1.5x / 2x DPI 屏幕下的正确行为，杜绝"代码块下方空白""双击坐标偏差""反引号过多空白"这一类 DPI 相关 bug。

## 2. 输入 / 输出

- **输入**：字体 `QFont`、当前的 `QPaintDevice*`
- **输出**：在所有 DPI 下视觉一致的布局和绘制

## 3. 行为契约

### [INV-1] 所有 QFontMetricsF 必须带 device 参数

```cpp
// ✗ 错误：逻辑像素
QFontMetricsF fm(font);

// ✓ 正确：物理像素
QFontMetricsF fm(font, device);
```

device 的来源：
- 布局阶段：由调用方传入的 `QPaintDevice*`
- 绘制阶段：`p->device()`
- 鼠标命中：`viewport()`

### [INV-2] 构造函数禁止计算 DPI 依赖的度量

构造函数时 widget 还未 attach 到 screen，`QPaintDevice*` 不可用。所有依赖 DPI 的成员变量必须延迟到 `updateMetrics(device)` 中初始化。

```cpp
// ✓ 构造函数只设临时值
PreviewLayout::PreviewLayout() {
    m_lineHeight = 20.0;        // 临时值
    m_codeLineHeight = 20.0;    // 临时值
}

// ✓ updateMetrics 负责真实计算
void PreviewLayout::updateMetrics(QPaintDevice* device) {
    QFontMetricsF fm(m_baseFont, device);
    m_lineHeight = fm.height() * 1.5;
    QFontMetricsF fmCode(m_monoFont, device);
    m_codeLineHeight = fmCode.height() * 1.4;
}
```

### [INV-3] 布局与绘制必须共享同一度量

布局阶段和绘制阶段不得出现一个用 `QFontMetricsF(font)`、另一个用 `QFontMetricsF(font, device)` 的情况。两者必须同时带 device，或同时不带（后者仅在无 DPI 依赖时允许）。

### [INV-4] 不得在估算阶段二次调整行高

`estimateParagraphHeight` 这类估算函数必须使用 `m_lineHeight`（已在 updateMetrics 中根据 DPI 调整），**不得**基于每个 run 的 `QFontMetricsF(font)` 再次计算行高：

```cpp
// ✗ 错误：高 DPI 下与 updateMetrics 的结果不一致
qreal lineHeight = m_lineHeight;
for (const auto& run : runs) {
    QFontMetricsF fm(run.font);         // 缺 device
    qreal runLineH = fm.height() * 1.5;
    if (runLineH > lineHeight) lineHeight = runLineH;
}

// ✓ 正确：直接使用 m_lineHeight
qreal lineHeight = m_lineHeight;
for (const auto& run : runs) {
    QFontMetricsF fm(run.font);
    totalWidth += fm.horizontalAdvance(run.text);
}
```

### [INV-5] 不得硬编码 padding / margin 像素值

内联代码（反引号）、代码块、块间距等涉及字体的 padding 必须按字体高度的**比例**计算：

```cpp
// ✗ 错误
qreal segW = fm.horizontalAdvance(run.text) + 4;

// ✓ 正确
qreal hPad = fm.height() * 0.2;  // 高度的 20%
qreal segW = fm.horizontalAdvance(run.text) + hPad * 2;
```

### [INV-6] updateMetrics 的触发点

必须在以下场景调用 `updateMetrics(device)`：

| 场景 | 调用点 |
|------|--------|
| 初次加载 AST | `updateAst()` |
| 窗口 resize | `resizeEvent()` |
| 绘制前 | `paintEvent()` 开头 |
| 字体改变 | `setFont()` |
| DPI 切换（跨屏） | `QEvent::ScreenChangeInternal` |

## 4. 接口

```cpp
// PreviewLayout.h
class PreviewLayout {
public:
    // 根据 device 的 DPI 重新计算所有度量
    // 必须在任何布局或绘制操作前调用至少一次
    void updateMetrics(QPaintDevice* device);

    // 布局时使用 device 计算字体度量
    void layout(const MarkdownAst& ast, int width, QPaintDevice* device);

private:
    qreal m_lineHeight{20.0};      // 临时值，updateMetrics 中覆盖
    qreal m_codeLineHeight{20.0};
};
```

## 5. 核心算法

### DPI 感知的度量计算流程

```
paintEvent(event)
  │
  ├─ updateMetrics(this)       ← 用当前 QPaintDevice 的 DPI 算度量
  │    └─ m_lineHeight = QFontMetricsF(m_baseFont, this).height() * 1.5
  │
  ├─ layout(ast, width, this)  ← 用同样的 device 布局
  │    └─ 使用 m_lineHeight，不得重新计算
  │
  └─ painter.paint(...)         ← 绘制时 p->device() == this
       └─ QFontMetricsF(font, p->device()) 与布局一致
```

### 鼠标命中测试

```
mousePressEvent(event)
  │
  ├─ updateMetrics(viewport())  ← 保证度量与最近一次绘制一致
  │
  └─ textIndexAtPoint(pos)
       └─ QFontMetricsF(seg.font, viewport())
```

## 6. 性能预算

- `updateMetrics` 单次调用 < 1 ms
- `updateMetrics` 在单次 paintEvent 内调用次数 ≤ 1

## 7. 验收条件

- **[T-1]** 1x DPI 屏：代码块、反引号、行间距与 1.25x/1.5x 屏视觉比例一致
- **[T-2]** 1.25x DPI 屏：代码块下方无多余空白
- **[T-3]** 1.5x DPI 屏：同 T-2
- **[T-4]** DPI 切换：把窗口从 1x 屏拖到 1.5x 屏，行高、缩进、选区位置正确更新
- **[T-5]** 双击选中：任意 DPI 下双击单词精确选中，不偏移
- **[T-6]** 反引号：内联代码的左右 padding 与字体大小成比例
- **[T-7]** 列表序号：序号与正文基线对齐，不下沉
- **[T-8]** 嵌套结构：引用内的列表 / 表格内的复杂内容渲染位置正确
- **[T-9]** grep 检查：`grep -rn "QFontMetricsF(" src/` 的所有调用都带 device 参数
- **[T-10]** 构造函数检查：所有 `PreviewLayout` / `EditorLayout` 构造函数不调用 `QFontMetricsF(font, device)`（此时 device 不可用）

## 8. 已知陷阱

### 8.1 构造函数陷阱（踩过 3 次）

```cpp
// ✗ 构造函数中 widget 未 attach，device 为 null
PreviewLayout::PreviewLayout() {
    QFontMetricsF fm(font);              // → 逻辑像素，值偏小
    m_height = fm.height() * 1.4;        // = 22.4px
}

// 当 updateMetrics 后续用正确 device 计算时
void updateMetrics(QPaintDevice* device) {
    QFontMetricsF fm(font, device);      // → 物理像素
    m_height = fm.height() * 1.4;        // = 33.6px (1.5x)
}

// DPI 切换时 m_height 从 33.6 → 22.4，下降 33%
```

**修复**：构造函数只设临时常量（如 `20.0`），不调用 `QFontMetricsF`。

### 8.2 estimateParagraphHeight 双重度量

在段落高度估算中，看似"取 run 最大字号"的逻辑，会导致高 DPI 下估算值和实际渲染不一致。详见 INV-4。

### 8.3 反引号 padding 硬编码

早期代码写死 `+4`，在 1.5x 屏下视觉太窄。改成 `fm.height() * 0.2` 后自动适配。

### 8.4 mousePressEvent 未先 updateMetrics

鼠标事件到来时，如果屏幕刚切换过 DPI 但 paintEvent 还没触发，度量是旧值，命中测试偏移一个字符宽度。解决：`mousePressEvent` 开头 `updateMetrics(viewport())`。

### 8.5 容器块的子块 X 坐标（commit 4073b22）

不是纯 DPI 问题，但和度量一起出现过。`BlockQuote` / `List` 这类容器递归绘制子块时，必须 `childAbsX = absX + child.bounds.x()`，否则序号与内容错位。详见 `specs/横切关注点/20-坐标系统.md`。

## 9. 参考

- [Qt 高 DPI 官方文档](https://doc.qt.io/qt-5/highdpi.html)
- [QFontMetricsF 文档](https://doc.qt.io/qt-5/qfontmetricsf.html)
- commit 4073b22 — 容器块坐标传递修复
- 2026-03-30 修复批次：代码块空白 / 双击偏差 / 反引号空白 / 行间距不均匀
- 原文档：`docs/高DPI适配指南.md`（已废弃，内容迁入本文）

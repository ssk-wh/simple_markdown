# SimpleMarkdown 项目指南

本文档记录项目开发中的经验、陷阱和最佳实践，帮助后续开发避免重复踩坑。

---

## 高 DPI 屏幕适配

### 问题场景

在 1.5x、1.25x DPI 屏幕上会出现：
- 代码块下方有多余的空白区域（约为正常行高的 20-40%）
- 双击选中文本时坐标偏差
- 反引号渲染出现过多空白

### 根本原因

Qt 的 DPI 缩放有两个层面：

| 层面 | 说明 |
|------|------|
| **逻辑像素** | `QFontMetricsF(font)` 返回的是逻辑像素<br>96 DPI 标准下的坐标系 |
| **物理像素** | `QFontMetricsF(font, device)` 返回的是物理像素<br>基于当前屏幕 DPI 的实际坐标 |

问题：
- **布局阶段**计算行高时如果用逻辑像素，会得到较小的值（例如 20px）
- **绘制阶段** QPainter 自动将坐标缩放到物理像素（例如 20px × 1.5 = 30px）
- 结果：布局的行高和实际渲染的行高不匹配 → 代码块下方空白

### 修复方案

**核心原则：布局和绘制使用同一度量系统**

#### 1. 布局阶段（PreviewLayout.cpp）

```cpp
void PreviewLayout::updateMetrics(QPaintDevice* device)
{
    // ✓ 正确做法：传入 device 参数
    QFontMetricsF fm(m_baseFont, device);
    m_lineHeight = fm.height() * 1.5;
    QFontMetricsF fmCode(m_monoFont, device);
    m_codeLineHeight = fmCode.height() * 1.4;
}
```

#### 2. 绘制阶段（PreviewPainter.cpp）

```cpp
// ✓ 所有 QFontMetricsF 都必须带 device 参数：
QFontMetricsF fm(monoFont, p->device());     // 代码块行高
QFontMetricsF defaultFm(font, p->device()); // 内联文本
```

#### 3. 鼠标事件（PreviewWidget.cpp）

```cpp
// 在 textIndexAtPoint 中计算文本位置时，也要用 device 参数
// 否则双击选中会出现坐标偏差
QFontMetricsF fm(seg.font, viewport());
```

#### 4. 高度估计（PreviewLayout.cpp）

**陷阱：** 布局阶段的高度估计不能动态调整行高！

```cpp
// ✗ 错误：在 estimateParagraphHeight 中根据每个 run 动态调整
qreal lineHeight = m_lineHeight;
for (const auto& run : runs) {
    QFontMetricsF fm(run.font);  // 没有 device 参数，逻辑像素
    qreal runLineH = fm.height() * 1.5;
    if (runLineH > lineHeight)
        lineHeight = runLineH;  // ← 高 DPI 时与 updateMetrics 不一致
}

// ✓ 正确：使用在 updateMetrics 中预先计算的 m_lineHeight
qreal lineHeight = m_lineHeight;
for (const auto& run : runs) {
    QFontMetricsF fm(run.font);
    totalWidth += fm.horizontalAdvance(run.text);
    // 不再调整 lineHeight，保持与 updateMetrics 一致
}
```

**原因：** m_lineHeight 已经根据设备 DPI 调整，再次调整会导致估计高度与实际高度不匹配。

### 何时调用 updateMetrics

| 场景 | 是否需要 |
|------|---------|
| 窗口初始化（updateAst） | ✓ 需要 |
| 窗口 resize（resizeEvent） | ✓ 需要 |
| 鼠标事件前（paintEvent） | ✓ 需要 |
| 字体改变时（setFont） | ✓ 需要 |

### 检查清单

在任何涉及字体、行高、坐标的改动前：

- [ ] 布局阶段使用 `QFontMetricsF(font, device)` 计算
- [ ] 绘制阶段使用 `QFontMetricsF(font, p->device())` 计算
- [ ] 鼠标命中测试使用 `QFontMetricsF(font, viewport())` 计算
- [ ] 在 DPI 改变时（resizeEvent、updateMetrics）同步重新计算
- [ ] 在 1x、1.25x、1.5x DPI 屏幕都验证过

---

## 坐标系统设计原则

### 问题场景

列表序号与文本基线不对齐（序号偏下）、嵌套块结构渲染位置错误。

### 根本原因

递归绘制时，子块使用了错误的绝对坐标：

```cpp
// ✗ 错误：子块继承了父块的 absY，而不是计算自身的绝对坐标
for (const auto& child : block.children) {
    paintBlock(p, child, absX, absY, ...);  // absY 是列表块的，不是列表项的
}
```

### 修复方案

所有子块必须显式计算自身的绝对坐标：

```cpp
// ✓ 正确：每个子块都计算自己的绝对 y 坐标
for (const auto& child : block.children) {
    qreal childAbsY = absY + child.bounds.y();  // 相对坐标 + 相对偏移
    paintBlock(p, child, absX, childAbsY, ...);
}
```

### 应用范围

- **List 块**：列表项的绝对坐标必须包含列表的 y 偏移
- **BlockQuote 块**：引用内容的绝对坐标必须包含引用的 y 偏移
- **Table 块**：表格单元格的绝对坐标必须包含行的 y 偏移

### 检查清单

任何涉及渲染树的嵌套结构改动：

- [ ] 子块的 absY 是否显式计算：`absY + child.bounds.y()`
- [ ] 递归调用 paintBlock 时是否使用了计算出的绝对坐标
- [ ] 序号/前缀绘制和内容绘制是否使用同一个 absY

---

## 窗口焦点管理

### 问题场景

用户在浏览器中打开 markdown 文件时，应用成功加载文件但窗口仍在后台，用户看不到。

### 解决方案

在 `MainWindow::openFile()` 中，文件加载完毕后立即提升窗口：

```cpp
void MainWindow::openFile(const QString& path)
{
    // ... 加载文件逻辑 ...

    // 三步走确保窗口被置于最前
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}
```

**三个步骤的作用：**

| 方法 | 作用 |
|------|------|
| `setWindowState()` | 取消最小化，设置活跃状态 |
| `raise()` | 在 Z 序中提升到最前 |
| `activateWindow()` | 给予键盘焦点 |

**需要在两个地方调用：**

1. **文件已打开** - 用户双击相同文件时，切换标签页后提升
2. **文件新打开** - 用户打开新文件时，完成加载后提升

---

## 代码块与标记功能

### 标记（Marking）的实现

见 `src/preview/PreviewWidget.cpp` 和 `src/preview/PreviewPainter.cpp`

**关键点：**

- 标记高亮先于选区高亮绘制（`m_highlights` 在选区前）
- 标记数据与 TOC 面板的条目映射要精确（`m_headingCharOffsets`）
- 深浅主题的颜色要有区别（避免可见性问题）

### 反引号（backtick）渲染

见 `PreviewPainter.cpp` 代码块渲染部分

**陷阱：** 不要硬编码 padding，必须根据字体度量计算！

```cpp
// ✗ 错误：硬编码 padding
qreal segW = fm.horizontalAdvance(run.text) + 4;  // 在高 DPI 下比例不对

// ✓ 正确：根据实际字体度量计算
qreal hPad = fm.height() * 0.2;  // 高度的 20%
qreal segW = fm.horizontalAdvance(run.text) + hPad * 2;
```

---

## 编译与打包

### 构建脚本

- **编译：** `./build_on_win.bat release`
- **收集依赖：** `python installer/collect_dist.py`
- **打包：** `/nsis-pack D:/...installer/SimpleMarkdown.nsi`

### 常见问题

| 问题 | 解决方案 |
|------|---------|
| CMake cache 过期 | 删除 `build/CMakeCache.txt` 后重建 |
| 编译找不到 Qt | 检查 `CMakeCache.txt` 中的 `Qt5_DIR` 路径 |
| 安装包大小异常 | 运行 `collect_dist.py` 检查是否包含了 debug DLL |

---

## 编码风格与规范

### 注释约定

- **高 DPI 相关修复** 都标注 `[高 DPI 修复]` 标签，方便日后查找
- **窗口管理** 都标注 `[窗口焦点]` 标签
- 复杂逻辑添加 **为什么（Why）** 的注释，而不仅是 **是什么（What）**

### Commit Message

遵循 Conventional Commits 规范：

```
fix: 高 DPI 下代码块高度计算不正确，代码块下方出现多余空白

- 在 PreviewLayout::updateMetrics 中使用 device 参数
- 在 PreviewPainter 绘制代码块时也使用 p->device()
- 确保布局和绘制使用同一度量系统

Fixes: #123
```

---

## 测试清单

新功能提交前必须在以下环境测试：

- [ ] **1x DPI 屏** - 确保没有回归
- [ ] **1.25x DPI 屏** - 检查代码块、反引号、坐标
- [ ] **1.5x DPI 屏** - 同上
- [ ] **DPI 切换** - 从 1x 拖到 1.5x 屏，验证 updateMetrics 生效
- [ ] **窗口最小化/最大化** - 检查 DPI 改变时的行为
- [ ] **多标签页场景** - 打开多个文件后切换
- [ ] **列表序号对齐** - 检查序号与文本基线是否对齐（A 和 B 屏）
- [ ] **嵌套结构** - 测试引用内的列表、表格中的复杂内容

---

## 已知问题与解决方案

### ✓ 已解决

- [x] 高 DPI 代码块下方空白（2026-03-30 修复）
- [x] 双击选中坐标偏差（2026-03-30 修复）
- [x] 反引号过多空白（2026-03-30 修复）
- [x] 打开文件时窗口未提升（2026-03-30 修复）
- [x] 列表序号与文本基线不对齐（2026-03-30 修复 - 坐标系统统一）
- [x] 高 DPI 屏上行间距不均匀（2026-03-30 修复 - 高度估计一致性）

### 待观察

- [ ] Linux/macOS 高 DPI 行为（当前仅在 Windows 上测试）
- [ ] 极端高 DPI（2.0x+）的表现

---

## 参考资源

- **Qt 高 DPI 官方文档**：https://doc.qt.io/qt-5/highdpi.html
- **QFontMetricsF 文档**：https://doc.qt.io/qt-5/qfontmetricsf.html
- **项目 CMakeLists.txt**：定义了 Qt 版本和编译选项


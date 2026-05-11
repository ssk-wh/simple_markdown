---
date: 2026-05-11
status: completed
related_specs:
  - specs/模块-preview/11-预览区查找.md
  - src/editor/SearchBar.cpp
  - src/app/MainWindow.cpp
---

# 彻底取消 SearchBar FocusOut 自动隐藏 + Ctrl+F 改可见性路由

## 背景

1.1.2 完成后用户再次复测，两个新诉求：

1. **bug A 残留**：编辑器侧搜索栏快速点击"下一个"也会消失（上一轮的单实例
   timer 优化只是改善了堆叠，没根除——任何 FocusOut 自动隐藏机制在按钮密集
   交互时都存在窗口期内 hasFocus 暂时 false 的隐患）
2. **bug C**：预览区 Ctrl+F 不响应（用户预期：应用打开 + 有文档 + 任一区域可见
   即响应，不应依赖焦点）
3. **下轮愿景**：搜索栏跨区域统一，编辑/预览同时显示时**两边同步标记 + 同步跳转**
   （用户原话："谁显示出来了谁就标记搜索结果，点上一个/下一个时两边同时跳"）

本轮聚焦根治 1+2；3 单列后续 plan 占位。

## 根因

### Bug A 残留：FocusOut 自动隐藏机制本身有缺陷

`SearchBar::eventFilter` 在 m_findEdit FocusOut 时启动 100ms 延迟检查后
hideBar。上一轮改成单实例 timer 避免堆叠，但仍存在：

- 用户最后一次按按钮后停手 → 最后一次 FocusOut 重启 timer
- 100ms 内某些 Qt 事件链（emit findNext → ensureCursorVisible → ...）可能让
  m_findEdit 短暂未持有焦点
- 100ms 到期 → hasFocus() 检查 false → hideBar 触发

**结论**：FocusOut 自动隐藏在密集交互场景下本质上不可靠——用户主动打开搜索栏
就该一直保留，直到主动关闭。彻底删除 FocusOut 隐藏机制。

### Bug C：Ctrl+F 路由依赖焦点过严

当前 INV-6：`focusInPreview ? preview : editor`。若用户在预览区滚动后焦点跳到
其他面板（如 TOC、资源管理器），Ctrl+F 走 editor 分支——若处于纯预览模式
（editor 不可见），用户感知"Ctrl+F 不响应"。

**结论**：路由策略应"焦点优先 + 可见性兜底"——焦点在 preview/editor 且对应区域
可见就用焦点决定；否则按可见性默认 editor → 兜底 preview。

## 设计

### 1. SearchBar 删除 FocusOut 自动隐藏

`SearchBar.h`：删 `QTimer* m_focusOutTimer` 成员 + 前置声明 `class QTimer;`
（若不再有其他 QTimer 使用）。

`SearchBar.cpp`：
- ctor 中删 m_focusOutTimer 初始化 + connect
- `eventFilter` 删 `FocusOut` 分支整块
- `keepFocus()` 删 `m_focusOutTimer->stop()` 调用——只保留 `setFocus(m_findEdit)`

`keepFocus()` 在 findNext/findPrev 后仍调用，让 m_findEdit 保持视觉/键盘
就绪态——但不再为"防止 FocusOut 触发 hideBar"服务。

### 2. MainWindow Ctrl+F 路由改造

```cpp
editMenu->addAction(tr("Find..."), [this]() {
    auto* tab = currentTab();
    if (!tab) return;

    QWidget* focused = QApplication::focusWidget();
    bool focusInPreview = false, focusInEditor = false;
    for (QWidget* w = focused; w; w = w->parentWidget()) {
        if (w == tab->preview) { focusInPreview = true; break; }
        if (w == tab->editor)  { focusInEditor  = true; break; }
    }

    const bool previewVisible = tab->preview && tab->preview->isVisible();
    const bool editorVisible  = tab->editor  && tab->editor->isVisible();

    auto open = [&](bool toPreview) {
        if (toPreview) {
            if (tab->editor) tab->editor->hideSearchBar();
            tab->preview->showSearchBar();
        } else {
            if (tab->preview) tab->preview->hideSearchBar();
            tab->editor->showSearchBar();
        }
    };

    if (focusInPreview && previewVisible)       open(true);   // 焦点优先
    else if (focusInEditor && editorVisible)    open(false);
    else if (editorVisible)                     open(false);  // 可见性兜底，编辑器优先
    else if (previewVisible)                    open(true);
    // 两侧都不可见 → 静默 no-op（理论不会发生，currentTab 非空一定有至少一侧可见）
}, QKeySequence::Find);
```

### 3. Spec 更新

- INV-6 改写：路由策略改为"焦点优先 + 可见性兜底"
- INV-13 重写：从"单实例 timer"改为"取消 FocusOut 自动隐藏"
- 新增 INV-14 / 调整 T 编号若需要

### 4. 占位下轮 plan

写 `plans/2026-05-11-预览编辑双区同步跳转.md` (status: draft) 记录用户愿景，
不实施——超出本轮范围。

## 任务

- [x] SearchBar.h/cpp 删 FocusOut 隐藏（成员 timer + ctor 初始化 + eventFilter 分支 + keepFocus stop + QTimer include）
- [x] MainWindow Ctrl+F 路由改写为"焦点优先 + 可见性兜底"
- [x] Spec INV-6 + INV-13 改写 + T-14 调整 + 新增 T-15（焦点不在两侧也能响应）
- [x] 编译 + ctest 全绿（12/12 通过，7.75 sec）
- [x] CHANGELOG 1.1.2 Fixed 段追加 2 条
- [x] 写下轮 plan 占位 `2026-05-11-预览编辑双区同步跳转.md` (draft)
- [x] 归档本 plan

## 风险

- **删除 FocusOut 自动隐藏**：用户点击编辑器其他区域时搜索栏**不再自动收起**
  ——这是 UX 改动，但与"打开就保留到主动关闭"的用户预期一致；可通过 Esc / 关闭
  按钮显式关闭，没增加心智负担
- **Ctrl+F 路由改可见性兜底**：用户焦点在 TOC / 资源管理器时按 Ctrl+F 总是开
  编辑器搜索栏（默认）——若用户想搜预览，仍可先点预览再 Ctrl+F；行为更宽容

## 进展

- 2026-05-11 创建 plan，根因复盘完成
- 2026-05-11 SearchBar 彻底删除 FocusOut 自动隐藏机制（h 删成员 + cpp 删 ctor 初始化 / eventFilter 分支 / keepFocus 中 stop 调用 / 删 QTimer include）
- 2026-05-11 MainWindow Ctrl+F 路由改"焦点优先 + 可见性兜底"
- 2026-05-11 Spec INV-6 重写 + INV-13 重写 + T-14 改义 + 新增 T-15
- 2026-05-11 编译通过，12/12 ctest 全绿（7.75 sec）
- 2026-05-11 CHANGELOG 1.1.2 Fixed 追加 2 条，归档本 plan，双区同步留作 draft plan
- ✅ 等用户行为级复测确认

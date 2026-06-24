# 模块：core（核心数据模型）

## 职责

提供文本存储、撤销、选区、文件 I/O、主题、最近文件等**无 GUI 依赖**的基础设施。所有上层模块都可以依赖 core，core 不依赖任何其他模块。

## 对应源码

`src/core/`

## Spec 清单

| 编号 | 标题 | 状态 | 对应源文件 |
|------|------|------|-----------|
| 01 | 文本存储（PieceTable） | draft | `PieceTable.h/cpp` |
| 02 | 文档模型（Document） | draft | `Document.h/cpp` |
| 03 | 撤销栈（UndoStack） | draft | `UndoStack.h/cpp` |
| 04 | 选区模型（Selection） | draft | `Selection.h/cpp` |
| 05 | 行索引（LineIndex） | draft | `LineIndex.h/cpp` |
| 06 | 文件映射（MappedFile） | draft | `MappedFile.h/cpp` |
| 07 | 最近文件（RecentFiles） | draft | `RecentFiles.h/cpp` |
| 08 | 主题系统（Theme） | draft | `Theme.h/cpp` |

## 依赖关系

```
Document
  ├─ PieceTable（持有）
  ├─ UndoStack（持有）
  └─ Selection（持有）

LineIndex    ← 被 Editor 使用，从 PieceTable 增量计算
MappedFile   ← 被 PieceTable 用于大文件的只读映射
RecentFiles  ← 独立，QSettings 持久化
Theme        ← 被 Editor 和 Preview 读取
```

## 验收条件

### UndoStack 撤销栈

| 编号 | 描述 |
|------|------|
| T-US-1 | 空栈 canUndo / canRedo 返回 false |
| T-US-2 | push → undo → redo 往返正确 |
| T-US-3 | 多步 undo 按栈序逆序返回 |
| T-US-4 | 新编辑清空重做栈 |
| T-US-5 | setSavePoint / isAtSavePoint 正确跟踪 |
| T-US-6 | 连续单字符键入合并为单条 |
| T-US-7 | 连续退格合并为单条 |
| T-US-8 | 空格和换行不与前序字符合并 |
| T-US-9 | clear 重置所有状态 |
| T-US-10 | 空字符串 push 无影响 |
| T-US-11 | 跨越保存点的操作不合并 |

### LineIndex 行索引

| 编号 | 描述 |
|------|------|
| T-LI-1 | 默认行为（骨架实现，lineCount=1） |
| T-LI-2 | build 后仍为默认 |

### Selection 选区模型

| 编号 | 描述 |
|------|------|
| T-SEL-1 | TextPosition 比较操作符 |
| T-SEL-2 | 空选区 hasSelection 返回 false |
| T-SEL-3 | 设置选区 anchor / cursor |
| T-SEL-4 | clearSelection 清空选区 |
| T-SEL-5 | 扩展选区到同一点 → 选区清空 |
| T-SEL-6 | 选区方向 isForward |
| T-SEL-7 | start / end 按方向返回正确端点 |
| T-SEL-8 | preferredColumn get/set/reset |

### Document 文档模型

| 编号 | 描述 |
|------|------|
| T-DOC-1 | 空文档 isEmpty / length / canUndo |
| T-DOC-2 | insert 操作 |
| T-DOC-3 | remove 操作 |
| T-DOC-4 | replace 操作 |
| T-DOC-5 | undo / redo 往返 |
| T-DOC-6 | isModified 跟踪 |
| T-DOC-7 | setModified 强制设回未修改 |
| T-DOC-8 | textChanged 信号参数正确 |

### PieceTable 文本存储

| 编号 | 描述 |
|------|------|
| T-PT-1 | 空表 isEmpty 返回 true |
| T-PT-2 | 构造函数初始化文本正确 |
| T-PT-3 | 在开头插入 |
| T-PT-4 | 在末尾插入 |
| T-PT-5 | 在中间插入 |
| T-PT-6 | 空字符串插入无影响 |
| T-PT-7 | 从开头删除 |
| T-PT-8 | 从中间删除 |
| T-PT-9 | 零长度删除无影响 |
| T-PT-10 | 删除超出末尾截断 |
| T-PT-11 | replace 操作 |
| T-PT-12 | 多次插入文本正确 |
| T-PT-13 | 混合插入删除 |
| T-PT-14 | textAt 子串查询 + 跨 piece |
| T-PT-15 | 行操作（lineText / offsetToLine / lineToOffset） |
| T-PT-16 | 跨多 piece 删除 |
| T-PT-17 | 空构造后 insert 不崩溃 |

## 全局约束

- 所有类必须是**线程安全**或明确标注为主线程独占
- 禁止依赖 Qt::Widgets，只允许 Qt::Core
- 文本 API 以 UTF-16 码元为单位（与 QString 一致）

# SimpleMarkdown 规范目录（Specs）

本目录是项目的**规范驱动开发（SDD）**核心——所有功能、约束、不变量都定义在此。

## 黄金法则

> **Spec 是第一公民，代码是 Spec 的投影。**

- 改动从写/改 Spec 开始，不从改代码开始
- AI 读 Spec 生成/修改代码
- 代码反向更新时，同步回写 Spec（不允许 Spec 腐烂）
- 测试从 Spec 的「验收条件」推导

## 目录布局

| 路径 | 层级 | 内容 |
|------|------|------|
| `00-产品愿景.md` | 项目级 | 产品定位、非目标、性能预算 |
| `10-系统架构.md` | 系统级 | 模块划分、线程模型、数据流 |
| `20-约束与不变量.md` | 系统级 | 全局硬约束（DPI、坐标系、线程安全…） |
| `模块-core/` | 模块级 | 核心（主题、字体默认值、最近文件、性能探针；PieceTable/UndoStack 已随 2.0.0 移除） |
| `模块-viewer/` | 模块级 | **只读分块查看器**（扫描/滚动/加载/控件/查找/标记，2.0.0 新增） |
| `模块-editor/` | 模块级 | ~~自绘编辑器~~（已废弃，2.0.0 移除） |
| `模块-parser/` | 模块级 | Markdown 解析 |
| `模块-preview/` | 模块级 | 自绘预览 |
| `模块-sync/` | 模块级 | ~~滚动同步~~（已废弃，2.0.0 移除） |
| `模块-app/` | 模块级 | 主窗口、多 Tab、文件操作 |
| `横切关注点/` | 横切 | 高 DPI、主题、国际化等跨模块不变量 |

## Spec 文件命名

- 顶层系统 Spec：`NN-名称.md`（`00`、`10`、`20` 这种十位编号，便于插入新条目）
- 模块 Spec：`NN-名称.md`（从 `01` 起编号）
- 所有文件名**使用中文**

## Spec 文件模板

每个 Spec 必须包含以下 9 段（不得省略，若不适用写「N/A」）：

```markdown
---
id: 模块-preview/01-布局引擎
status: stable | draft | deprecated
owners: [@pcfan]
code: [src/preview/PreviewLayout.h, src/preview/PreviewLayout.cpp]
tests: [tests/preview/PreviewLayoutTest.cpp]
depends: [specs/20-约束与不变量.md#高DPI]
last_reviewed: 2026-04-13
---

# 模块/功能名

## 1. 目的
一句话：这个模块/功能解决什么问题。

## 2. 输入 / 输出
- 输入：...
- 输出：...

## 3. 行为契约（不变量）
- [INV-1] 必须满足的规则一
- [INV-2] 必须满足的规则二

## 4. 接口（API 签名）
关键类/函数的签名，不贴实现。

## 5. 核心算法
伪代码或流程图，描述「怎么做」的大方向。

## 6. 性能预算
量化指标（延迟、内存、吞吐）。

## 7. 验收条件（测试用例清单）
- [T-1] 可执行的验证场景一
- [T-2] 可执行的验证场景二

## 8. 已知陷阱
历史上踩过的坑、容易写错的地方、反模式。

## 9. 参考
相关 commit、设计决策、外部文档链接。
```

## Frontmatter 字段说明

| 字段 | 必填 | 说明 |
|------|------|------|
| `id` | 是 | 全局唯一 ID，通常是路径（不含 `.md`） |
| `status` | 是 | `stable` / `draft` / `deprecated` |
| `owners` | 是 | GitHub 用户名数组 |
| `code` | 是 | 实现此 Spec 的源文件路径数组 |
| `tests` | 是 | 对应的测试文件路径数组（无则写 `[]`） |
| `depends` | 否 | 依赖的其他 Spec 路径（含锚点） |
| `last_reviewed` | 是 | `YYYY-MM-DD`，上次人工审核日期 |

## 工作流

### 新功能 / 修改

1. **写 / 改 Spec**：在本目录新增或修改 `.md`，更新 `status` 和 `last_reviewed`
2. **写 Plan**（复杂改动必须）：在 `plans/` 下新增实施计划
3. **AI 生成代码**：`/ecc:prp-implement specs/模块-xxx/NN-xxx.md` 或手动指令
4. **AI 生成测试**：从 Spec 第 7 节「验收条件」推导
5. **人审**：对照 Spec 的 INV 条目逐条核对
6. **提交**：commit message 引用 Spec 路径

### Bug 修复

**先问**：Spec 里有没有写这个行为？

- **写了** → 代码违反了 Spec → 改代码
- **没写** → Spec 有漏洞 → **先补 Spec 的 INV + T 条目，再改代码**

每修一个 bug 都把教训固化到 Spec，防止重复踩坑。

## 代码 ↔ Spec 追溯

**源文件头部注释**：

```cpp
// src/preview/PreviewLayout.h
//
// Spec: specs/模块-preview/01-布局引擎.md
// Invariants enforced here: INV-1, INV-2, INV-3
// Last synced: 2026-04-13
```

**测试用例名嵌入验收编号**：

```cpp
TEST(PreviewLayoutTest, T1_HighDpiNoExtraSpaceBelowCodeBlock) { ... }
```

## 当前 Spec 状态总览

见各模块目录的 `README.md`。

## 与 plans/ 的关系

- `specs/` 描述**系统应该是什么样子**（稳态）
- `plans/` 描述**如何从 A 迁移到 B**（变动）
- 实施完成后 plan 归档到 `plans/归档/`，对应 Spec 的 `status` 从 `draft` 升级为 `stable`

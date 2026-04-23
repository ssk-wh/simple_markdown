# SimpleMarkdown · AI 协作入口

> 本项目采用 **规范驱动开发（SDD）**：Spec 是第一公民，代码是 Spec 的投影。
> 所有改动必须**先更新 Spec 再改代码**。

---

## 黄金法则

1. **改动从 Spec 开始**，不从代码开始
2. **Bug 修复先问**：Spec 里是否定义过这个行为？
   - 写了 → 代码违反 Spec → 改代码
   - 没写 → Spec 有漏洞 → **先补 Spec 的 INV + T 条目，再改代码**
3. **代码必须引用 Spec**：源文件头部注释 `Spec: specs/xxx.md`
4. **测试用例嵌入 T 编号**：`TEST(PreviewLayoutTest, T1_HighDpiNoExtraSpace)`
5. **改完立即更新 CHANGELOG.md**：bug 修复 / 功能新增 / 可感知的行为改动，完成后立即记录到 CHANGELOG，不要拖到推送前

---

## 目录速览

```
specs/              ← 规范（稳态）
├── README.md                  ← Spec 模板与工作流
├── 00-产品愿景.md               ← 产品定位、非目标、性能预算
├── 10-系统架构.md               ← 模块划分、线程模型、数据流
├── 20-约束与不变量.md            ← 全局硬约束（必读）
├── 模块-core/                  ← 核心数据模型
├── 模块-editor/                ← 自绘编辑器
├── 模块-parser/                ← Markdown 解析
├── 模块-preview/               ← 自绘预览
├── 模块-sync/                  ← 滚动同步
├── 模块-app/                   ← 主窗口、多 Tab、会话
└── 横切关注点/                  ← 跨模块不变量
    ├── 10-线程模型.md
    ├── 20-坐标系统.md
    ├── 30-主题系统.md
    ├── 40-高DPI适配.md          ← ⚠️ 所有涉及字体/坐标的改动必读
    ├── 50-字符编码与IO.md
    ├── 60-国际化.md
    ├── 70-性能预算.md
    └── 80-字体系统.md           ← ⚠️ 所有涉及字号/字体的改动必读

plans/              ← 实施计划（变动）
├── README.md                  ← Plan 模板与生命周期
└── 归档/                       ← 已完成/废弃的 Plan

docs/               ← 人类向文档
└── 构建说明.md                  ← 编译/打包/诊断

src/                ← 源码（每个文件头部注明 Spec 路径）
tests/              ← 测试（按 Spec 的 T 编号组织）
tmp/                ← 临时文件（gitignored）
```

---

## ⚠ 本项目不使用 TODO 文件

全局 `~/.claude/CLAUDE.md` 要求"接到任务第一步写入 TODO 文件"——**本项目 override 该规则**。

原因：本项目采用 SDD，所有任务（需求 / Bug / 改进 / 小修）都以 Plan 文件存在于 `plans/` 目录，不再有独立的 TODO 文件。

- 接到新任务 → 在 `plans/` 下创建对应 Plan（极简或完整模板，见 `plans/README.md`）
- 查找当前工作 → 扫 `plans/` 下 `status: in_progress` 的文件
- 看还没开始的 → 扫 `plans/` 下 `status: draft` 的文件
- 看已完成的 → `CHANGELOG.md`

Ralph Loop 的"待办扫描"改为扫 `plans/*.md` 的 frontmatter `status` 字段（draft / in_progress 优先取最早日期）。

| 维度 | `plans/`（统一入口） |
|------|----------------------|
| 定位 | 所有工作项的承载地 |
| 粒度 | 极简模板 3-5 行；完整模板含背景/步骤/风险/验证 |
| 生命周期 | draft → in_progress → completed → 一周后归档 |
| 与 Spec 关系 | frontmatter 的 `related_specs` 字段引用 |
| 何时用 | 所有任务，无论大小 |

---

## 标准工作流

### 新功能 / 修改

1. **读 Spec**：`specs/README.md` → 找到相关模块 Spec 和横切 Spec
2. **改/写 Spec**：更新 `status`、`last_reviewed`；如涉及不变量变化，同步 `specs/20-约束与不变量.md`
3. **（可选）写 Plan**：复杂改动在 `plans/` 下写实施计划
4. **生成代码**：根据 Spec 的接口定义、INV、算法段落生成
5. **生成测试**：从 Spec 第 7 节「验收条件」的 T 编号推导
6. **对照 INV 自查**：逐条核对
7. **更新 CHANGELOG.md**：把可感知变更追加到最新条目（见下方「CHANGELOG 维护纪律」）
8. **归档 plan**：把 plan frontmatter 改为 `status: completed`，**立即 `mv plans/xxx.md plans/归档/xxx.md`**
9. **commit**：message 引用 Spec 路径

**commit message 示例**：
```
feat(preview): 实现 INV-2 子块绝对坐标传递

Spec: specs/模块-preview/03-绘制管线.md
测试: T1, T2, T6
```

### Bug 修复

1. **先查 Spec**：bug 对应的行为在 Spec 里定义了吗？
2. **补 Spec**（若无定义）：新增 INV + T 条目
3. **改代码**：对齐 Spec
4. **验证 T 条目**：跑对应测试
5. **更新 CHANGELOG.md**：把修复追加到最新 `### Fixed` 段
6. **归档 plan**（若有对应 plan）：`mv plans/xxx.md plans/归档/xxx.md`
7. **commit**：引用新的 INV 和 T 编号

---

## 代码 ↔ Spec 追溯

### 源文件头部

```cpp
// src/preview/PreviewLayout.h
//
// Spec: specs/模块-preview/02-布局引擎.md
// Invariants enforced here: INV-1, INV-2, INV-3, INV-6
// Last synced: 2026-04-13
#pragma once
// ...
```

### 测试用例命名

```cpp
TEST(PreviewLayoutTest, T1_HighDpiNoExtraSpaceBelowCodeBlock) { ... }
TEST(PreviewLayoutTest, T2_NestedListAbsoluteCoordinates) { ... }
```

---

## 全局硬规则（违反即 bug）

> 完整清单见 `specs/20-约束与不变量.md`。这里列**最常踩**的几条：

### 高 DPI
- **[INV-DPI-METRICS]** 所有 `QFontMetricsF` 必须带 `QPaintDevice*` 参数
- **[INV-DPI-CTOR]** 构造函数禁止计算 DPI 依赖的度量值
- **[INV-DPI-NO-HARDCODE]** 禁止硬编码 padding，必须用 `fm.height() * ratio`

### 坐标系统
- **[INV-COORD-ABS]** 递归绘制子块必须显式计算 `childAbsX/Y = absX/Y + child.bounds.x()/y()`

### 线程
- **[INV-THREAD-MAIN]** QPainter 和所有 QWidget 方法必须在主线程
- **[INV-THREAD-IMMUTABLE]** 跨线程共享必须是 immutable 或加锁

### 国际化（i18n）
- **[INV-I18N-TR]** 所有用户可见的字符串必须用 `tr()` 包裹，禁止硬编码中文或英文
- **[INV-I18N-TS]** 新增或修改 `tr()` 字符串后，必须同步更新 `translations/simple_markdown_zh_CN.ts` 和 `translations/simple_markdown_en_US.ts`，添加对应的 `<message>` 条目及翻译
- **[INV-I18N-QM]** 更新 .ts 文件后必须运行 `lrelease` 生成 .qm 文件，或依赖 cmake build 自动生成
- **[INV-I18N-CHECKLIST]** 新增 UI 功能的完成检查项：`tr()` 包裹 → .ts 补翻译 → lrelease → 构建验证

### 代码质量
- **[INV-CODE-UTF8]** C++ 源文件必须是 UTF-8 无 BOM（项目现状），修改时严禁变更 BOM 状态
- **[INV-CODE-RAII]** 禁止裸 new/delete
- **[INV-CODE-COMMENT-ZH]** 所有代码注释必须使用中文，禁止英文注释；新增或修改代码时遇到英文注释应顺手改为中文

---

## 高频命令速查

### 构建

```bat
build_on_win.bat release    :: Windows
./build_on_linux.sh release  # Linux
```

> **注意**：编译前 taskkill 仅在目标进程锁定了 build 目录下的 exe 时才需要。
> 如果用户启动的是从其他位置（如安装目录）运行的 SimpleMarkdown，不要杀死它。

### 打包

```bat
pack_on_win.bat              :: Windows (NSIS)
./pack_on_linux.sh           # Linux (deb)
```

### 测试

```bash
cd build && ctest -C Release --output-on-failure
```

详细构建/打包/诊断见 `docs/构建说明.md`。

---

## CHANGELOG 维护纪律

**核心要求**：任何改动代码产生的可感知影响（bug 修复、功能新增、性能优化、样式调整），完成后**立即**同步 `CHANGELOG.md`，不得拖到推送前再补。

**判定标准**：用户能否从构建结果感知到这个改动？

- 能感知 → 必须记（Fixed / Added / Changed / Improved 任选一段）
- 纯内部重构、Spec 梳理不改行为 → 可不记
- 但 SDD 级别的重大架构转型必须记（参见 0.2.3）

**版本号策略**：

- 头条 `## [x.y.z] - YYYY-MM-DD` 日期是今天 → 追加到现有条目（同日多次改动合并）
- 日期不是今天 → 新建 `## [x.y.z+1] - <today>`（微版本号递增）
- 重大架构变更 → 递增次版本号

**条目格式**：一行中文描述，动词在前，不加句号。例：

- `高 DPI 屏幕下代码块下方出现多余空白`（Fixed）
- `字号统一从中心化常量派生，避免编辑器与预览字号不一致`（Changed）

---

## 推送代码流程

当用户说"推送"/"push"时：

1. `git status` + `git log --oneline -5` 检查本地状态
2. 检查 `CHANGELOG.md` 是否已记录最新改动（格式 `## [x.y.z] - YYYY-MM-DD`）
3. 检查相关 Spec 是否已同步更新（SDD 要求）
4. 整理代码：确认无意外修改、无调试代码、无临时注释
5. `git add -A && git commit -m "type: 简短描述"`（conventional commits）
6. `git push`

不要简单地只跑 `git push`。

---

## Windows 编码注意

- 可能输出中文的命令前加 `chcp.com 65001 > /dev/null 2>&1;`
- 修改现有 C++ 源码前先检查编码；全部 NO-BOM UTF-8，保持不变；发现 GBK 需先转 NO-BOM UTF-8 再编辑

---

## 临时文件

测试脚本、临时数据、截图等放 `tmp/`（已在 `.gitignore`），不要散落在项目目录。

---

## 需要深入阅读时

| 问题 | 去哪查 |
|------|--------|
| 产品该做什么 / 不做什么 | `specs/00-产品愿景.md` |
| 模块划分 / 数据流 | `specs/10-系统架构.md` |
| 全局硬约束完整清单 | `specs/20-约束与不变量.md` |
| 高 DPI 陷阱与修复 | `specs/横切关注点/40-高DPI适配.md` |
| 坐标系统递归规则 | `specs/横切关注点/20-坐标系统.md` |
| 线程通信规则 | `specs/横切关注点/10-线程模型.md` |
| 字号 / 字体 / 字体族规则 | `specs/横切关注点/80-字体系统.md` |
| 编译/打包/诊断 | `docs/构建说明.md` |
| 版本历史 | `CHANGELOG.md` |

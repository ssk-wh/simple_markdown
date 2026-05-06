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
6. **CHANGELOG 条目必须面向终端用户**：禁止出现 commit hash、源码路径、函数 / 类 / 变量名、INV 编号、内部算法常量、设计文档式根因分析（详见下方「§ CHANGELOG 维护纪律 → 写作风格」）
7. **新增交互行为必须写入 Spec**：涉及用户可感知的交互逻辑（显隐规则、快捷键、默认状态、宽度策略等）时，先在 `specs/` 对应模块下创建或更新 Spec，再写代码。交互规则不在 Spec 中 = 未定义行为

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

### 编译后自动跑单元测试

`build_on_win.bat` / `build_on_linux.sh` 完成编译后**默认**运行 `ctest`：

- 默认排除 `LABELS "perf"`（如 `PreviewRenderBenchmark`），减少日常构建尾巴时长
- 任意测试 fail → build 返回非 0，`pack_on_win.bat`（先调 build）因此**拒绝打包**——质量门 fail-fast
- 加 `--skip-tests` 或 `--no-tests` 参数跳过（开发迭代场景）

**[INV-BUILD-RUN-TESTS]** 任何 PR 在打包/发布前必须经本地 build 脚本默认路径跑一遍（不带
`--skip-tests`），让 ctest 自动验证；CI 流水线另有独立测试，本地强制是双保险。

### Qt DLL 自动部署（Windows）

`build_on_win.bat` 在编译完成后会**自动**做两件事：

1. 对 `build/src/app/SimpleMarkdown.exe` 跑 `windeployqt`，把 Qt 运行时（`Qt5*.dll` + `platforms/qwindows.dll` 等）拷到 exe 同目录
2. **把同一组 Qt DLL 镜像到 `build/tests/`**——测试 exe 也需要这些 DLL，否则 `ctest` 启动测试时报 `0xC000007B` (`STATUS_INVALID_IMAGE_FORMAT`)，本质是 DLL 缺失
3. 额外把 `Qt5Test.dll` 从 `<QT_DIR>/bin` 复制到 `build/tests/`——`SimpleMarkdown.exe` 本身不依赖 `Qt5::Test`，但部分单元测试用 `QSignalSpy` 等组件依赖它

约束：

- **[INV-BUILD-QT-MIRROR]** Windows 上任何新增的 ctest 目标如果依赖 Qt 模块（含 `Qt5::Test`），不需要在 CMake 里写 `add_custom_command(POST_BUILD ... copy)`，统一由 `build_on_win.bat` 末尾的 mirror 段处理。如果未来需要其他 Qt 模块（如 `Qt5Sql`、`Qt5Multimedia`），同步在 mirror 段补一行复制
- 该机制对**已存在**的 DLL 跳过（`if not exist ... copy`），所以增量构建零开销

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
- `字号统一对齐，避免编辑器与预览字号不一致`（Changed）

### 写作风格（强制规则）

**受众定义**：CHANGELOG 的读者是 SimpleMarkdown 的**终端用户**——写作者、笔记用户、文档编辑者。**假设他们不读源码、不懂 Qt、不知道仓库结构、不关心 commit hash**。他们想知道的只是：「这个版本对我有什么变化？」

**判定原则**：**如果一条记录的核心信息只有开发者能看懂，要么改写成用户视角，要么直接删除（移到 commit message 里）**。

**保留**（应当出现在 CHANGELOG）：

- 用户可感知的事实：做了什么动作、得到什么结果、修复了什么现象
- 操作路径：菜单层级、快捷键、UI 元素名（"视图 → 主题"、"Ctrl+B"）
- 用户可验证的行为：「重启后仍恢复」、「切换主题后正常显示」
- 用户场景：在哪种情况下能感知到改动（一行内括号补充即可）

**禁止**（必须从 CHANGELOG 移除或改写）：

- ❌ commit hash / SHA（如 `4447bf7`、`撤销 abc1234`）
- ❌ 源码路径（如 `src/...`、`MainWindow.cpp`）
- ❌ 函数 / 类 / 变量名（含 `m_xxx` 私有成员、`QXxx` Qt 类名、camelCase 内部标识符）
- ❌ INV / T 编号（项目内部 Spec 索引，属于内部追溯系统）
- ❌ 算法常量与公式（如 `screen/8`、`maxW=256`、`fm.height() * 1.5`）
- ❌ 内部实现替换说明（如 "从 X 算法改为 Y 算法"、"改用 setWindowState"、"restoreState 覆盖了"）
- ❌ 设计文档式根因分析（这些归 plan / commit message，不归 CHANGELOG）
- ❌ 测试用例命名（如 `TocPanelWidthTest.LongTitleExpandsWidth`）
- ❌ 编译 / CI / 测试基础设施修复（用户感知不到 → 直接删除条目）
- ❌ Spec 路径、配置 key 名（如 `view/leftPanelWidth`、`QSettings`）

**风格约束**：

- 单行短句，动词在前，用户语气
- 中文为主；专有术语保持原文（如 Markdown / TOC / Liquid Glass / GitHub）
- 按动作分类：`Added` / `Fixed` / `Changed` / `Improved` / `Removed`
- 一条一句话，必要时一行内括号补一个用户场景，不展开技术细节
- 重大架构变更可写入 `Changed`，但措辞仍以用户能感知的影响为主

**良好示例**：

| 反面示例（含开发者细节） | 改写后（用户视角） |
|--------------------------|--------------------|
| `默认基准从 m_mainSplitter->width() 改为"应用所在屏幕宽度 / 8"` | `资源管理器和目录面板的宽度在用户调整后能在重启时正确恢复` |
| `修复 INV-TOC-WIDTH-MAX 在 headless 测试环境下 maxW=256 触发上界塌穿下界` | （删除——属于测试基础设施修复，用户无感） |
| `撤销 4447bf7 编辑器焦点 F3 跳转匹配项实现` | `撤销编辑器焦点下 F3 / Shift+F3 跳转匹配项的尝试（多次实现均验证未通过，已废弃）` |
| `restoreState 覆盖了面板宽度，showEvent 中重置` | `"全部显示"标签栏模式下重启应用时左侧面板可能不显示` |
| `Linux CI 构建缺少 Qt5LinguistTools 导致打包失败` | （删除——CI 配置问题，用户无感） |

**自检清单**（提交前对照）：

- [ ] 没有出现 `m_` / `->` / `QXxx` / commit hash 模式 `[a-f0-9]{7}` / `INV-` / `T\d+` / `screen/` 等开发者标识
- [ ] 没有引用源码文件路径或函数名
- [ ] 每条都能被一个普通用户读懂"我能看到什么变化"
- [ ] 测试 / CI / 编译相关的修复已经删除或挪到 commit message
- [ ] 内部算法 / 常量 / 实现替换说明已抽象为用户感知层面的事实

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

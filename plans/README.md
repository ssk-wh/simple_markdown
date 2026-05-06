# 实施计划目录（Plans）

## 定位

`plans/` 描述**如何从当前状态迁移到目标状态**（变动），与 `specs/`（稳态）互补。

- **specs/**：系统应该是什么样子（稳态规范）
- **plans/**：所有工作项的承载地——需求、Bug、改进、小修都在这里以 Plan 文件形式存在

> **本项目不再使用 TODO 文件**。所有任务（包括小修小补）都进 `plans/`；项目级 CLAUDE.md 已 override 全局 `~/.claude/CLAUDE.md` 中的 TODO 规则。

## 何时写 Plan

| 改动规模 | 是否需要 Plan |
|---------|---------------|
| 单文件 bug 修复 | **极简 Plan** |
| 单模块小改 | **极简 Plan** |
| 跨模块功能 | **必须（完整 Plan）** |
| 架构级改动 | **必须（完整 Plan）** |
| 新增横切约束 | **必须（完整 Plan）** |

## 文件命名

**强制规则**：`plans/` 和 `plans/归档/` 下所有 plan 文件**必须**以 `YYYY-MM-DD-` 为前缀，
日期取自 plan 的**创建日期**（与 frontmatter 的 `date` / `created` 字段一致）。
唯一例外是本目录的 `README.md`（目录说明文件）。

格式：`YYYY-MM-DD-简短描述.md`，例如：

- `2026-04-13-会话恢复增强.md`
- `2026-04-15-TOC面板重构.md`
- `2026-05-06-内容标记持久化未实现.md`

约束：

- **同一日期下多个 Plan**：用文件名后缀的简短中文描述区分，无需修改日期前缀
- **重新打开已归档的 plan**：保留**原始创建日期**前缀，不要用重新打开的日期
  （日期表示「立项时间」，便于按时间序排序追溯，不是「最近活动时间」）
- **plan 命名变更后内部引用同步**：如 CHANGELOG / Spec 的 plan 路径指向、其他 plan 的
  `related_plans` 字段等都要更新到新文件名
- **审计命令**：`find plans -maxdepth 2 -name '*.md' | grep -v -E '(README\.md|/[0-9]{4}-[0-9]{2}-[0-9]{2}-)'`
  应当返回空——CI 或 commit hook 可加这条作为 lint 拦截无前缀文件入库

历史背景：2026-05-06 起强制此规则，之前归档目录有 8 个无前缀的旧 plan 已批量补齐。

## Plan 文件模板（完整版）

适用于跨模块功能、架构级改动等需要详细计划的任务。

```markdown
---
date: 2026-04-13
status: in_progress | completed | aborted
related_specs: [specs/模块-app/06-会话恢复.md]
---

# 会话恢复增强

## 背景
为什么需要这个改动？（业务/技术驱动力）

## 目标
要达成的状态，最好能引用 Spec 的 INV 编号。

## 影响范围
- 修改：哪些文件/模块
- 新增：哪些文件
- 删除：哪些文件
- Spec 变更：哪些 Spec 需要同步更新

## 步骤拆分
- [ ] Step 1: ...
- [ ] Step 2: ...
- [ ] Step 3: ...

## 风险与回滚
- 风险 1：...
  - 缓解：...
- 回滚方案：git revert 或者分阶段开关

## 验证清单
- [ ] 所有相关 Spec 的 T 条目通过
- [ ] 新增的不变量已写入 Spec
- [ ] CHANGELOG.md 更新
```

## 极简 Plan 模板

适用于改常量、修 typo、单行修改、小型配置调整等 3-5 行能说清的任务。仍然遵循 plans/ 的生命周期。

```markdown
---
date: YYYY-MM-DD
status: draft | in_progress | completed | aborted
related_specs: [...]
---

# 任务标题

## 背景
一句话为什么做。

## 动作
- [ ] Step 1
- [ ] Step 2

## 验收
一两条可观察的验证条件（对应 Spec 的 T 编号更好）。
```

## 生命周期

```
draft → in_progress → completed → 立即 mv 到 plans/归档/
                   ↘ aborted     → 立即 mv 到 plans/归档/
```

- **draft**：还没开始做，仅作为待办列表
- **in_progress**：正在执行
- **completed**：做完且经过验证——**此状态必须立即归档**
- **aborted**：废弃——**同样立即归档**

**强制规则（2026-04-14）**：plan 完成（status 改为 completed）后**必须在同一个工作流步骤内**执行
`mv plans/xxx.md plans/归档/xxx.md`，不等一周、不等 batch 提交。这样 `plans/` 根目录只保留
draft + in_progress 状态的任务，视觉上就是"还没做完的清单"，一目了然。

## 归档

`plans/归档/` 存放已完成或废弃的 Plan，作为历史参考。归档时**不改内容**，只移动位置。

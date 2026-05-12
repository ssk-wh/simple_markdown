---
date: 2026-05-07
status: draft
related_specs:
  - CLAUDE.md   # CHANGELOG 写作纪律
---

# CHANGELOG 提醒 hook 加 commit type 白名单

## 背景
项目级 hook（`PostToolUse` 类型，触发于 `git commit` 后）会无条件提醒「本次提交未包含
CHANGELOG.md 更新。根据项目规范，可感知的改动必须同步更新 CHANGELOG.md」。

但 CLAUDE.md「CHANGELOG 维护纪律」明文规则**禁止**以下 commit 类型进入 CHANGELOG：
- `chore:` 内部纪律改动（plans 命名规范、构建脚本调整）
- `docs:` 仅文档/plan 草稿/反模式沉淀
- `test:` 仅测试基础设施
- 「内部资源勘误」（如修一个内部链接 typo）

2026-05-06 ~ 2026-05-07 一轮 ralph loop 中 hook **重复误报至少 4 次**：
- `4036d24 chore: 统一 plans 命名规范`
- `8989edd docs: 新增 5 项待办 plan 草稿`
- `cbe1b9f docs: 新增 1 项待办 plan 草稿`
- `（本条 plan 提交时也会误报）docs: CLAUDE.md 复盘沉淀`

每次都是「人工判定为误报 → 忽略」的循环——浪费注意力，且让真正应当 enforced 的提醒变弱。

仅记录待办，本次不实施。

## 动作（实施时）
- [ ] 找到该 hook 脚本（项目级 .claude/hooks/ 或全局 ~/.claude/hooks/）
- [ ] 读取 commit message（hook 入参 / 从 `git log -1 --format=%s` 取）
- [ ] 解析前缀 type，对以下 type 跳过提醒：
  - `chore:` / `docs:` / `test:` / `style:` / `refactor:`（仅当 refactor 不改用户可感知行为时）
- [ ] 仅 `feat:` / `fix:` / `perf:` 才检查 CHANGELOG.md 是否在本 commit 中
- [ ] 测试用例：分别用 4 种 type 模拟提交，断言 hook 输出符合预期

## 验收
- `chore:` / `docs:` / `test:` commit 不再触发 CHANGELOG 提醒
- `feat:` / `fix:` / `perf:` commit 若没改 CHANGELOG.md 仍触发提醒
- 极少数边界情况（如 `refactor:` 涉及用户可感知行为）人工判定后再决定是否例外

## 备注
- 这条 plan 本身记录了「同一语义两份独立实现走样」的元案例——hook 自动化纪律 vs CLAUDE.md
  人工写作纪律不对齐，正是 CLAUDE.md 反模式清单 B 描述的形态
- 如果 hook 是全局 `~/.claude/hooks/` 下的，改动会影响所有项目；建议**项目级 override**
  （在项目 `.claude/hooks/` 加同名脚本覆盖）
- 优先级：低 P2——每次误报"代价"小（一行说明），但累计起来侵蚀注意力

## 进展

- 2026-05-11 尝试在本会话实施 → Edit 工具修改 `.claude/hooks/check_changelog.py`
  被 don't-ask-mode 安全策略拒绝（`.claude/` 元配置受保护）
- **下一步**：需用户**手动**应用以下补丁（或临时放开权限重跑）：
  - 加 `USER_FACING_TYPES = {"feat", "fix", "perf"}` 集合
  - 加 `INTERNAL_TYPES = {"chore", "docs", "test", "style", "refactor", "ci", "build"}` 集合
  - 用 `git log -1 --format=%s` 取最新 commit subject，正则 `^([a-z]+)(?:\([^)]+\))?:` 解析 type
  - 仅 `commit_type in USER_FACING_TYPES` 时检查 CHANGELOG.md 是否被改；其他 type 直接 `sys.exit(0)`
- 保留 draft 等用户决定

"""PostToolUse hook: git commit 后检查是否更新了 CHANGELOG.md

仅对用户可感知 commit type（feat/fix/perf）做检查，其他 type
（chore/docs/test/style/refactor/ci/build）跳过，对齐 CLAUDE.md
「CHANGELOG 维护纪律」：只有可感知改动才需要 CHANGELOG 记录。

判断逻辑抽成纯函数 should_warn(subject, files) 便于单元测试。
"""
import sys
import json
import re
import subprocess

# 用户可感知的 commit type（必须配套更新 CHANGELOG）
USER_FACING_TYPES = {"feat", "fix", "perf"}
# 内部改动 type（不要求 CHANGELOG，跳过提醒）
INTERNAL_TYPES = {"chore", "docs", "test", "style", "refactor", "ci", "build"}

# conventional commits 前缀解析：type 或 type(scope)，后跟冒号
_TYPE_RE = re.compile(r"^([a-z]+)(?:\([^)]+\))?:")


def parse_commit_type(subject):
    """从 commit subject 中解析出 type 前缀；解析失败返回 None"""
    m = _TYPE_RE.match(subject)
    return m.group(1) if m else None


def should_warn(subject, changed_files):
    """判断是否应当提醒补 CHANGELOG。

    True  → 用户可感知 type 且未改 CHANGELOG.md
    False → 内部 type / 未识别 type / 已包含 CHANGELOG.md
    """
    commit_type = parse_commit_type(subject)
    # 仅用户可感知 type 才检查；其他 type（含 INTERNAL_TYPES 与未识别）一律跳过
    if commit_type not in USER_FACING_TYPES:
        return False
    return "CHANGELOG.md" not in changed_files


def main():
    data = json.load(sys.stdin)
    cmd = data.get("tool_input", {}).get("command", "")

    # 只在 git commit 命令后触发
    if "git commit" not in cmd:
        return

    try:
        subject = subprocess.check_output(
            ["git", "log", "-1", "--format=%s"],
            text=True, stderr=subprocess.DEVNULL
        ).strip()
        files = subprocess.check_output(
            ["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD"],
            text=True, stderr=subprocess.DEVNULL
        ).strip()

        if should_warn(subject, files):
            commit_type = parse_commit_type(subject) or "?"
            print(json.dumps({
                "hookSpecificOutput": {
                    "hookEventName": "PostToolUse",
                    "additionalContext": (
                        f"[CHANGELOG 提醒] 本次提交 ({commit_type}) 未包含 CHANGELOG.md 更新。"
                        "根据项目规范，可感知的改动必须同步更新 CHANGELOG.md。"
                    )
                }
            }))
    except Exception:
        pass


if __name__ == "__main__":
    main()

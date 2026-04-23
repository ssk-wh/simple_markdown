"""PostToolUse hook: git commit 后检查是否更新了 CHANGELOG.md"""
import sys
import json
import subprocess

data = json.load(sys.stdin)
cmd = data.get("tool_input", {}).get("command", "")

# 只在 git commit 命令后触发
if "git commit" not in cmd:
    sys.exit(0)

try:
    files = subprocess.check_output(
        ["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD"],
        text=True, stderr=subprocess.DEVNULL
    ).strip()
    if "CHANGELOG.md" not in files:
        print(json.dumps({
            "hookSpecificOutput": {
                "hookEventName": "PostToolUse",
                "additionalContext": (
                    "[CHANGELOG 提醒] 本次提交未包含 CHANGELOG.md 更新。"
                    "根据项目规范，可感知的改动必须同步更新 CHANGELOG.md。"
                )
            }
        }))
except Exception:
    pass

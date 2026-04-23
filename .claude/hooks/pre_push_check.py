"""PostToolUse hook: git push 后自动打 tag 并推送"""
import sys
import json
import subprocess
import re

data = json.load(sys.stdin)
cmd = data.get("tool_input", {}).get("command", "")

if "git push" not in cmd:
    sys.exit(0)

# 避免 git push --tags / git push origin vX.Y.Z 时重复触发
if "--tags" in cmd or re.search(r"v\d+\.\d+\.\d+", cmd):
    sys.exit(0)

try:
    # 从 CHANGELOG.md 读取最高版本号
    with open("CHANGELOG.md", "r", encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^## \[(\d+\.\d+\.\d+)\]", line)
            if m:
                version = m.group(1)
                tag = f"v{version}"
                break
        else:
            sys.exit(0)

    # 检查该 tag 是否已存在
    existing = subprocess.run(
        ["git", "tag", "-l", tag],
        capture_output=True, text=True
    ).stdout.strip()

    if existing:
        sys.exit(0)

    # 创建并推送 tag
    subprocess.run(["git", "tag", tag], check=True)
    subprocess.run(["git", "push", "origin", tag], check=True,
                   capture_output=True, text=True)

    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": f"[自动打 Tag] 已创建并推送 {tag}"
        }
    }))

except Exception as e:
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": f"[Tag 警告] 自动打 Tag 失败: {e}"
        }
    }))

"""PostToolUse hook: git push 后自动打 tag 并推送（自愈：检查远端而非仅本地）"""
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


def out(msg):
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": msg,
        }
    }))


try:
    # 从 CHANGELOG.md 读取最高版本号
    with open("CHANGELOG.md", "r", encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^## \[(\d+\.\d+\.\d+)\]", line)
            if m:
                tag = f"v{m.group(1)}"
                break
        else:
            sys.exit(0)

    # 本地 tag：不存在则创建（指向当前 HEAD）
    local = subprocess.run(["git", "tag", "-l", tag],
                           capture_output=True, text=True).stdout.strip()
    if not local:
        subprocess.run(["git", "tag", tag], check=True)

    # 关键：检查**远端**是否已有该 tag（而非仅看本地——否则一旦本地建了 tag
    # 但推送失败/超时，后续永远 exit(0) 不再重推，tag 永久卡在本地）。
    remote = subprocess.run(
        ["git", "ls-remote", "--tags", "origin", tag],
        capture_output=True, text=True, timeout=45,
    ).stdout.strip()

    if remote:
        # 远端已有：确认本地 tag 与远端指向一致即可，无需重推
        sys.exit(0)

    # 远端缺失 → 推送（自愈：覆盖"上次创建了本地 tag 但没推成功"的情况）
    subprocess.run(["git", "push", "origin", tag], check=True,
                   capture_output=True, text=True, timeout=90)
    out(f"[自动打 Tag] 已创建并推送 {tag}")

except subprocess.TimeoutExpired:
    out(f"[Tag 警告] 推送 {tag} 超时——本地 tag 已建，下次 push 会自愈重推；"
        f"或手动 git push origin {tag}")
except Exception as e:
    out(f"[Tag 警告] 自动打 Tag 失败: {e}")

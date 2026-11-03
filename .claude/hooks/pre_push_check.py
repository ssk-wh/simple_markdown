"""PostToolUse hook: git push 后自动打 tag 并推送（自愈：检查远端而非仅本地）

触发：任何包含 `git push` 的 Bash 命令（含 `git add && git commit ; git push` 这类复合命令）。
动作：读取 CHANGELOG.md 最高版本号 → 本地缺 tag 则创建 → 远端缺 tag 则推送。
"""
import sys
import json
import subprocess
import re

data = json.load(sys.stdin)
cmd = data.get("tool_input", {}).get("command", "")

# 复合命令兼容：在命令文本任意位置匹配 git push，只取这一段的参数
m_push = re.search(r"\bgit\s+push\b([^&;|\n]*)", cmd)
if not m_push:
    sys.exit(0)

# 避免 git push --tags / git push origin vX.Y.Z 本身再次触发（只看 push 这一段的参数，
# 不因整条命令里别处出现版本号字符串而误跳过）
push_args = m_push.group(1)
if "--tags" in push_args or re.search(r"(^|\s)v\d+\.\d+\.\d+(\s|$)", push_args):
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
        # 远端已有：无需重推
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

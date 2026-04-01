#!/usr/bin/env python3
"""从 CHANGELOG.md 生成 debian/changelog。

用法:
    python3 scripts/gen_debian_changelog.py [--package NAME] [--maintainer 'Name <email>']

CHANGELOG.md 格式要求:
    ## [版本号] - YYYY-MM-DD
    ### Added / Fixed / Changed / ...
    - 条目内容
"""

import re
import sys
import argparse
from datetime import datetime
from email.utils import format_datetime


def parse_changelog(text):
    """解析 CHANGELOG.md，返回版本列表。"""
    versions = []
    current = None

    for line in text.splitlines():
        # 匹配版本标题: ## [0.2.0] - 2026-04-01
        m = re.match(r'^## \[(.+?)\]\s*-\s*(\d{4}-\d{2}-\d{2})', line)
        if m:
            if current:
                versions.append(current)
            current = {
                'version': m.group(1),
                'date': datetime.strptime(m.group(2), '%Y-%m-%d'),
                'entries': [],
            }
            continue

        if current is None:
            continue

        # 跳过分类标题 (### Added 等)
        if line.startswith('### '):
            continue

        # 收集条目 (- xxx)
        m = re.match(r'^- (.+)', line.strip())
        if m:
            current['entries'].append(m.group(1))

    if current:
        versions.append(current)

    return versions


def to_debian_changelog(versions, package='simplemarkdown',
                        maintainer='SimpleMarkdown Developers <dev@simplemarkdown.org>'):
    """将解析后的版本列表转换为 debian/changelog 格式。"""
    blocks = []
    for v in versions:
        # RFC 2822 日期格式
        dt = v['date'].replace(hour=12)
        date_str = format_datetime(dt)

        lines = [f"{package} ({v['version']}) unstable; urgency=medium", '']
        for entry in v['entries']:
            lines.append(f"  * {entry}")
        lines.append('')
        lines.append(f" -- {maintainer}  {date_str}")
        blocks.append('\n'.join(lines))

    return '\n\n'.join(blocks) + '\n'


def main():
    parser = argparse.ArgumentParser(description='从 CHANGELOG.md 生成 debian/changelog')
    parser.add_argument('--package', default='simplemarkdown', help='包名')
    parser.add_argument('--maintainer',
                        default='SimpleMarkdown Developers <dev@simplemarkdown.org>',
                        help='维护者 (Name <email>)')
    parser.add_argument('--input', default='CHANGELOG.md', help='输入文件')
    parser.add_argument('--output', default='debian/changelog', help='输出文件')
    args = parser.parse_args()

    with open(args.input, 'r', encoding='utf-8') as f:
        text = f.read()

    versions = parse_changelog(text)
    if not versions:
        print(f"[ERROR] 未在 {args.input} 中找到版本信息", file=sys.stderr)
        sys.exit(1)

    result = to_debian_changelog(versions, args.package, args.maintainer)

    with open(args.output, 'w', encoding='utf-8') as f:
        f.write(result)

    print(f"[OK] 从 {args.input} 提取 {len(versions)} 个版本 → {args.output}")


if __name__ == '__main__':
    main()

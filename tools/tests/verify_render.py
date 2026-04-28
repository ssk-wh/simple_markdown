#!/usr/bin/env python3
"""
SimpleMarkdown 渲染验证脚本
读取 render_blocks.json，执行 9 项自动化检查。

用法: python verify_render.py [json_path] [--verbose]
默认路径: %TEMP%/render_blocks.json
"""

import json
import sys
import os
from pathlib import Path
from collections import defaultdict


class RenderVerifier:
    def __init__(self, data, verbose=False):
        self.data = data
        self.blocks = data.get("blocks", [])
        self.viewport_w = data.get("viewport_width", 0)
        self.viewport_h = data.get("viewport_height", 0)
        self.verbose = verbose
        self.results = []  # (check_name, passed, details)

    def run_all(self):
        """运行所有检查"""
        self.check_overlap()
        self.check_extra_gaps()
        self.check_font_hierarchy()
        self.check_font_consistency()
        self.check_content_completeness()
        self.check_alignment()
        self.check_bullet_occlusion()
        self.check_codeblock_padding()
        self.check_viewport_bounds()
        self.check_zero_height()
        self.check_inline_content()
        return self.results

    def _add(self, name, passed, details=""):
        self.results.append((name, passed, details))
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {name}")
        if details and (not passed or self.verbose):
            for line in details.strip().split("\n"):
                print(f"         {line}")

    # ---- 检查 1: 重叠检测 ----
    def check_overlap(self):
        issues = []
        self._check_overlap_recursive(self.blocks, "root", issues)
        self._add("重叠检测", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_overlap_recursive(self, blocks, parent_path, issues):
        for i in range(len(blocks)):
            for j in range(i + 1, len(blocks)):
                a, b = blocks[i], blocks[j]
                # 检查二维重叠（必须水平和垂直都有交集才算重叠）
                # 水平交集
                h_overlap = (a["x"] < b["x"] + b["width"]) and (b["x"] < a["x"] + a["width"])
                # 垂直交集
                a_bottom = a["y"] + a["height"]
                b_bottom = b["y"] + b["height"]
                v_overlap = (a["y"] < b_bottom - 1) and (b["y"] < a_bottom - 1)  # 1px 容差
                if h_overlap and v_overlap:
                    issues.append(
                        f"{parent_path}[{i}]({a['type']}) 与 [{j}]({b['type']}) "
                        f"二维重叠 (a:{a['x']},{a['y']},{a['width']}x{a['height']} "
                        f"b:{b['x']},{b['y']},{b['width']}x{b['height']})")
            # 递归检查子块
            children = blocks[i].get("children", [])
            if children:
                self._check_overlap_recursive(
                    children, f"{parent_path}[{i}]({blocks[i]['type']})", issues)

    # ---- 检查 2: 多余空行/间距异常 ----
    def check_extra_gaps(self):
        issues = []
        self._check_gaps_recursive(self.blocks, "root", issues)
        self._add("间距异常", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_gaps_recursive(self, blocks, parent_path, issues):
        # 预期间距阈值（像素）
        MAX_GAP = 80  # 两个块之间最大合理间距

        for i in range(len(blocks) - 1):
            a, b = blocks[i], blocks[i + 1]
            a_bottom = a["y"] + a["height"]
            gap = b["y"] - a_bottom
            if gap > MAX_GAP:
                issues.append(
                    f"{parent_path}[{i}]({a['type']}) → [{i+1}]({b['type']}) "
                    f"间距 {gap}px 超过阈值 {MAX_GAP}px")
            if gap < -1:  # 负间距 = 重叠（已在 check_overlap 检查）
                pass

        for i, block in enumerate(blocks):
            children = block.get("children", [])
            if children:
                self._check_gaps_recursive(
                    children, f"{parent_path}[{i}]({block['type']})", issues)

    # ---- 检查 3: 字体层级 ----
    def check_font_hierarchy(self):
        headings = {}  # level -> font_size
        self._collect_headings(self.blocks, headings)

        issues = []
        levels = sorted(headings.keys())
        for i in range(len(levels) - 1):
            l1, l2 = levels[i], levels[i + 1]
            s1, s2 = headings[l1], headings[l2]
            if s1 <= s2:
                issues.append(f"H{l1}({s1}pt) <= H{l2}({s2}pt)，层级反转")

        # 检查最小标题是否大于段落字体
        para_size = self._get_paragraph_font_size(self.blocks)
        if para_size and levels:
            min_heading_size = headings[levels[-1]]
            if min_heading_size <= para_size:
                issues.append(
                    f"H{levels[-1]}({min_heading_size}pt) <= 段落({para_size}pt)")

        self._add("字体层级", len(issues) == 0,
                  "\n".join(issues) if issues else
                  f"层级正确: {', '.join(f'H{l}={headings[l]}pt' for l in levels)}"
                  + (f", 段落={para_size}pt" if para_size else ""))

    def _collect_headings(self, blocks, headings):
        for b in blocks:
            if b["type"] == "heading" and "heading_level" in b and "font_size" in b:
                level = b["heading_level"]
                size = b["font_size"]
                if level not in headings or headings[level] < size:
                    headings[level] = size
            for child in b.get("children", []):
                self._collect_headings([child], headings)

    def _get_paragraph_font_size(self, blocks):
        for b in blocks:
            if b["type"] == "paragraph" and "font_size" in b:
                return b["font_size"]
            for child in b.get("children", []):
                result = self._get_paragraph_font_size([child])
                if result:
                    return result
        return None

    # ---- 检查 4: 字体一致性 ----
    def check_font_consistency(self):
        type_fonts = defaultdict(set)  # type -> set of (family, size)
        self._collect_fonts(self.blocks, type_fonts)

        issues = []
        for block_type, fonts in type_fonts.items():
            if block_type == "heading":
                continue  # 标题不同级别字体不同，跳过
            if len(fonts) > 1:
                fonts_str = ", ".join(f"{f}@{s}pt" for f, s in fonts)
                issues.append(f"{block_type}: 字体不一致 [{fonts_str}]")

        self._add("字体一致性", len(issues) == 0,
                  "\n".join(issues) if issues else "")

    def _collect_fonts(self, blocks, type_fonts):
        for b in blocks:
            if "font_family" in b and "font_size" in b:
                type_fonts[b["type"]].add((b["font_family"], b["font_size"]))
            for child in b.get("children", []):
                self._collect_fonts([child], type_fonts)

    # ---- 检查 5: 内容完整性 ----
    def check_content_completeness(self):
        type_counts = defaultdict(int)
        self._count_types(self.blocks, type_counts)

        issues = []
        # 基本检查：必须有块
        if not self.blocks:
            issues.append("blocks 数组为空")

        # 检查是否有未知类型
        if type_counts.get("unknown", 0) > 0:
            issues.append(f"存在 {type_counts['unknown']} 个 unknown 类型块")

        details = "块类型统计: " + ", ".join(
            f"{t}={c}" for t, c in sorted(type_counts.items()))

        self._add("内容完整性", len(issues) == 0,
                  (("\n".join(issues) + "\n") if issues else "") + details)

    def _count_types(self, blocks, counts):
        for b in blocks:
            counts[b["type"]] += 1
            for child in b.get("children", []):
                self._count_types([child], counts)

    # ---- 检查 6: 对齐验证 ----
    def check_alignment(self):
        issues = []
        self._check_list_alignment(self.blocks, "root", issues)
        self._add("对齐验证", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_list_alignment(self, blocks, parent_path, issues):
        for i, block in enumerate(blocks):
            if block["type"] == "list":
                children = block.get("children", [])
                if len(children) >= 2:
                    x_values = [c["x"] for c in children]
                    if len(set(x_values)) > 1:
                        issues.append(
                            f"{parent_path}[{i}](list) 子项 x 坐标不对齐: "
                            f"{x_values}")
            children = block.get("children", [])
            if children:
                self._check_list_alignment(
                    children, f"{parent_path}[{i}]({block['type']})", issues)

    # ---- 检查 7: 序号/圆点遮挡 ----
    def check_bullet_occlusion(self):
        issues = []
        self._check_bullets(self.blocks, "root", issues)
        self._add("序号遮挡", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_bullets(self, blocks, parent_path, issues):
        for i, b in enumerate(blocks):
            if b.get("bullet_x") is not None and b["bullet_x"] >= 0:
                bx = b["bullet_x"]
                bw = b.get("bullet_width", 0)
                bullet_right = bx + bw
                content_x = b["x"]  # ListItem 的内容区域 X

                # 检查1: 序号必须在内容左侧（不被遮挡）
                if bullet_right > content_x + 2:  # 2px 容差
                    issues.append(
                        f"{parent_path}[{i}]({b['type']}): "
                        f"序号右边界({bullet_right}) > 内容左边界({content_x})，序号被遮挡")

                # 检查2: 序号 Y 坐标必须与 ListItem Y 对齐
                by = b.get("bullet_y", -1)
                if by >= 0 and abs(by - b["y"]) > 5:  # 5px 容差
                    issues.append(
                        f"{parent_path}[{i}]({b['type']}): "
                        f"序号 Y({by}) 与内容 Y({b['y']}) 偏差 {abs(by - b['y'])}px")

            children = b.get("children", [])
            if children:
                self._check_bullets(
                    children, f"{parent_path}[{i}]({b['type']})", issues)

    # ---- 检查 8: 代码块空白 ----
    def check_codeblock_padding(self):
        issues = []
        self._check_codeblocks(self.blocks, "root", issues)
        self._add("代码块空白", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_codeblocks(self, blocks, parent_path, issues):
        # 估算每行能容纳的等宽字符数（保守: viewport / 10px per char）
        # 用于反推超长行被软换行后的真实显示行数，避免把"软换行"误判为"多余空白"
        chars_per_line = max(20, self.viewport_w // 10)
        for i, b in enumerate(blocks):
            if b["type"] == "code_block" and "content" in b:
                content = b["content"]
                # 真实显示行数 = 每个源代码行按软换行展开后的行数之和
                visual_lines = sum(
                    max(1, (len(line) + chars_per_line - 1) // chars_per_line)
                    for line in content.split("\n")
                )
                # 代码块高度 = padding(上下各8px) + 行数 * 行高
                # 合理行高范围: 18-28px (含行间距)
                # 最大合理高度 = 16 + visual_lines * 28 + 16 (额外容差)
                max_expected = 16 + visual_lines * 28 + 16
                if b["height"] > max_expected:
                    excess = b["height"] - max_expected
                    src_lines = content.count("\n") + 1
                    issues.append(
                        f"{parent_path}[{i}](code_block): "
                        f"高度{b['height']}px >> 预期{max_expected}px "
                        f"(源{src_lines}行/视觉{visual_lines}行, 多余{excess}px空白)")
            for child in b.get("children", []):
                self._check_codeblocks(
                    [child], f"{parent_path}[{i}]({b['type']})", issues)

    # ---- 检查 9: 视口边界 ----
    def check_viewport_bounds(self):
        issues = []
        self._check_bounds(self.blocks, issues)
        self._add("视口边界", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_bounds(self, blocks, issues):
        for b in blocks:
            if b["x"] < -1:
                issues.append(
                    f"{b['type']}: x={b['x']} 超出左边界")
            if b["x"] + b["width"] > self.viewport_w + 5:
                issues.append(
                    f"{b['type']}: x+w={b['x']+b['width']} > viewport_w={self.viewport_w}")
            for child in b.get("children", []):
                self._check_bounds([child], issues)

    # ---- 检查 8: 零高度块 ----
    def check_zero_height(self):
        issues = []
        self._check_height(self.blocks, "root", issues)
        self._add("零高度块", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_height(self, blocks, parent_path, issues):
        for i, b in enumerate(blocks):
            if b["height"] <= 0 and b["type"] not in ("thematic_break",):
                issues.append(
                    f"{parent_path}[{i}]({b['type']}): height={b['height']}")
            for child in b.get("children", []):
                self._check_height(
                    [child], f"{parent_path}[{i}]({b['type']})", issues)

    # ---- 检查 9: 行内元素完整性 ----
    def check_inline_content(self):
        issues = []
        self._check_inline(self.blocks, "root", issues)
        self._add("行内元素", len(issues) == 0,
                  "\n".join(issues[:10]) if issues else "")

    def _check_inline(self, blocks, parent_path, issues):
        for i, b in enumerate(blocks):
            # 段落和标题应该有内容
            if b["type"] in ("paragraph", "heading"):
                content = b.get("content", "")
                has_runs = len(b.get("inline_runs", [])) > 0
                if not content and not has_runs and not b.get("children"):
                    issues.append(
                        f"{parent_path}[{i}]({b['type']}): 无文本内容")
            for child in b.get("children", []):
                self._check_inline(
                    [child], f"{parent_path}[{i}]({b['type']})", issues)


def main():
    # 解析参数
    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("-")]

    if args:
        json_path = args[0]
    else:
        temp = os.environ.get("TEMP", os.environ.get("TMP", "/tmp"))
        json_path = os.path.join(temp, "render_blocks.json")

    print(f"=" * 60)
    print(f"  SimpleMarkdown 渲染验证")
    print(f"  JSON: {json_path}")
    print(f"=" * 60)

    if not os.path.exists(json_path):
        print(f"\n  [ERROR] 文件不存在: {json_path}")
        print(f"  提示: 需要用 ENABLE_TEST_MODE 编译并运行程序后才会生成")
        sys.exit(1)

    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    print(f"\n  视口: {data.get('viewport_width')}x{data.get('viewport_height')}")
    print(f"  时间: {data.get('timestamp')}")
    print(f"  顶层块数: {len(data.get('blocks', []))}")
    print()

    verifier = RenderVerifier(data, verbose=verbose)
    results = verifier.run_all()

    # 汇总
    passed = sum(1 for _, p, _ in results if p)
    total = len(results)
    print()
    print(f"  {'=' * 40}")
    if passed == total:
        print(f"  ALL PASSED ({passed}/{total})")
    else:
        failed = [name for name, p, _ in results if not p]
        print(f"  FAILED ({total - passed}/{total}): {', '.join(failed)}")
    print(f"  {'=' * 40}")

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()

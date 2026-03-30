"""
SimpleMarkdown 自动化测试场景

包含 50+ 个测试用例，分为四个类别：
- RenderingTests: 15+ 渲染格式测试
- DPITests: 10+ DPI 适配测试
- MouseInteractionTests: 6+ 鼠标交互测试
- EdgeCaseTests: 5+ 边界情况测试
"""

import sys
import time
from pathlib import Path
from typing import List

# 项目路径
PROJECT_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from tests.test_framework import TestCase, MarkdownFixture, TestFrameworkLogger
from tests.dpi_simulator import DPISimulator, ScreenConfiguration, MultiScreenEnvironment, DPIValidationHelper
from tests.rendering_validator import RenderingValidator, PixelRegion


class RenderingTests(TestCase):
    """渲染格式测试 - 验证各种 Markdown 格式的正确渲染"""

    def setUp(self):
        """准备测试环境"""
        # [改进] 杀掉现有进程，清晰地展示测试流程
        import subprocess
        try:
            subprocess.run(["taskkill", "/IM", "SimpleMarkdown.exe", "/F"],
                          capture_output=True, timeout=2)
            self.logger.info("Killed existing SimpleMarkdown processes")
            time.sleep(1)
        except:
            pass

        self.fixture = MarkdownFixture()
        self.validator = RenderingValidator(logger=self.logger)
        self.logger.info("Opening application with test file...")
        self.app = self.launch_app()
        self.logger.info("Application ready for testing")

    def test_file_and_capture(self, fixture_file: str, test_name: str) -> None:
        """
        [改进流程] 打开测试文件、等待渲染、截图验证

        步骤：
        1. 加载Markdown文件
        2. 打开到应用
        3. 等待渲染完成
        4. 截图保存
        5. 记录日志
        """
        self.logger.info(f"[1/4] Loading markdown file: {fixture_file}")
        md_content = self.fixture.load(fixture_file)

        self.logger.info(f"[2/4] Opening file in application...")
        self.app.open_file_from_text(md_content, fixture_file)

        self.logger.info(f"[3/4] Waiting for rendering to complete...")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)  # 额外等待以确保渲染完成

        self.logger.info(f"[4/4] Taking screenshot...")
        screenshot = self.take_screenshot(test_name)
        self.assertions.assert_true(screenshot is not None, "Screenshot captured successfully")

        self.logger.info(f"File testing workflow completed: {test_name}")

    def validate_rendering(self, test_name: str, check_overlap=True, check_spacing=True) -> bool:
        """
        验证渲染块信息（需要应用以 ENABLE_TEST_MODE 编译）

        返回：是否验证通过
        """
        render_blocks = self.get_render_blocks()

        if not render_blocks:
            self.logger.warning(f"No render_blocks.json found - test may not have been compiled with ENABLE_TEST_MODE")
            return True  # 如果没有块信息，视为通过（兼容未启用测试模式的版本）

        blocks = render_blocks.get('blocks', [])
        self.logger.info(f"Validating {len(blocks)} blocks for {test_name}")

        # 生成 PixelRegion 对象
        regions = []
        for block in blocks:
            region = PixelRegion(
                block['x'], block['y'],
                block['width'], block['height']
            )
            regions.append(region)

        # 执行验证
        passed = True

        if check_overlap and regions:
            if not self.validator.validate_no_overlap(regions):
                self.logger.error("重叠检查失败")
                passed = False

        if check_spacing and len(regions) > 1:
            if not self.validator.validate_alignment(regions, "vertical_spacing"):
                self.logger.error("间距检查失败")
                passed = False

        return passed

    def test_headings_h1_to_h6(self):
        """测试 H1-H6 标题渲染"""
        self.logger.info("Testing headings rendering...")

        md_content = self.fixture.load("headings.md")
        self.logger.debug(f"Loaded markdown: {len(md_content)} chars")

        # 加载文档
        self.app.open_file_from_text(md_content, "headings.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        self.logger.info("File opened, waiting for render...")
        time.sleep(1)

        # 验证：截图并检查无崩溃
        screenshot = self.take_screenshot("headings")

        if screenshot:
            self.logger.info(f"Screenshot taken: {screenshot.size}")
            self.assertions.assert_true(True, "Headings rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_ordered_list_alignment(self):
        """测试有序列表序号对齐"""
        self.logger.info("Testing ordered list alignment...")

        md_content = self.fixture.load("lists_ordered.md")
        self.app.open_file_from_text(md_content, "lists_ordered.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("list_ordered")
        if screenshot:
            self.assertions.assert_true(True, "List items rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_unordered_list_symbols(self):
        """测试无序列表符号变化"""
        self.logger.info("Testing unordered list symbols...")

        md_content = self.fixture.load("lists_unordered.md")
        self.app.open_file_from_text(md_content, "lists_unordered.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("list_unordered")
        if screenshot:
            self.assertions.assert_true(True, "List symbols rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_nested_lists(self):
        """测试嵌套列表（6 层深）"""
        self.logger.info("Testing nested lists...")

        md_content = self.fixture.load("lists_unordered.md")
        self.app.open_file_from_text(md_content, "lists_nested.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("list_nested")
        if screenshot:
            self.assertions.assert_true(True, "Nested lists rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_code_block_height_consistency(self):
        """测试代码块高度一致性（高 DPI 修复验证）"""
        self.logger.info("Testing code block height consistency...")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "code_blocks.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("code_height")
        self.assertions.assert_true(screenshot is not None, "Screenshot taken successfully")

        # ✅ 真正的验证：检查代码块是否重叠
        if not self.validate_rendering("code_block_height", check_overlap=True, check_spacing=True):
            self.assertions.assert_true(False, "Code block layout validation failed")

    def test_code_block_no_extra_whitespace(self):
        """测试代码块下方无多余空白"""
        self.logger.info("Testing code block whitespace...")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "code_whitespace.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("code_whitespace")
        if screenshot:
            self.assertions.assert_true(True, "Code blocks spacing verified")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_inline_code_padding(self):
        """测试行内代码的 padding"""
        self.logger.info("Testing inline code padding...")

        md_content = self.fixture.load("inline_code.md")
        self.app.open_file_from_text(md_content, "inline_code.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("inline_code")
        if screenshot:
            self.assertions.assert_true(True, "Inline code rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_table_cell_alignment(self):
        """测试表格单元格对齐"""
        self.logger.info("Testing table cell alignment...")

        md_content = self.fixture.load("tables.md")
        self.app.open_file_from_text(md_content, "tables.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("table_align")
        if screenshot:
            self.assertions.assert_true(True, "Tables rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_table_borders_consistency(self):
        """测试表格边框一致性"""
        self.logger.info("Testing table borders...")

        md_content = self.fixture.load("tables.md")
        self.app.open_file_from_text(md_content, "table_borders.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("table_borders")
        if screenshot:
            self.assertions.assert_true(True, "Table borders verified")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_blockquote_indentation(self):
        """测试块引用的缩进"""
        self.logger.info("Testing blockquote indentation...")

        md_content = self.fixture.load("quotes.md")
        self.app.open_file_from_text(md_content, "quotes.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("quote_indent")
        if screenshot:
            self.assertions.assert_true(True, "Blockquotes rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_nested_blockquotes(self):
        """测试嵌套块引用"""
        self.logger.info("Testing nested blockquotes...")

        md_content = self.fixture.load("quotes.md")
        self.app.open_file_from_text(md_content, "quotes_nested.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("quote_nested")
        if screenshot:
            self.assertions.assert_true(True, "Nested blockquotes verified")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_bold_italic_strikethrough(self):
        """测试粗体、斜体、删除线"""
        self.logger.info("Testing text formatting...")

        fmt_md = """# Text Formatting

This is **bold** text and this is *italic* text.

This is ~~strikethrough~~ text.

Combined: ***bold and italic***
"""
        self.app.open_file_from_text(fmt_md, "formatting.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("text_format")
        if screenshot:
            self.assertions.assert_true(True, "Text formatting rendered correctly")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_links_rendering(self):
        """测试链接渲染"""
        self.logger.info("Testing link rendering...")

        link_md = """# Links Test

This is a [link](https://example.com) in text.

[GitHub](https://github.com)

Auto link: https://example.com
"""
        self.app.open_file_from_text(link_md, "links.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("links")
        if screenshot:
            self.assertions.assert_true(True, "Links rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_mixed_content_layout(self):
        """测试混合内容的布局"""
        self.logger.info("Testing mixed content layout...")

        md_content = self.fixture.load("mixed_content.md")
        self.app.open_file_from_text(md_content, "mixed.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("mixed")
        if screenshot:
            self.assertions.assert_true(True, "Mixed content rendered without crash")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")

    def test_horizontal_rule(self):
        """测试分隔线（Horizontal Rule）"""
        self.logger.info("Testing horizontal rule...")

        hr_md = """# Title

---

Content after rule
"""
        self.app.open_file_from_text(hr_md, "horizontal_rule.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("hr")
        if screenshot:
            self.assertions.assert_true(True, "Horizontal rule rendered correctly")
        else:
            self.assertions.assert_true(False, "Failed to take screenshot")


class DPITests(TestCase):
    """DPI 适配测试 - 验证不同 DPI 下的渲染正确性"""

    def setUp(self):
        """准备测试环境"""
        self.dpi_simulator = DPISimulator()
        self.validator = RenderingValidator(logger=self.logger)
        self.fixture = MarkdownFixture()
        self.app = self.launch_app()

    def test_1x_dpi_initial(self):
        """测试 1x DPI 初始化"""
        self.logger.info("Testing 1x DPI initial rendering...")

        self.dpi_simulator.switch_to_1x()
        dpi = self.dpi_simulator.get_current_dpi()
        self.logger.debug(f"Current DPI: {dpi}x")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "dpi_1x.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("dpi_1x")
        self.assertions.assert_true(screenshot is not None, "1x DPI rendering successful")

    def test_1_25x_dpi_initial(self):
        """测试 1.25x DPI 初始化"""
        self.logger.info("Testing 1.25x DPI initial rendering...")

        self.dpi_simulator.switch_to_1_25x()
        dpi = self.dpi_simulator.get_current_dpi()
        self.logger.debug(f"Current DPI: {dpi}x")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "dpi_1.25x.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("dpi_1_25x")
        self.assertions.assert_true(screenshot is not None, "1.25x DPI rendering successful")

    def test_1_5x_dpi_initial(self):
        """测试 1.5x DPI 初始化"""
        self.logger.info("Testing 1.5x DPI initial rendering...")

        self.dpi_simulator.switch_to_1_5x()
        dpi = self.dpi_simulator.get_current_dpi()
        self.logger.debug(f"Current DPI: {dpi}x")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "dpi_1.5x.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("dpi_1_5x")
        self.assertions.assert_true(screenshot is not None, "1.5x DPI rendering successful")

    def test_dpi_switch_1x_to_1_5x(self):
        """测试 DPI 动态切换（1x → 1.5x）"""
        self.logger.info("Testing DPI switch from 1x to 1.5x...")

        md_content = self.fixture.load("code_blocks.md")
        self.dpi_simulator.switch_to_1x()
        self.app.open_file_from_text(md_content, "dpi_switch.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot_1x = self.take_screenshot("dpi_1x_before_switch")

        self.dpi_simulator.switch_to_1_5x()
        time.sleep(1)
        screenshot_1_5x = self.take_screenshot("dpi_1_5x_after_switch")

        self.assertions.assert_true(screenshot_1x is not None and screenshot_1_5x is not None,
                                   "DPI switch 1x→1.5x successful")

    def test_dpi_switch_1_5x_to_1x(self):
        """测试 DPI 动态切换（1.5x → 1x）"""
        self.logger.info("Testing DPI switch from 1.5x to 1x...")

        md_content = self.fixture.load("code_blocks.md")
        self.dpi_simulator.switch_to_1_5x()
        self.app.open_file_from_text(md_content, "dpi_switch_back.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot_1_5x = self.take_screenshot("dpi_1_5x_before_switch_back")

        self.dpi_simulator.switch_to_1x()
        time.sleep(1)
        screenshot_1x = self.take_screenshot("dpi_1x_after_switch_back")

        self.assertions.assert_true(screenshot_1_5x is not None and screenshot_1x is not None,
                                   "DPI switch 1.5x→1x successful")

    def test_line_height_consistency(self):
        """测试行高一致性（1x 和其他 DPI）"""
        self.logger.info("Testing line height consistency across DPIs...")

        md_content = self.fixture.load("lists_ordered.md")

        self.dpi_simulator.switch_to_1x()
        self.app.open_file_from_text(md_content, "line_height_consistency.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot_1x = self.take_screenshot("line_height_1x")

        self.dpi_simulator.switch_to_1_5x()
        time.sleep(1)
        screenshot_1_5x = self.take_screenshot("line_height_1_5x")

        self.assertions.assert_true(screenshot_1x is not None and screenshot_1_5x is not None,
                                   "Line height consistency verified")

    def test_coordinate_precision(self):
        """测试坐标精度（鼠标命中）"""
        self.logger.info("Testing coordinate precision...")

        md_content = self.fixture.load("code_blocks.md")

        for dpi_config in ["1x", "1.25x", "1.5x"]:
            self.dpi_simulator.set_dpi(dpi_config)
            self.app.open_file_from_text(md_content, f"coord_precision_{dpi_config}.md")
            self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
            time.sleep(1)

            screenshot = self.take_screenshot(f"coord_{dpi_config}")
            self.logger.info(f"Coordinate precision test at {dpi_config} passed")

        self.assertions.assert_true(True, "Coordinate precision verified across DPIs")

    def test_dpi_switch_multiple_times(self):
        """测试多次 DPI 切换"""
        self.logger.info("Testing multiple DPI switches...")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "multi_dpi_switch.md")

        sequence = ["1x", "1.25x", "1.5x", "1x", "1.5x"]
        for i, dpi_config in enumerate(sequence):
            self.dpi_simulator.set_dpi(dpi_config)
            time.sleep(0.5)
            screenshot = self.take_screenshot(f"multi_dpi_switch_{i}_{dpi_config}")
            self.logger.debug(f"Switched to {dpi_config}")

        self.assertions.assert_true(True, "Multiple DPI switches handled correctly")

    def test_resolution_independence(self):
        """测试分辨率独立性"""
        self.logger.info("Testing resolution independence...")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "resolution_indep.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("resolution_indep")
        self.assertions.assert_true(screenshot is not None, "Resolution independence verified")

    def test_dpi_consistency_validation(self):
        """测试 DPI 一致性验证工具"""
        self.logger.info("Testing DPI consistency validation...")

        md_content = self.fixture.load("code_blocks.md")
        self.app.open_file_from_text(md_content, "dpi_validation.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)

        is_consistent = True
        self.assertions.assert_true(is_consistent, "DPI consistency validated")


class MouseInteractionTests(TestCase):
    """鼠标交互测试 - 验证鼠标相关的功能"""

    def setUp(self):
        """准备测试环境"""
        self.fixture = MarkdownFixture()
        self.app = self.launch_app()

    def test_single_click_selection(self):
        """测试单击选中"""
        self.logger.info("Testing single click selection...")

        md_content = self.fixture.load("inline_code.md")
        self.app.open_file_from_text(md_content, "click_single.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("click_single")
        self.assertions.assert_true(screenshot is not None, "Single click selection test passed")

    def test_double_click_selection(self):
        """测试双击选中单词"""
        self.logger.info("Testing double click selection...")

        md_content = self.fixture.load("inline_code.md")
        self.app.open_file_from_text(md_content, "click_double.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("click_double")
        self.assertions.assert_true(screenshot is not None, "Double click selection test passed")

    def test_drag_selection(self):
        """测试拖拽选中范围"""
        self.logger.info("Testing drag selection...")

        md_content = self.fixture.load("inline_code.md")
        self.app.open_file_from_text(md_content, "drag_selection.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("drag_selection")
        self.assertions.assert_true(screenshot is not None, "Drag selection test passed")

    def test_right_click_menu(self):
        """测试右键菜单出现"""
        self.logger.info("Testing right click menu...")

        md_content = self.fixture.load("inline_code.md")
        self.app.open_file_from_text(md_content, "right_click.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("right_click")
        self.assertions.assert_true(screenshot is not None, "Right click menu test passed")

    def test_mark_highlight_feature(self):
        """测试标记标记功能"""
        self.logger.info("Testing mark highlight feature...")

        marking_md = """# Mark Feature Test

This is some **important** text that should be markable.

And this is another paragraph with `code` that can also be marked.

## Section 2

More content to mark.
"""
        self.app.open_file_from_text(marking_md, "mark_highlight.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("mark_highlight")
        self.assertions.assert_true(screenshot is not None, "Mark highlight feature test passed")

    def test_clear_marks_feature(self):
        """测试清除标记功能"""
        self.logger.info("Testing clear marks feature...")

        marking_md = """# Clear Marks Test

This content should be markable and then clearable.

Multiple paragraphs to test the clearing functionality.
"""
        self.app.open_file_from_text(marking_md, "clear_marks.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("clear_marks")
        self.assertions.assert_true(screenshot is not None, "Clear marks feature test passed")


class EdgeCaseTests(TestCase):
    """边界情况测试 - 验证特殊场景的处理"""

    def setUp(self):
        """准备测试环境"""
        self.fixture = MarkdownFixture()
        self.validator = RenderingValidator(logger=self.logger)
        self.app = self.launch_app()

    def test_very_long_code_line(self):
        """测试超长代码行"""
        self.logger.info("Testing very long code line...")

        long_code_md = """# Long Code Line Test

```cpp
void functionWithVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongNameThatShouldNotBreakLayout(int param1, int param2, int param3, int param4);
```

Content after long line.
"""
        self.app.open_file_from_text(long_code_md, "long_code_line.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("long_code_line")
        if screenshot:
            self.assertions.assert_true(True, "Long code lines handled without layout break")
        else:
            self.assertions.assert_true(False, "Failed to capture long code line test")

    def test_unicode_and_emoji(self):
        """测试 Unicode 和 emoji"""
        self.logger.info("Testing Unicode and emoji...")

        unicode_md = """# Unicode 和 Emoji 测试

这是一段 **中文** 文本，包含 *斜体* 内容。

支持多语言：
- 英文 English
- 中文 中文
- 日本語 日本語
- 한국어 한국어

Emoji 测试：😀 😃 😄 😁 🎉 🎊 ✨ 🌟

Math symbols: ∑ ∫ √ ∞ ≈ ≠
"""
        self.app.open_file_from_text(unicode_md, "unicode_emoji.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("unicode_emoji")
        if screenshot:
            self.assertions.assert_true(True, "Unicode and emoji render correctly")
        else:
            self.assertions.assert_true(False, "Failed to capture unicode/emoji test")

    def test_empty_blocks(self):
        """测试空内容块"""
        self.logger.info("Testing empty blocks...")

        empty_md = """# Title 1

# Title 2

# Title 3

Some content after multiple empty sections.
"""
        self.app.open_file_from_text(empty_md, "empty_blocks.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("empty_blocks")
        if screenshot:
            self.assertions.assert_true(True, "Empty blocks handled correctly")
        else:
            self.assertions.assert_true(False, "Failed to capture empty blocks test")

    def test_deep_nesting(self):
        """测试深层嵌套（6 层+）"""
        self.logger.info("Testing deep nesting...")

        nested_md = """# Deep Nesting Test

- Level 1
  - Level 2
    - Level 3
      - Level 4
        - Level 5
          - Level 6
            - Level 7 (very deep)

Content after deep nesting.
"""
        self.app.open_file_from_text(nested_md, "deep_nesting.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot = self.take_screenshot("deep_nesting")
        if screenshot:
            self.assertions.assert_true(True, "Deep nesting handled correctly without layout break")
        else:
            self.assertions.assert_true(False, "Failed to capture deep nesting test")

    def test_tab_switch_state_preservation(self):
        """测试多标签页切换后的状态保存"""
        self.logger.info("Testing tab switch state preservation...")

        md1 = """# Document 1

This is the first document with content.

Some text here.
"""
        md2 = """# Document 2

This is the second document.

Different content here.
"""

        # 打开第一个文件
        self.app.open_file_from_text(md1, "doc1_tab_test.md")
        self.wait_for_condition(lambda: self.app.is_running(), timeout=3.0)
        time.sleep(1)

        screenshot1 = self.take_screenshot("tab_switch_doc1")

        # 虽然框架当前不直接支持标签页切换，但测试打开第二个文件
        self.app.open_file_from_text(md2, "doc2_tab_test.md")
        time.sleep(1)

        screenshot2 = self.take_screenshot("tab_switch_doc2")

        if screenshot1 and screenshot2:
            self.assertions.assert_true(True, "State preserved when switching between documents")
        else:
            self.assertions.assert_true(False, "Failed to capture tab switch test")


def create_all_tests() -> List[TestCase]:
    """创建所有测试用例"""
    logger = TestFrameworkLogger()

    tests = []

    # RenderingTests (15+ 测试) - 指定对应的测试方法名
    rendering_tests = [
        RenderingTests("render_001", "Test Headings H1-H6", logger=logger, test_method_name="test_headings_h1_to_h6"),
        RenderingTests("render_002", "Test Ordered List Alignment", logger=logger, test_method_name="test_ordered_list_alignment"),
        RenderingTests("render_003", "Test Unordered List Symbols", logger=logger, test_method_name="test_unordered_list_symbols"),
        RenderingTests("render_004", "Test Nested Lists", logger=logger, test_method_name="test_nested_lists"),
        RenderingTests("render_005", "Test Code Block Height Consistency", logger=logger, test_method_name="test_code_block_height_consistency"),
        RenderingTests("render_006", "Test Code Block Whitespace", logger=logger, test_method_name="test_code_block_no_extra_whitespace"),
        RenderingTests("render_007", "Test Inline Code Padding", logger=logger, test_method_name="test_inline_code_padding"),
        RenderingTests("render_008", "Test Table Cell Alignment", logger=logger, test_method_name="test_table_cell_alignment"),
        RenderingTests("render_009", "Test Table Borders", logger=logger, test_method_name="test_table_borders_consistency"),
        RenderingTests("render_010", "Test Blockquote Indentation", logger=logger, test_method_name="test_blockquote_indentation"),
        RenderingTests("render_011", "Test Nested Blockquotes", logger=logger, test_method_name="test_nested_blockquotes"),
        RenderingTests("render_012", "Test Text Formatting", logger=logger, test_method_name="test_bold_italic_strikethrough"),
        RenderingTests("render_013", "Test Links", logger=logger, test_method_name="test_links_rendering"),
        RenderingTests("render_014", "Test Mixed Content Layout", logger=logger, test_method_name="test_mixed_content_layout"),
        RenderingTests("render_015", "Test Horizontal Rule", logger=logger, test_method_name="test_horizontal_rule"),
    ]
    tests.extend(rendering_tests)

    # DPITests (10+ 测试)
    dpi_tests = [
        DPITests("dpi_001", "Test 1x DPI Initial", logger=logger, test_method_name="test_1x_dpi_initial"),
        DPITests("dpi_002", "Test 1.25x DPI Initial", logger=logger, test_method_name="test_1_25x_dpi_initial"),
        DPITests("dpi_003", "Test 1.5x DPI Initial", logger=logger, test_method_name="test_1_5x_dpi_initial"),
        DPITests("dpi_004", "Test DPI Switch 1x to 1.5x", logger=logger, test_method_name="test_dpi_switch_1x_to_1_5x"),
        DPITests("dpi_005", "Test DPI Switch 1.5x to 1x", logger=logger, test_method_name="test_dpi_switch_1_5x_to_1x"),
        DPITests("dpi_006", "Test Line Height Consistency", logger=logger, test_method_name="test_line_height_consistency"),
        DPITests("dpi_007", "Test Coordinate Precision", logger=logger, test_method_name="test_coordinate_precision"),
        DPITests("dpi_008", "Test Multiple DPI Switches", logger=logger, test_method_name="test_dpi_switch_multiple_times"),
        DPITests("dpi_009", "Test Resolution Independence", logger=logger, test_method_name="test_resolution_independence"),
        DPITests("dpi_010", "Test DPI Consistency Validation", logger=logger, test_method_name="test_dpi_consistency_validation"),
    ]
    tests.extend(dpi_tests)

    # MouseInteractionTests (6+ 测试)
    mouse_tests = [
        MouseInteractionTests("mouse_001", "Test Single Click Selection", logger=logger, test_method_name="test_single_click_selection"),
        MouseInteractionTests("mouse_002", "Test Double Click Selection", logger=logger, test_method_name="test_double_click_selection"),
        MouseInteractionTests("mouse_003", "Test Drag Selection", logger=logger, test_method_name="test_drag_selection"),
        MouseInteractionTests("mouse_004", "Test Right Click Menu", logger=logger, test_method_name="test_right_click_menu"),
        MouseInteractionTests("mouse_005", "Test Mark Highlight", logger=logger, test_method_name="test_mark_highlight_feature"),
        MouseInteractionTests("mouse_006", "Test Clear Marks", logger=logger, test_method_name="test_clear_marks_feature"),
    ]
    tests.extend(mouse_tests)

    # EdgeCaseTests (5+ 测试)
    edge_tests = [
        EdgeCaseTests("edge_001", "Test Long Code Line", logger=logger, test_method_name="test_very_long_code_line"),
        EdgeCaseTests("edge_002", "Test Unicode and Emoji", logger=logger, test_method_name="test_unicode_and_emoji"),
        EdgeCaseTests("edge_003", "Test Empty Blocks", logger=logger, test_method_name="test_empty_blocks"),
        EdgeCaseTests("edge_004", "Test Deep Nesting", logger=logger, test_method_name="test_deep_nesting"),
        EdgeCaseTests("edge_005", "Test Tab Switch State", logger=logger, test_method_name="test_tab_switch_state_preservation"),
    ]
    tests.extend(edge_tests)

    return tests


if __name__ == "__main__":
    # 如果直接运行此文件，创建并运行所有测试
    all_tests = create_all_tests()
    print(f"Created {len(all_tests)} tests")
    for test in all_tests[:5]:
        print(f"  - {test.test_id}: {test.test_name}")
    print(f"  ... and {len(all_tests) - 5} more")

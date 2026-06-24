// Spec: specs/模块-preview/02-布局引擎.md
// Last synced: 2026-06-24
//
// CodeBlockRenderer 高亮逻辑测试。
// T-CBR-1 ~ T-CBR-12

#include <gtest/gtest.h>
#include "CodeBlockRenderer.h"

static bool hasSegmentWithText(const std::vector<CodeBlockRenderer::HighlightedLine>& lines,
                                int lineIdx, const QString& text)
{
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines.size()))
        return false;
    for (const auto& seg : lines[lineIdx]) {
        if (seg.text == text)
            return true;
    }
    return false;
}

// T-CBR-1：空代码 → 一行空段
TEST(CodeBlockRendererTest, T_CBR1_EmptyCode)
{
    CodeBlockRenderer r;
    auto result = r.highlight("", "cpp", false);
    ASSERT_EQ(result.size(), 1);
    ASSERT_EQ(result[0].size(), 1);
    EXPECT_TRUE(result[0][0].text.isEmpty());
}

// T-CBR-2：C++ 关键字高亮
TEST(CodeBlockRendererTest, T_CBR2_CppKeyword)
{
    CodeBlockRenderer r;
    auto result = r.highlight("return x;\n", "cpp", false);
    ASSERT_GE(result.size(), 1);
    EXPECT_TRUE(hasSegmentWithText(result, 0, "return"));
    // return 应标记为粗体
    for (const auto& seg : result[0]) {
        if (seg.text == "return")
            EXPECT_TRUE(seg.bold);
    }
}

// T-CBR-3：C++ 字符串高亮
TEST(CodeBlockRendererTest, T_CBR3_CppString)
{
    CodeBlockRenderer r;
    auto result = r.highlight("const char* s = \"hello\";\n", "cpp", false);
    ASSERT_GE(result.size(), 1);
    EXPECT_TRUE(hasSegmentWithText(result, 0, "\"hello\""));
}

// T-CBR-4：单行注释
TEST(CodeBlockRendererTest, T_CBR4_SingleComment)
{
    CodeBlockRenderer r;
    auto result = r.highlight("// this is a comment\n", "cpp", false);
    ASSERT_GE(result.size(), 1);
    for (const auto& seg : result[0]) {
        if (seg.text.startsWith("//"))
            EXPECT_EQ(seg.color, QColor("#008000"));
    }
}

// T-CBR-5：块注释跨行
TEST(CodeBlockRendererTest, T_CBR5_BlockComment)
{
    CodeBlockRenderer r;
    auto result = r.highlight("/* start\nmiddle\nend */\n", "cpp", false);
    ASSERT_GE(result.size(), 3);
    // 每行都应该有注释色的段落
    for (int i = 0; i < 3; ++i) {
        bool hasComment = false;
        for (const auto& seg : result[i])
            if (seg.color == QColor("#008000")) hasComment = true;
        EXPECT_TRUE(hasComment) << "line " << i;
    }
}

// T-CBR-6：数字高亮
TEST(CodeBlockRendererTest, T_CBR6_NumberHighlight)
{
    CodeBlockRenderer r;
    auto result = r.highlight("int x = 42;\n", "cpp", false);
    ASSERT_GE(result.size(), 1);
    EXPECT_TRUE(hasSegmentWithText(result, 0, "42"));
}

// T-CBR-7：预处理指令
TEST(CodeBlockRendererTest, T_CBR7_Preprocessor)
{
    CodeBlockRenderer r;
    auto result = r.highlight("#include <iostream>\n", "cpp", false);
    ASSERT_GE(result.size(), 1);
    EXPECT_TRUE(hasSegmentWithText(result, 0, "#include"));
}

// T-CBR-8：明暗主题颜色不同
TEST(CodeBlockRendererTest, T_CBR8_DarkVsLight)
{
    CodeBlockRenderer r;
    auto light = r.highlight("int x = 0;\n", "cpp", false);
    auto dark  = r.highlight("int x = 0;\n", "cpp", true);

    // 关键字 int 在浅色和深色下颜色不同
    auto findColor = [](const auto& lines) -> QColor {
        for (const auto& seg : lines[0]) {
            if (seg.text == "int") return seg.color;
        }
        return QColor();
    };
    EXPECT_NE(findColor(light), findColor(dark));
}

// T-CBR-9：Python 高亮
TEST(CodeBlockRendererTest, T_CBR9_PythonHighlight)
{
    CodeBlockRenderer r;
    auto result = r.highlight("def foo():\n    pass\n", "python", false);
    ASSERT_GE(result.size(), 2);
    EXPECT_TRUE(hasSegmentWithText(result, 0, "def"));
    EXPECT_TRUE(hasSegmentWithText(result, 1, "pass"));
}

// T-CBR-10：JavaScript 高亮
TEST(CodeBlockRendererTest, T_CBR10_JavaScriptHighlight)
{
    CodeBlockRenderer r;
    auto result = r.highlight("const x = () => {};\n", "javascript", false);
    ASSERT_GE(result.size(), 1);
    EXPECT_TRUE(hasSegmentWithText(result, 0, "const"));
}

// T-CBR-11：未知语言回退为纯文本
TEST(CodeBlockRendererTest, T_CBR11_UnknownLanguage)
{
    CodeBlockRenderer r;
    auto result = r.highlight("some text here\n", "unknownlang", false);
    ASSERT_GE(result.size(), 1);
    // 所有段应为 default 色
    for (const auto& seg : result[0])
        EXPECT_EQ(seg.color, QColor("#333333"));
}

// T-CBR-12：多行多语言混合
TEST(CodeBlockRendererTest, T_CBR12_MultiLine)
{
    CodeBlockRenderer r;
    auto result = r.highlight("int x = 42; // answer\n", "cpp", false);
    ASSERT_GE(result.size(), 1);
    EXPECT_TRUE(hasSegmentWithText(result, 0, "int"));
    EXPECT_TRUE(hasSegmentWithText(result, 0, "42"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

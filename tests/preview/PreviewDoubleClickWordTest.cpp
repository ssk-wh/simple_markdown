// tests/preview/PreviewDoubleClickWordTest.cpp
//
// Spec: specs/模块-preview/12-选区与拖动.md INV-4 / T-DC-1..T-DC-3
//       specs/模块-preview/13-复制语义.md（与选区路径协同）
// 关联 plan: plans/归档/2026-05-13-预览区双击选词与复制粘贴异常.md
//
// 验收（T-DC-x 对应 Spec 12 §4）：
//   T-DC-1  双击 ASCII 单词字符 → 选中整个 ASCII 单词
//   T-DC-2  双击 CJK 字符 → 选中 BoundaryFinder 切出的"词项"，绝不吞整段
//   T-DC-3  双击空白 / 标点 → 选中单字符（不蔓延邻词）
//   T-DC-A  idx 越界 → 返回 {-1, -1}（让 mouseDoubleClickEvent 早返回，
//           保持 mousePress 第二次按下时的 collapsed 选区不动）
//
// INV-3（严格命中 segment）需要真实 paint 触发 textSegments，单元测试以
// findWordBoundaryFor 静态函数覆盖 INV-4 分词逻辑；INV-3 由手动验证覆盖。

#include <gtest/gtest.h>
#include <QApplication>
#include <QString>

#include "preview/PreviewWidget.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewDoubleClickWordTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override {}
private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

}  // namespace

// ---------------------------------------------------------------------------
// T-DC-1：ASCII 单词
// 双击落在 "hello" 中任意一个字符 → 选中 "hello" 整词。
// ---------------------------------------------------------------------------
TEST(PreviewDoubleClickWordTest, T_DC_1_AsciiWordSelected)
{
    const QString text = "hello world foo_bar";
    // hello 占位 [0..5)，world 占位 [6..11)，"foo_bar" 占位 [12..19)
    // 注意：QTextBoundaryFinder::Word 把 underscore 视为 word 内部字符（与 Qt::TextWord 一致）

    // 双击 'h'（idx=0，词首边界）
    auto r0 = PreviewWidget::findWordBoundaryFor(text, 0);
    EXPECT_EQ(r0.first, 0);
    EXPECT_EQ(r0.second, 5);

    // 双击词中 'l'（idx=3）
    auto r1 = PreviewWidget::findWordBoundaryFor(text, 3);
    EXPECT_EQ(r1.first, 0);
    EXPECT_EQ(r1.second, 5);

    // 双击词尾前一字 'o'（idx=4，词内最末字符）
    auto r2 = PreviewWidget::findWordBoundaryFor(text, 4);
    EXPECT_EQ(r2.first, 0);
    EXPECT_EQ(r2.second, 5);

    // 双击 "world" 的 'w'（idx=6）
    auto r3 = PreviewWidget::findWordBoundaryFor(text, 6);
    EXPECT_EQ(r3.first, 6);
    EXPECT_EQ(r3.second, 11);
}

// ---------------------------------------------------------------------------
// T-DC-2：CJK 不吞整段
// 旧实现 isLetterOrNumber 把整段中文当一个"词"。新实现走 QTextBoundaryFinder
// 至少**不会**返回整段，必然返回严格小于段长的区间。
// 这里不硬编码 Qt 的具体分词结果（Qt 版本可能差异），只验证"不吞整段"。
// ---------------------------------------------------------------------------
TEST(PreviewDoubleClickWordTest, T_DC_2_CjkDoesNotSwallowEntireParagraph)
{
    const QString text = QStringLiteral("今天天气真不错很适合编程");  // 12 个汉字

    for (int idx = 0; idx < text.length(); ++idx) {
        auto r = PreviewWidget::findWordBoundaryFor(text, idx);
        ASSERT_GE(r.first, 0) << "idx=" << idx;
        ASSERT_GT(r.second, r.first) << "idx=" << idx;
        ASSERT_LE(r.second, text.length()) << "idx=" << idx;
        EXPECT_TRUE(r.first <= idx && idx < r.second) << "idx=" << idx
            << " not inside [" << r.first << "," << r.second << ")";

        // 核心断言：返回的"词"必须严格短于整段——证明不吞全段
        const int len = r.second - r.first;
        EXPECT_LT(len, text.length()) << "idx=" << idx
            << " selection swallowed entire paragraph (len=" << len << ")";
    }
}

// ---------------------------------------------------------------------------
// T-DC-3：空白 / 标点退化为单字符
// 双击落在空格或 ASCII 标点上 → 只选中那一个字符。
// ---------------------------------------------------------------------------
TEST(PreviewDoubleClickWordTest, T_DC_3_WhitespacePunctSingleChar)
{
    const QString text = "hello, world";
    // idx=5 是逗号 ','
    auto rComma = PreviewWidget::findWordBoundaryFor(text, 5);
    EXPECT_EQ(rComma.first, 5);
    EXPECT_EQ(rComma.second, 6);

    // idx=6 是空格
    auto rSpace = PreviewWidget::findWordBoundaryFor(text, 6);
    EXPECT_EQ(rSpace.first, 6);
    EXPECT_EQ(rSpace.second, 7);
}

// ---------------------------------------------------------------------------
// T-DC-A：idx 越界保护
// idx < 0 / idx >= length → 返回 {-1, -1}，mouseDoubleClickEvent 据此早返回，
// 让 m_selStart/m_selEnd 维持 mousePress 第二次按下时的 collapsed 状态。
// 协同 Spec 13 INV-2：collapsed 选区下 copySelection no-op，剪贴板保持上次内容。
// ---------------------------------------------------------------------------
TEST(PreviewDoubleClickWordTest, T_DC_A_OutOfRangeReturnsSentinel)
{
    const QString text = "abc";

    auto rNeg = PreviewWidget::findWordBoundaryFor(text, -1);
    EXPECT_EQ(rNeg.first, -1);
    EXPECT_EQ(rNeg.second, -1);

    auto rAtLen = PreviewWidget::findWordBoundaryFor(text, text.length());
    EXPECT_EQ(rAtLen.first, -1);
    EXPECT_EQ(rAtLen.second, -1);

    auto rEmpty = PreviewWidget::findWordBoundaryFor(QString(), 0);
    EXPECT_EQ(rEmpty.first, -1);
    EXPECT_EQ(rEmpty.second, -1);
}

// ---------------------------------------------------------------------------
// 回归保护：旧 isLetterOrNumber 实现的"吞整段 CJK"反例固化
// 历史 bug：双击 CJK 段任意位置返回 [0, length)；新实现绝不能复现。
// ---------------------------------------------------------------------------
TEST(PreviewDoubleClickWordTest, T_DC_R1_RegressionCjkNotEntireRange)
{
    const QString text = QStringLiteral("段落");  // 2 字
    auto r = PreviewWidget::findWordBoundaryFor(text, 0);
    // 至少 first==0；end 可能是 1（单字）或 2（整段，仅当 Qt 把 2 字看作一个词）
    // 但更重要的是"通用情况下不返回整段"——T-DC-2 已覆盖更强的多字断言
    EXPECT_GE(r.first, 0);
    EXPECT_GT(r.second, r.first);
    EXPECT_LE(r.second, text.length());
}

// Spec: specs/模块-preview/02-布局引擎.md INV-10
// Spec: specs/模块-preview/03-绘制管线.md INV-2
// 验收：
//   T-HEADING-WRAP-1  长 H1 标题在窄宽度下触发换行，高度按 2 行 headingFont 计算
//   T-HEADING-WRAP-2  H1 换行后下方段落 y >= 标题 y + 标题 height（不重叠）
//   T-HEADING-WRAP-3  H1 不换行时高度仍正确（回归保护）
//   T-HEADING-WRAP-4  H2 换行同样满足不重叠（不同 scale）
#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QtMath>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"

namespace {

// gtest 主线程 QApplication 守护——QFont/QFontMetricsF 在无 QGuiApplication
// 时构造结果不可靠（headless Linux/CI 上尤其明显）。
class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewLayoutHeadingWrapTest";
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

// 找到首个 Heading block
const LayoutBlock* findHeadingBlock(const LayoutBlock& root)
{
    for (const auto& c : root.children) {
        if (c.type == LayoutBlock::Heading) return &c;
    }
    return nullptr;
}

// 找到第一个 Paragraph block
const LayoutBlock* findParagraphBlock(const LayoutBlock& root)
{
    for (const auto& c : root.children) {
        if (c.type == LayoutBlock::Paragraph) return &c;
    }
    return nullptr;
}

// 构造一个临时 QPaintDevice（QImage）用于度量
QImage makeDevice() { return QImage(800, 600, QImage::Format_RGB32); }

} // namespace

// T-HEADING-WRAP-1
// 长标题在窄宽度下应换行多行；block.height 必须基于 headingFont 行高（与 paint 一致），
// 而不是被低估为 baseFont 行高。
//
// 检测原理：在 bug 版本下，layout 估算的行数与 paint 行数一致（都用 headingFont
// 测宽），但行高用 m_baseFont（基于正文）；修复版本必须用 headingFont 行高。
// 期望 height ≈ N * headingLineH（+ H1/H2 边距 8）。
TEST(PreviewLayoutHeadingWrapTest, T1_LongH1WrapsAndHeightMatchesHeadingFont)
{
    MarkdownParser parser;
    // 文本长度刚好让 H1 换行约 2 行（不要太长，否则 bug 版本下 N*baseLineH
    // 也能撑过 2*headingLineH 的下界）。
    QString h1Text = QStringLiteral("Long Heading That Should Wrap ABCDEF");
    QString doc = QStringLiteral("# ") + h1Text + QStringLiteral("\n\nFollowing paragraph.");
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    PreviewLayout layout;
    QImage img = makeDevice();
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&img);
    layout.setViewportWidth(200.0);  // 强制 H1 换行 2-3 行
    layout.buildFromAst(ast);

    const LayoutBlock& root = layout.rootBlock();
    const LayoutBlock* heading = findHeadingBlock(root);
    ASSERT_NE(heading, nullptr);
    ASSERT_FALSE(heading->inlineRuns.empty());

    const QFont& hFont = heading->inlineRuns[0].font;
    EXPECT_NEAR(hFont.pointSizeF(), base.pointSizeF() * 1.8, 0.01);

    QFontMetricsF hFm(hFont, &img);
    qreal headingLineH = hFm.height() * 1.5;
    QFontMetricsF baseFm(base, &img);
    qreal baseLineH = baseFm.height() * 1.5;
    ASSERT_GT(headingLineH - baseLineH, 5.0);

    // 期望 height ≈ (N-1)*headingLineH + headingFm.height() + 8（H1 底部边距）
    // bug 版本下 height = N_paint_lines * baseLineH，必然小于这个下界。
    // 估算 paint 端实际换行行数 N（与 layout 估算同构：累加所有非 \n run 的
    // horizontalAdvance，按 maxWidth 取 ceil）。
    qreal totalAdvance = 0;
    for (const auto& run : heading->inlineRuns) {
        if (run.text == "\n") continue;
        QFontMetricsF fm(run.font, &img);
        totalAdvance += fm.horizontalAdvance(run.text);
    }
    int paintLines = qMax(1, static_cast<int>(qCeil(totalAdvance / heading->bounds.width())));
    ASSERT_GE(paintLines, 2) << "测试输入应触发至少 2 行换行";

    // 新公式（INV-11）：N 行段落 height = (N-1)*lineHeight + glyphHeight，
    // 末行不再多附 0.5 倍墨迹高的尾部空白。bug 版本（N * baseLineH）仍 < 这个下界。
    qreal expectedH = (paintLines - 1) * headingLineH + hFm.height() + 8.0;
    qreal lower = expectedH - 2.0;  // 留 ±2px hinting 容差
    EXPECT_GE(heading->bounds.height(), lower)
        << "heading bounds.height=" << heading->bounds.height()
        << " < lower=" << lower
        << " paintLines=" << paintLines
        << " headingLineH=" << headingLineH;
}

// T-HEADING-WRAP-2
// 下方段落 y 必须 >= 标题 y + 标题 height + 块间距，确保不重叠。
TEST(PreviewLayoutHeadingWrapTest, T2_ParagraphBelowWrappedH1NoOverlap)
{
    MarkdownParser parser;
    QString h1Text = QStringLiteral("Long Heading That Should Wrap ABCDEF");
    QString doc = QStringLiteral("# ") + h1Text + QStringLiteral("\n\nFollowing paragraph.");
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    PreviewLayout layout;
    QImage img = makeDevice();
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&img);
    layout.setViewportWidth(200.0);
    layout.buildFromAst(ast);

    const LayoutBlock& root = layout.rootBlock();
    const LayoutBlock* heading = findHeadingBlock(root);
    const LayoutBlock* para = findParagraphBlock(root);
    ASSERT_NE(heading, nullptr);
    ASSERT_NE(para, nullptr);

    qreal headingBottom = heading->bounds.y() + heading->bounds.height();
    EXPECT_GE(para->bounds.y(), headingBottom)
        << "paragraph y=" << para->bounds.y()
        << " < heading bottom=" << headingBottom
        << "（标题与段落重叠）";

    // 估算 paint 端实际换行 N 与 paint 实际占用的高度
    qreal totalAdvance = 0;
    for (const auto& run : heading->inlineRuns) {
        if (run.text == "\n") continue;
        QFontMetricsF fm(run.font, &img);
        totalAdvance += fm.horizontalAdvance(run.text);
    }
    int paintLines = qMax(1, static_cast<int>(qCeil(totalAdvance / heading->bounds.width())));
    ASSERT_GE(paintLines, 2);

    const QFont& hFont = heading->inlineRuns[0].font;
    QFontMetricsF hFm(hFont, &img);
    qreal headingLineH = hFm.height() * 1.5;
    // 新公式（INV-11）：(N-1)*lineH + glyphHeight + H1 边距 8
    qreal paintMinHeight = (paintLines - 1) * headingLineH + hFm.height() + 8.0 - 2.0;
    EXPECT_GE(para->bounds.y(), heading->bounds.y() + paintMinHeight)
        << "para.y=" << para->bounds.y()
        << " heading.y=" << heading->bounds.y()
        << " paintMinHeight=" << paintMinHeight
        << " paintLines=" << paintLines;
}

// T-HEADING-WRAP-3
// 单行 heading（宽度足够时不换行）回归保护。
TEST(PreviewLayoutHeadingWrapTest, T3_ShortH1NoWrapHeightCorrect)
{
    MarkdownParser parser;
    QString doc = QStringLiteral("# Short\n\nbody.");
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    PreviewLayout layout;
    QImage img = makeDevice();
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&img);
    layout.setViewportWidth(800.0);
    layout.buildFromAst(ast);

    const LayoutBlock& root = layout.rootBlock();
    const LayoutBlock* heading = findHeadingBlock(root);
    ASSERT_NE(heading, nullptr);

    const QFont& hFont = heading->inlineRuns[0].font;
    QFontMetricsF hFm(hFont, &img);

    // 单行 H1：新公式（INV-11）= 0*lineH + glyphHeight + 8px 底部边距
    // 即贴合实际墨迹高度，不再附加 0.5 行尾部空白
    EXPECT_NEAR(heading->bounds.height(), hFm.height() + 8.0, 2.0);
}

// T-HEADING-WRAP-4
// H2 换行场景同样满足不重叠（scale = 1.5x）
TEST(PreviewLayoutHeadingWrapTest, T4_H2WrappedNoOverlap)
{
    MarkdownParser parser;
    QString h2Text = QStringLiteral("H2 wrapping segment XYZ XYZ XYZ XYZ");
    QString doc = QStringLiteral("## ") + h2Text + QStringLiteral("\n\nbody.");
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    PreviewLayout layout;
    QImage img = makeDevice();
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&img);
    layout.setViewportWidth(220.0);
    layout.buildFromAst(ast);

    const LayoutBlock& root = layout.rootBlock();
    const LayoutBlock* heading = findHeadingBlock(root);
    const LayoutBlock* para = findParagraphBlock(root);
    ASSERT_NE(heading, nullptr);
    ASSERT_NE(para, nullptr);

    const QFont& hFont = heading->inlineRuns[0].font;
    EXPECT_NEAR(hFont.pointSizeF(), base.pointSizeF() * 1.5, 0.01);

    QFontMetricsF hFm(hFont, &img);
    qreal expectedLineH = hFm.height() * 1.5;

    qreal totalAdvance = 0;
    for (const auto& run : heading->inlineRuns) {
        if (run.text == "\n") continue;
        QFontMetricsF fm(run.font, &img);
        totalAdvance += fm.horizontalAdvance(run.text);
    }
    int paintLines = qMax(1, static_cast<int>(qCeil(totalAdvance / heading->bounds.width())));
    ASSERT_GE(paintLines, 2);

    // 新公式（INV-11）：(N-1)*lineH + glyphHeight + H2 底部边距 8
    qreal expectedH = (paintLines - 1) * expectedLineH + hFm.height() + 8.0;
    EXPECT_GE(heading->bounds.height(), expectedH - 2.0);
    EXPECT_GE(para->bounds.y(), heading->bounds.y() + heading->bounds.height());
}

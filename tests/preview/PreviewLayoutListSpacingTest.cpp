// Spec: specs/模块-preview/02-布局引擎.md INV-11, INV-12
// 验收：
//   T-LIST-SPACING-1  连续两项含粗体的列表，相邻 ListItem 的视觉距离应紧凑
//                     —— ListItem.bounds.height 紧贴单行墨迹高度（fm.height()），
//                        不再被 1.5x 行高公式抬高 0.5 行的尾部空白
//   T-LIST-SPACING-2  纯文本两项 vs 含粗体两项，项间距差异 < 1px（粗体不影响行高）
//   T-LIST-SPACING-3  下一个列表项的 absY = 前一项底部 + 4px (List 内部固定项间距)
//   T-LIST-SPACING-4  字体度量缓存键不能因 qHash(QFont) 冲突让 H1 度量串到列表
//                     bold 段落（INV-12，配合"先 H1 后 list bold" 的真实复现）
#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QtMath>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewLayoutListSpacingTest";
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

QImage makeDevice() { return QImage(800, 600, QImage::Format_RGB32); }

const LayoutBlock* findFirstList(const LayoutBlock& root)
{
    for (const auto& c : root.children) {
        if (c.type == LayoutBlock::List) return &c;
    }
    return nullptr;
}

} // namespace

// T-LIST-SPACING-1
// 含粗体的两项列表（用户复现样本），ListItem.bounds.height 应紧贴单行墨迹高度。
// bug 版本下 height = lineHeight = fm.height() * 1.5，多出 0.5 倍尾部空白。
// 修复版本下 height ≈ fm.height()（单行段落不附加 0.5 行尾部空白）。
TEST(PreviewLayoutListSpacingTest, T1_BoldListItemHeightMatchesGlyphHeight)
{
    MarkdownParser parser;
    QString doc = QStringLiteral(
        "- **DrawBoard**\xef\xbc\x9a\xe5\x9f\xba\xe4\xba\x8e Qt Graphics Framework "
        "\xe7\x9a\x84\xe6\xb8\xb2\xe6\x9f\x93\xe5\x92\x8c\xe4\xba\xa4\xe4\xba\x92\xe5\xb1\x82\n"
        "- **VirtualCanvas**\xef\xbc\x9a\xe5\xb9\xb3\xe5\x8f\xb0\xe6\x97\xa0\xe5\x85\xb3"
        "\xe7\x9a\x84\xe7\x94\xbb\xe5\xb8\x83\xe6\x95\xb0\xe6\x8d\xae\xe6\xa8\xa1\xe5\x9e\x8b"
        "\xe5\x92\x8c\xe5\xba\x8f\xe5\x88\x97\xe5\x8c\x96\xe5\xb1\x82\n");
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    PreviewLayout layout;
    QImage img = makeDevice();
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&img);
    layout.setViewportWidth(800.0);  // 足够宽不触发换行
    layout.buildFromAst(ast);

    QFont bold = base; bold.setWeight(QFont::Bold);
    QFontMetricsF boldFm(bold, &img);
    qreal boldLineH = boldFm.height() * 1.5;
    qreal boldGlyphH = boldFm.height();

    const LayoutBlock& root = layout.rootBlock();
    const LayoutBlock* listBlock = findFirstList(root);
    ASSERT_NE(listBlock, nullptr);
    ASSERT_EQ(listBlock->children.size(), 2u);

    for (size_t i = 0; i < listBlock->children.size(); ++i) {
        const LayoutBlock& item = listBlock->children[i];
        ASSERT_EQ(item.type, LayoutBlock::ListItem);
        // 修复后：单行段落 height ≈ fm.height()
        // bug 版本下 height = lineHeight = fm.height() * 1.5
        EXPECT_NEAR(item.bounds.height(), boldGlyphH, 2.0)
            << "item " << i << " height=" << item.bounds.height()
            << " expected ≈ " << boldGlyphH
            << " (bug 版本会等于 boldLineH=" << boldLineH << ")";
    }
}

// T-LIST-SPACING-2
// 纯文本列表与含粗体列表的项间距应基本一致（差 < 1px），
// 粗体不应导致项内被多估一行高度。
TEST(PreviewLayoutListSpacingTest, T2_BoldVsPlainListItemSpacingMatches)
{
    MarkdownParser parser;
    QString plainDoc = QStringLiteral("- DrawBoard\n- VirtualCanvas\n");
    QString boldDoc  = QStringLiteral("- **DrawBoard**\n- **VirtualCanvas**\n");

    auto pickItemDelta = [&](const QString& src) -> qreal {
        auto astUnique = parser.parse(src);
        EXPECT_NE(astUnique, nullptr);
        std::shared_ptr<AstNode> ast(std::move(astUnique));
        PreviewLayout layout;
        QImage img = makeDevice();
        QFont base("Segoe UI", 12);
        layout.setFont(base);
        layout.updateMetrics(&img);
        layout.setViewportWidth(800.0);
        layout.buildFromAst(ast);
        const LayoutBlock* listBlock = findFirstList(layout.rootBlock());
        EXPECT_NE(listBlock, nullptr);
        EXPECT_EQ(listBlock->children.size(), 2u);
        // 第二项 absY - 第一项 absY = 第一项 height + 项间距 4
        return listBlock->children[1].bounds.y() - listBlock->children[0].bounds.y();
    };

    qreal plainDelta = pickItemDelta(plainDoc);
    qreal boldDelta  = pickItemDelta(boldDoc);

    EXPECT_NEAR(plainDelta, boldDelta, 1.0)
        << "plain item delta=" << plainDelta
        << " bold item delta=" << boldDelta
        << "（粗体不应额外撑高项间距）";
}

// T-LIST-SPACING-3
// 验证 List 内部项间距 = 4px（紧凑列表渲染惯例）。
// 即 item[1].y - (item[0].y + item[0].height) ≈ 4。
// bug 版本下 item.height 多估 0.5 行，视觉间距会 = 4 + 0.5*lineH，约 14~21px，明显偏大。
TEST(PreviewLayoutListSpacingTest, T3_AdjacentItemsHaveTightInternalSpacing)
{
    MarkdownParser parser;
    QString doc = QStringLiteral("- **alpha**\n- **beta**\n");
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

    const LayoutBlock* listBlock = findFirstList(layout.rootBlock());
    ASSERT_NE(listBlock, nullptr);
    ASSERT_EQ(listBlock->children.size(), 2u);
    const LayoutBlock& a = listBlock->children[0];
    const LayoutBlock& b = listBlock->children[1];

    qreal aBottom = a.bounds.y() + a.bounds.height();
    qreal gap = b.bounds.y() - aBottom;
    // 期望 gap ≈ 4 (List layout 中固定加的 spacing)。
    // bug 版本下 a.bounds.height 多估 0.5 行 → b.y - aBottom = 4，但 a.bounds.height
    // 会比墨迹底部高 0.5 行——视觉上墨迹之间的空白 = 4 + 0.5 * fm.height() * 1.5 ≈ 25px。
    // 这里的 gap（layout 上的）总是 4，所以这条测试主要靠 T1/T2 守护"item.height 不被多估"。
    EXPECT_NEAR(gap, 4.0, 0.1)
        << "a.bottom=" << aBottom << " b.y=" << b.bounds.y();

    // 视觉间距：紧凑列表项之间真正的空白（layout 上的 gap + ListItem 末行墨迹底部到 height 边界的 0 距离）
    // 修复后 ListItem.height ≈ glyphHeight，故视觉空白 = 4 + 0（即 4px）。
    QFont bold = base; bold.setWeight(QFont::Bold);
    QFontMetricsF boldFm(bold, &img);
    qreal visualWhitespace = (b.bounds.y() - a.bounds.y()) - boldFm.height();
    EXPECT_LT(visualWhitespace, boldFm.height() * 0.6)
        << "相邻列表项墨迹间空白=" << visualWhitespace
        << " 应 < 0.6 * 字号(" << boldFm.height() << ")"
        << "（bug 版本下空白 ≈ fm.height(), 即一行高度）";
}

// T-LIST-SPACING-4
// [Spec INV-12] 字体度量缓存键唯一性：模拟"新建 tab + 粘贴"实际触发顺序——
// 文档头部是 H1（heading 用 1.8x 加粗字体），随后是连续的 bold 列表项。
// bug 版本下 cachedFontMetrics 用 qHash(QFont) 作 key，Qt 5.12 对
// "同 family、同 weight、同 italic、不同 pointSize" 会返回同一个 hash，
// H1 的 QFontMetricsF（高度 ≈ 1.8 * baseGlyphHeight）会被列表项的
// bold 段落复用，导致 estimateParagraphHeight 把单行段落估为多行，
// ListItem 高度暴增到 baseGlyphHeight 的 4~5 倍，相邻列表项间出现一整行空白。
// 缩放可以掩盖问题，因为 setFont 改变 pointSize 后 hash 不再冲突。
//
// 该测试在同一 PreviewLayout 实例中先布局 H1、再布局含粗体的列表，验证
// list item 高度仍接近单行墨迹高，与 H1 度量隔离。
TEST(PreviewLayoutListSpacingTest, T4_HeadingDoesNotPolluteListItemMetrics)
{
    MarkdownParser parser;
    // 与用户复现一致：H1 + 介绍段 + bold 列表（newTab 默认模板 + 粘贴的合成内容）
    QString doc = QStringLiteral(
        "# SimpleMarkdown\n"
        "\n"
        "A **lightweight** cross-platform Markdown editor.\n"
        "\n"
        "- **AI \xe6\x99\xba\xe8\x83\xbd\xe8\xaf\x86\xe5\x88\xab**\xef\xbc\x9a"
        "\xe6\x89\x8b\xe5\x86\x99\xe4\xb8\xad\xe8\x8b\xb1\xe6\x96\x87\xe8\xaf\x86\xe5\x88\xab\n"
        "- **\xe5\xad\xa6\xe7\xa7\x91\xe5\xb7\xa5\xe5\x85\xb7**\xef\xbc\x9a"
        "150+ \xe7\xa7\x8d\xe6\x95\x99\xe5\xad\xa6\xe5\xb7\xa5\xe5\x85\xb7\n");
    auto astUnique = parser.parse(doc);
    ASSERT_NE(astUnique, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astUnique));

    PreviewLayout layout;
    QImage img = makeDevice();
    // 用 SimSun（与用户机器一致）和 9pt（base）—— 复现关键：
    // headingFont = 1.8 * 9 = 16.2pt bold；listBoldFont = 9pt bold；
    // 在 Qt 5.12 中两者 qHash 相等。
    QFont base("SimSun", 9);
    layout.setFont(base);
    layout.updateMetrics(&img);
    layout.setViewportWidth(800.0);
    layout.buildFromAst(ast);

    QFont bold = base; bold.setWeight(QFont::Bold);
    QFontMetricsF boldFm(bold, &img);
    qreal boldGlyphH = boldFm.height();

    const LayoutBlock* listBlock = findFirstList(layout.rootBlock());
    ASSERT_NE(listBlock, nullptr);
    ASSERT_EQ(listBlock->children.size(), 2u);

    for (size_t i = 0; i < listBlock->children.size(); ++i) {
        const LayoutBlock& item = listBlock->children[i];
        ASSERT_EQ(item.type, LayoutBlock::ListItem);
        // 期望 item 高度 ≈ 单行墨迹高度。bug 版本下 item.height 会变成
        // 1.8x baseGlyphH 的 4~5 倍（H1 度量被错误复用 + 多行估算）。
        EXPECT_LT(item.bounds.height(), boldGlyphH * 1.8)
            << "item " << i << " height=" << item.bounds.height()
            << " 期望 < " << (boldGlyphH * 1.8)
            << "（bug 版本下会等于 (totalLines-1)*1.8*1.5*baseH + 1.8*baseH，约为 4~5 倍单行）";
        EXPECT_NEAR(item.bounds.height(), boldGlyphH, 4.0)
            << "item " << i << " height=" << item.bounds.height()
            << " 应紧贴单行墨迹高 " << boldGlyphH;
    }
}

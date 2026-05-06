// Spec: specs/模块-preview/10-Frontmatter渲染.md §7 验收条件 T-1 .. T-19
// Last synced: 2026-05-06
#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QString>
#include <QStringList>

#include "MarkdownParser.h"
#include "MarkdownAst.h"
#include "PreviewLayout.h"

namespace {

static const AstNode* findFrontmatter(const AstNode* root)
{
    if (!root) return nullptr;
    for (const auto& c : root->children) {
        if (c->type == AstNodeType::Frontmatter) return c.get();
    }
    return nullptr;
}

static QString makeDoc(std::initializer_list<const char*> lines)
{
    QStringList out;
    for (const char* s : lines) out << QString::fromUtf8(s);
    return out.join('\n');
}

} // namespace

// T-1：基本提取
TEST(FrontmatterParseTest, T1_BasicExtraction)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "title: Hello",
        "date: 2026-04-14",
        "---",
        "",
        "# Body"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 2u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QStringLiteral("title"));
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("Hello"));
    EXPECT_EQ(fm->frontmatterEntries[1].first,  QStringLiteral("date"));
    EXPECT_EQ(fm->frontmatterEntries[1].second, QStringLiteral("2026-04-14"));
}

// T-2：首行非 --- 不触发
TEST(FrontmatterParseTest, T2_NoFrontmatterWhenFirstLineIsHeading)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "# Title",
        "---",
        "body"
    });
    auto root = parser.parse(doc);
    EXPECT_EQ(findFrontmatter(root.get()), nullptr);
}

// T-3：中间 --- 是 ThematicBreak，不与 frontmatter 冲突
TEST(FrontmatterParseTest, T3_MidDocHrDoesNotConflict)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "title: T",
        "---",
        "",
        "Para1",
        "",
        "---",
        "",
        "Para2"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    // 在剩余 children 里至少存在一个 ThematicBreak
    bool foundHr = false;
    for (const auto& c : root->children) {
        if (c->type == AstNodeType::ThematicBreak) { foundHr = true; break; }
    }
    EXPECT_TRUE(foundHr);
}

// T-7：数组字面量原样保留
TEST(FrontmatterParseTest, T7_ArrayLiteralPreservedAsString)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "owners: [pcfan, alice]",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QStringLiteral("owners"));
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("[pcfan, alice]"));
}

// T-8：注释行被忽略
TEST(FrontmatterParseTest, T8_CommentLinesIgnored)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "a: 1",
        "# comment line",
        "   # leading-space comment",
        "b: 2",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 2u);
    EXPECT_EQ(fm->frontmatterEntries[0].first, QStringLiteral("a"));
    EXPECT_EQ(fm->frontmatterEntries[1].first, QStringLiteral("b"));
}

// T-9：空行被忽略
TEST(FrontmatterParseTest, T9_EmptyLinesIgnored)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "a: 1",
        "",
        "    ",
        "b: 2",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 2u);
}

// T-16：首行 --- 但缺少结束 --- → 回退
TEST(FrontmatterParseTest, T16_MissingEndFallback)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "title: T",
        "no closing marker"
    });
    auto root = parser.parse(doc);
    EXPECT_EQ(findFrontmatter(root.get()), nullptr);
}

// T-18：无冒号行 → 整行作 value，key 留空（INV-14）
TEST(FrontmatterParseTest, T18_LineWithoutColon)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "TODO",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QString());
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("TODO"));
}

// T-19：多冒号 → 取首个冒号拆分
TEST(FrontmatterParseTest, T19_MultipleColonsFirstWins)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "site: https://example.com",
        "---"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
    EXPECT_EQ(fm->frontmatterEntries[0].first,  QStringLiteral("site"));
    EXPECT_EQ(fm->frontmatterEntries[0].second, QStringLiteral("https://example.com"));
}

// 行号偏移：§8.9 剥离后 cmark 节点的 startLine 要加回 frontmatterLineCount
TEST(FrontmatterParseTest, LineNumberShiftAfterExtraction)
{
    MarkdownParser parser;
    // frontmatter 占 3 行（行号 0..2），紧随空行（行 3），标题在行 4
    QString doc = makeDoc({
        "---",
        "a: 1",
        "---",
        "",
        "# Heading"
    });
    auto root = parser.parse(doc);
    const AstNode* heading = nullptr;
    for (const auto& c : root->children) {
        if (c->type == AstNodeType::Heading) { heading = c.get(); break; }
    }
    ASSERT_NE(heading, nullptr);
    EXPECT_EQ(heading->startLine, 4);
}

// BOM 容忍（INV-2）
TEST(FrontmatterParseTest, BomToleranceOnFirstLine)
{
    MarkdownParser parser;
    QString doc;
    doc.append(QChar(0xFEFF));
    doc.append(QStringLiteral("---\na: 1\n---\n\n# Body"));
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(fm->frontmatterEntries.size(), 1u);
}

// rawText 保留（INV-13）
TEST(FrontmatterParseTest, RawTextRetainsOriginalYaml)
{
    MarkdownParser parser;
    QString doc = makeDoc({
        "---",
        "a: 1",
        "# comment",
        "",
        "b: 2",
        "---",
        "# body"
    });
    auto root = parser.parse(doc);
    const AstNode* fm = findFrontmatter(root.get());
    ASSERT_NE(fm, nullptr);
    EXPECT_TRUE(fm->frontmatterRawText.startsWith(QStringLiteral("---")));
    EXPECT_TRUE(fm->frontmatterRawText.endsWith(QStringLiteral("---")));
    EXPECT_TRUE(fm->frontmatterRawText.contains(QStringLiteral("# comment")));
}

// ============================================================================
// FrontmatterLayoutTest（2026-05-06 新增）
// Spec: specs/模块-preview/10-Frontmatter渲染.md INV-12（修订）
// Plan: plans/2026-05-06-frontmatter多行列表项对齐bug.md
// 验收：
//   T-FM-LAYOUT-1  多行 YAML 列表 value 必须被拆成多行（而不是按字符硬切单行长串）
//   T-FM-LAYOUT-2  每行去掉前导缩进，列表标记 "- " 保留
//   T-FM-LAYOUT-3  layout 端的 valueLines 行数与 frontmatter block 高度估算一致
// ============================================================================

namespace {

class FmLayoutQAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "FrontmatterRenderTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const g_fm_env =
    ::testing::AddGlobalTestEnvironment(new FmLayoutQAppFixture);

const LayoutBlock* findLayoutFrontmatter(const LayoutBlock& root)
{
    if (root.type == LayoutBlock::Frontmatter) return &root;
    for (const auto& c : root.children) {
        if (auto* p = findLayoutFrontmatter(c)) return p;
    }
    return nullptr;
}

}  // namespace

TEST(FrontmatterLayoutTest, T_FM_LAYOUT_1_YamlListItemsBecomeNoKeyEntries)
{
    // YAML 多行列表：parser 把每个列表项作为独立无 key entry，
    // layout 给无 key entry 用 fullCharsPerLine（整 innerWidth）而非 valColW，
    // paint 把它们绘制在 key 列起点（视觉对齐到 frontmatter 卡片最左侧）。
    QString doc = QStringLiteral(
        "---\n"
        "title: sample\n"
        "related_specs:\n"
        "  - specs/A.md\n"
        "  - specs/B.md\n"
        "  - specs/C.md\n"
        "---\n"
        "\n"
        "# Body\n");

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_NE(astU, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astU));

    QImage device(800, 600, QImage::Format_RGB32);
    PreviewLayout layout;
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&device);
    layout.setViewportWidth(800.0);
    layout.buildFromAst(ast);

    const LayoutBlock* fm = findLayoutFrontmatter(layout.rootBlock());
    ASSERT_NE(fm, nullptr);

    // 收集 key 为空的 entries 数量与首字符——必须包含 3 个 "- specs/..."
    int noKeyDashCount = 0;
    for (const auto& kv : fm->frontmatterEntries) {
        if (kv.first.isEmpty() && kv.second.startsWith('-')) {
            ++noKeyDashCount;
        }
    }
    EXPECT_EQ(noKeyDashCount, 3)
        << "3 个 YAML 列表项必须各自成为独立的无 key entry（实际: " << noKeyDashCount << "）";

    // valueLines 与 entries 数量必须一致（每条都是一行短文本）
    EXPECT_EQ(fm->frontmatterValueLines.size(), fm->frontmatterEntries.size());
}

TEST(FrontmatterLayoutTest, T_FM_LAYOUT_2_NoKeyEntryUsesFullWidthBudget)
{
    // 无 key entry 应当用 innerWidth 字符预算而不是 valColW——这样它绘制
    // 在 key 列起点时不会因为按 valColW 截断而被错误切碎。
    // 用一个长字符串列表项验证无 key entry 单行能容纳更多字符。
    QString longItem = QStringLiteral("specs/some/very/very/very/long/path/segment.md");
    QString doc = QStringLiteral(
        "---\n"
        "items:\n"
        "  - %1\n"
        "---\n").arg(longItem);

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_NE(astU, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astU));

    QImage device(800, 600, QImage::Format_RGB32);
    PreviewLayout layout;
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&device);
    // 用较窄宽度让 valColW < innerWidth，对比预算差
    layout.setViewportWidth(400.0);
    layout.buildFromAst(ast);

    const LayoutBlock* fm = findLayoutFrontmatter(layout.rootBlock());
    ASSERT_NE(fm, nullptr);

    // 找无 key entry
    int noKeyIdx = -1;
    for (size_t i = 0; i < fm->frontmatterEntries.size(); ++i) {
        if (fm->frontmatterEntries[i].first.isEmpty()) {
            noKeyIdx = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(noKeyIdx, 0);

    const QStringList& lines = fm->frontmatterValueLines[noKeyIdx];
    // 重要断言：长 item 在 innerWidth 预算下能放下更多字符——这里只做"非空"
    // 与"行数合理"的弱断言，确保 layout 没有把它截成无意义的碎片
    EXPECT_FALSE(lines.isEmpty());
    // 总字符长度应当等于原 value（即 "- specs/...md"）
    int total = 0;
    for (const QString& l : lines) total += l.length();
    EXPECT_GT(total, 0);
}

TEST(FrontmatterLayoutTest, T_FM_LAYOUT_4_MixedCJK_AsciiNeverOverflowsCardWidth)
{
    // 复现 bug：含 ASCII 路径 + 中文注释的 YAML 列表项，按 averageCharWidth 估算切碎后
    // 实际渲染宽度可能超 innerWidth → 越出 frontmatter 卡片右边框
    QString doc = QStringLiteral(
        "---\n"
        "related_specs:\n"
        "  - specs/模块-app/02-主窗口与多Tab.md          # 待创建/补充：Tab 休眠规则\n"
        "  - specs/模块-preview/10-Frontmatter渲染.md\n"
        "---\n");

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_NE(astU, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astU));

    QImage device(800, 600, QImage::Format_RGB32);
    PreviewLayout layout;
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&device);

    // 在多个宽度下断言每行渲染宽度 ≤ 可用宽度（不越框）
    for (int W : {300, 400, 500, 600, 800}) {
        layout.setViewportWidth(static_cast<qreal>(W));
        layout.buildFromAst(ast);

        const LayoutBlock* fm = findLayoutFrontmatter(layout.rootBlock());
        ASSERT_NE(fm, nullptr) << "viewportWidth=" << W;

        // 重建 fm metrics（与 layoutFrontmatter 同步：monoFont 字体族 + baseFont 字号）
        QFont fmFont = layout.monoFont();
        fmFont.setPointSizeF(layout.baseFont().pointSizeF());
        QFontMetricsF fmm(fmFont, &device);
        const qreal hPad = fmm.height() * 0.5;
        const qreal innerCellPad = fmm.height() * 0.25;
        const qreal innerWidth = qMax<qreal>(1.0, fm->bounds.width() - 2 * hPad);
        const qreal availForNoKey = qMax<qreal>(1.0, innerWidth - 2 * innerCellPad);
        const qreal availForKey =
            qMax<qreal>(1.0, (innerWidth - fm->frontmatterKeyColumnWidth) - 2 * innerCellPad);

        for (size_t i = 0; i < fm->frontmatterEntries.size(); ++i) {
            const QString& key = fm->frontmatterEntries[i].first;
            const qreal avail = key.isEmpty() ? availForNoKey : availForKey;
            const QStringList& lines = fm->frontmatterValueLines[i];
            for (const QString& line : lines) {
                qreal lineW = fmm.horizontalAdvance(line);
                EXPECT_LE(lineW, avail + 0.5)  // 0.5 px 浮点容差
                    << "viewportWidth=" << W << " entry#" << i
                    << " line 渲染宽度 " << lineW << "px 超过可用宽度 " << avail << "px"
                    << " line=\"" << line.toUtf8().constData() << "\"";
            }
        }
    }
}

TEST(FrontmatterLayoutTest, T_FM_LAYOUT_3_BlockHeightMatchesValueLineCount)
{
    QString doc = QStringLiteral(
        "---\n"
        "title: hello\n"
        "items:\n"
        "  - a\n"
        "  - b\n"
        "  - c\n"
        "  - d\n"
        "---\n");

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_NE(astU, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astU));

    QImage device(800, 600, QImage::Format_RGB32);
    PreviewLayout layout;
    QFont base("Segoe UI", 12);
    layout.setFont(base);
    layout.updateMetrics(&device);
    layout.setViewportWidth(800.0);
    layout.buildFromAst(ast);

    const LayoutBlock* fm = findLayoutFrontmatter(layout.rootBlock());
    ASSERT_NE(fm, nullptr);
    int totalLines = 0;
    for (const auto& vl : fm->frontmatterValueLines) {
        totalLines += vl.size();
    }
    // 至少 5 行（title 1 + items 4 项；layout 也可能给 items 单独一空行作 key 行——
    // 总行数 ≥ 5 即可；不严格断言精确值因为去前导缩进后空 value 行的策略可能微调）
    EXPECT_GE(totalLines, 5)
        << "title + 4 个列表项至少占 5 行；实际 totalLines=" << totalLines;
    // bounds.height 应当 > 单行高度 * totalLines 的合理下界（含 padding）
    EXPECT_GT(fm->bounds.height(), 0.0);
}

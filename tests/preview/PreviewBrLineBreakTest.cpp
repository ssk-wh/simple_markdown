// tests/preview/PreviewBrLineBreakTest.cpp
//
// Spec: specs/模块-preview/02-布局引擎.md INV-BR-LINEBREAK（行内 <br> 归一化为换行）
// Bug:  plans/2026-06-16-单元格内br换行未生效且显示原文.md
//
// 验证：collectInlineRuns 把行内 <br>/<br/>/<br /> 归一化为 "\n" run（与 LineBreak 同通路），
//       既不显示原文 "<br>"，又触发换行（高度增加），复制语义产出 "\n"。
//
// 验收：
//   T-BR-1  段落内 <br> → 产生 "\n" run，不含 "<br>" 原文，纯文本为 "line1\nline2"
//   T-BR-2  <br/> 与 <br /> 变体同样换行
//   T-BR-3  含 <br> 的段落高度 > 同内容单行（无 br）段落——确实换行占两行
//   T-BR-4  表格单元格内 <br> 换行且不显示原文

#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QString>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"
#include "FontDefaults.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewBrLineBreakTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
private:
    QApplication* app_ = nullptr;
};
::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

// 递归收集所有 inlineRuns 的 text 拼接（不加块间换行，只看 run 级文本流）
void collectRunText(const LayoutBlock& block, QString& out)
{
    for (const auto& run : block.inlineRuns) out += run.text;
    for (const auto& child : block.children) collectRunText(child, out);
}

// 构建 layout，返回 run 级文本流
QString buildRunText(const QString& doc, int W = 600)
{
    MarkdownParser parser;
    auto astU = parser.parse(doc);
    EXPECT_TRUE(astU);
    std::shared_ptr<AstNode> ast(std::move(astU));

    QImage device(W, 400, QImage::Format_ARGB32);
    PreviewLayout layout;
    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);
    layout.buildFromAst(ast);

    QString out;
    collectRunText(layout.rootBlock(), out);
    return out;
}

qreal buildTotalHeight(const QString& doc, int W = 600)
{
    MarkdownParser parser;
    auto astU = parser.parse(doc);
    EXPECT_TRUE(astU);
    std::shared_ptr<AstNode> ast(std::move(astU));

    QImage device(W, 400, QImage::Format_ARGB32);
    PreviewLayout layout;
    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);
    layout.buildFromAst(ast);
    return layout.totalHeight();
}

}  // namespace

TEST(PreviewBrLineBreakTest, T_BR_1_ParagraphBrBecomesNewline)
{
    QString text = buildRunText(QStringLiteral("line1<br>line2\n"));
    fprintf(stderr, "[br] T1 runText=%s\n", text.toUtf8().toPercentEncoding().constData());

    EXPECT_FALSE(text.contains(QStringLiteral("<br>"))) << "不应显示 <br> 原文";
    EXPECT_TRUE(text.contains(QChar('\n'))) << "<br> 应产生换行";
    EXPECT_TRUE(text.contains(QStringLiteral("line1\nline2"))) << "换行应落在 line1 与 line2 之间";
}

TEST(PreviewBrLineBreakTest, T_BR_2_BrVariantsAllBreak)
{
    for (const QString& tag : {QStringLiteral("<br/>"), QStringLiteral("<br />"),
                                QStringLiteral("<BR>"), QStringLiteral("<br  />")}) {
        QString doc = QStringLiteral("alpha") + tag + QStringLiteral("beta\n");
        QString text = buildRunText(doc);
        fprintf(stderr, "[br] T2 tag=%s runText=%s\n",
                tag.toUtf8().constData(), text.toUtf8().toPercentEncoding().constData());
        EXPECT_FALSE(text.contains(tag)) << "变体 " << tag.toStdString() << " 不应显示原文";
        EXPECT_TRUE(text.contains(QStringLiteral("alpha\nbeta")))
            << "变体 " << tag.toStdString() << " 应换行";
    }
}

TEST(PreviewBrLineBreakTest, T_BR_3_BrIncreasesHeight)
{
    // 同内容：一个用 <br> 强制两行，一个纯单行短文本。带 br 的高度应明显更高。
    qreal hWithBr = buildTotalHeight(QStringLiteral("aaa<br>bbb\n"));
    qreal hSingle = buildTotalHeight(QStringLiteral("aaa bbb\n"));
    fprintf(stderr, "[br] T3 hWithBr=%.1f hSingle=%.1f\n", hWithBr, hSingle);
    EXPECT_GT(hWithBr, hSingle) << "<br> 应使段落占两行，高度大于单行";
}

TEST(PreviewBrLineBreakTest, T_BR_4_TableCellBr)
{
    const QString doc = QStringLiteral(
        "| 列A | 列B |\n"
        "|-----|-----|\n"
        "| first<br>second | plain |\n");
    QString text = buildRunText(doc);
    fprintf(stderr, "[br] T4 runText=%s\n", text.toUtf8().toPercentEncoding().constData());

    EXPECT_FALSE(text.contains(QStringLiteral("<br>"))) << "单元格不应显示 <br> 原文";
    EXPECT_TRUE(text.contains(QStringLiteral("first\nsecond"))) << "单元格内 <br> 应换行";
}

// tests/preview/PreviewCopyConsistencyTest.cpp
//
// Spec: specs/模块-preview/13-复制语义.md
//       specs/模块-preview/12-选区与拖动.md（选区索引空间）
// Bug:  plans/2026-06-15-渲染区单元格复制粘贴内容不一致回归.md
//
// 核心不变量（复制即 m_plainText.mid(selStart,selEnd)）：
//   选区索引空间（paint 时 m_charCounter / TextSegment.charStart）必须与复制源
//   m_plainText（extractBlockText 构建）**逐字节一致**。否则选中可见单元格的词，
//   切片落到 m_plainText 的错误区段 → 复制内容与所见不符（反模式 B）。
//
// 验收：
//   T-COPY-CONSIST-1  全可见表格：每个 TextSegment 满足 plain.mid(charStart,charLen)==text
//   T-COPY-CONSIST-2  视口剪裁（部分块成 placeholder）下仍满足同一不变量

#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QString>
#include <vector>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "FontDefaults.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewCopyConsistencyTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
private:
    QApplication* app_ = nullptr;
};
::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

// 复制源字符流：与 PreviewWidget::extractBlockText 逐字节同款规则。
// （extractBlockText 是 PreviewWidget 私有，这里复制其逻辑作为 oracle；
//  若未来 extractBlockText 改规则，本函数须同步——INV 由两侧共同保证。）
void extractBlockText(const LayoutBlock& block, QString& out)
{
    if (block.type == LayoutBlock::Frontmatter) {
        out += block.frontmatterRawText;
        if (!block.frontmatterRawText.isEmpty()) out += '\n';
        return;
    }
    if (!block.inlineRuns.empty()) {
        for (const auto& run : block.inlineRuns) out += run.text;
        out += '\n';
    }
    if (!block.codeText.isEmpty()) {
        const QStringList lines = block.codeText.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (i == lines.size() - 1 && lines[i].isEmpty()) break;
            out += lines[i];
            out += '\n';
        }
    }
    for (const auto& child : block.children) extractBlockText(child, out);
}

// 在“当前 layout 状态”下校验：每个 TextSegment 的 (charStart,charLen) 切片 == 其 text。
// plainText 用 extractBlockText 在同一 layout 状态构建（模拟 m_plainText = extractPlainText()）。
int checkAtCurrentState(PreviewLayout& layout, qreal H, QString* firstMismatch)
{
    QString plain;
    extractBlockText(layout.rootBlock(), plain);

    PreviewPainter painter;
    Theme theme;
    painter.setTheme(theme);
    painter.setLayout(&layout);

    const int W = 600;
    QImage img(W, (int)H, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    painter.paint(&p, layout.rootBlock(), 0, H, W);
    p.end();

    int mismatches = 0;
    for (const auto& seg : painter.textSegments()) {
        if (seg.charLen <= 0) continue;
        QString slice = plain.mid(seg.charStart, seg.charLen);
        if (slice != seg.text) {
            ++mismatches;
            if (firstMismatch && firstMismatch->isEmpty()) {
                *firstMismatch = QString("charStart=%1 len=%2 expected=[%3] got=[%4]")
                    .arg(seg.charStart).arg(seg.charLen).arg(seg.text).arg(slice);
            }
        }
    }
    return mismatches;
}

// 跑 paint，校验每个 TextSegment 的 (charStart,charLen) 切片 == 其 text
int checkConsistency(const QString& doc, bool clip, QString* firstMismatch)
{
    MarkdownParser parser;
    auto astU = parser.parse(doc);
    if (!astU) return -1;
    std::shared_ptr<AstNode> ast(std::move(astU));

    const int W = 600, H = 400;
    QImage device(W, H, QImage::Format_ARGB32);
    PreviewLayout layout;
    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);
    if (clip) layout.setViewportYRange(0, 120);  // 只让顶部一小段在视口，触发占位块
    layout.buildFromAst(ast);

    return checkAtCurrentState(layout, H, firstMismatch);
}

}  // namespace

// ---------------------------------------------------------------------------
// T-COPY-CONSIST-1：全可见表格，选区索引↔复制源逐字节一致
// ---------------------------------------------------------------------------
TEST(PreviewCopyConsistencyTest, T_COPY_CONSIST_1_TableFullyVisible)
{
    const QString doc = QStringLiteral(
        "# 标题\n\n"
        "段落一些文字 some text\n\n"
        "| 列A | 列B 描述 | 列C |\n"
        "|-----|---------|-----|\n"
        "| 苹果 | apple 红色 | 1 |\n"
        "| 香蕉 | banana 黄 | 22 |\n\n"
        "结尾段落 ending\n");

    QString mm;
    int n = checkConsistency(doc, /*clip=*/false, &mm);
    ASSERT_GE(n, 0) << "解析失败";
    EXPECT_EQ(n, 0) << "选区索引与复制源字符流不一致，首个不匹配：" << mm.toStdString();
}

// ---------------------------------------------------------------------------
// T-COPY-CONSIST-2：视口剪裁（部分块成 placeholder）下仍一致
// ---------------------------------------------------------------------------
TEST(PreviewCopyConsistencyTest, T_COPY_CONSIST_2_ViewportClipped)
{
    QString doc = QStringLiteral("# 顶部标题\n\n");
    for (int i = 0; i < 30; ++i)
        doc += QString("段落 %1 一些较长的中文与 english 混排内容用于撑高文档\n\n").arg(i);
    doc += QStringLiteral(
        "| 列A | 列B | 列C |\n"
        "|-----|-----|-----|\n"
        "| 苹果 | apple | 1 |\n\n");

    QString mm;
    int n = checkConsistency(doc, /*clip=*/true, &mm);
    ASSERT_GE(n, 0) << "解析失败";
    EXPECT_EQ(n, 0) << "剪裁下选区索引与复制源不一致，首个不匹配：" << mm.toStdString();
}

// ---------------------------------------------------------------------------
// T-COPY-CONSIST-3：模拟滚动（重剪裁）后，复制源与选区索引仍一致
// 复现 2026-06-16 用户报告：滚动到表格后复制粘贴为空/错乱——根因是 scrollContentsBy
// 重剪裁 layout（改变 placeholder 集合→索引空间）却不重建 m_plainText。本测试验证：
// 任一剪裁状态下 extractBlockText（= m_plainText）与 paint segments 必须逐字节一致；
// 即只要 widget 在重剪裁后同步重建 m_plainText（已修复），复制就正确。
// ---------------------------------------------------------------------------
TEST(PreviewCopyConsistencyTest, T_COPY_CONSIST_3_ReclipStillConsistent)
{
    QString doc = QStringLiteral("# 顶部标题\n\n");
    for (int i = 0; i < 40; ++i)
        doc += QString("段落 %1 一些较长的中文与 english 混排内容用于撑高文档触发视口剪裁\n\n").arg(i);
    doc += QStringLiteral(
        "| 列A | 列B | 列C |\n"
        "|-----|-----|-----|\n"
        "| 苹果 apple | 香蕉 banana | 1 |\n\n");

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_TRUE(astU);
    std::shared_ptr<AstNode> ast(std::move(astU));

    const int W = 600, H = 400;
    QImage device(W, H, QImage::Format_ARGB32);
    PreviewLayout layout;
    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);

    // 初始剪裁在顶部
    layout.setViewportYRange(0, 240);
    layout.buildFromAst(ast);

    // 模拟向下滚动若干屏后的重剪裁（表格进入视口、顶部段落变 placeholder）
    const qreal total = layout.totalHeight();
    layout.setViewportYRange(total - 480, total);   // 视口移到文档底部（含表格）
    layout.buildFromAst(ast);

    // 重剪裁后：extractBlockText（= 重建的 m_plainText）与 paint segments 必须仍一致
    QString mm;
    int n = checkAtCurrentState(layout, H, &mm);
    ASSERT_GE(n, 0);
    EXPECT_EQ(n, 0) << "重剪裁（滚动）后复制源与选区索引不一致，首个不匹配：" << mm.toStdString();
}

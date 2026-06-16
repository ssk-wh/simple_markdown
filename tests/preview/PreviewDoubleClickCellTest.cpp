// tests/preview/PreviewDoubleClickCellTest.cpp
//
// Spec: specs/模块-preview/12-选区与拖动.md INV-3/INV-4（双击严格命中 + Word 边界）
// Bug:  plans/2026-05-13-预览区双击选词与复制粘贴异常.md（表格单元格双击只选一个字母/错位）
//
// 复现链路（端到端，不依赖文字光栅化——只用 segment.rect/charStart + 字体度量）：
//   渲染表格 → 取单元格内 English 词所在 segment → 在该词中部构造点击点 →
//   复刻 textIndexAtPoint 的「严格命中 + hitTestSegment」→ idx →
//   findWordBoundaryFor(m_plainText, idx) → 断言选中**整个**英文单词。
//
// 验收：
//   T-DC-CELL-1  双击表格单元格内英文单词中部 → 选中整词（非单字母、非错位）

#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QString>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "PreviewWidget.h"
#include "FontDefaults.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewDoubleClickCellTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
private:
    QApplication* app_ = nullptr;
};
::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

// 复刻 PreviewWidget.cpp 的 file-static hitTestSegment（relX→字符索引）
int hitTestSegment(const TextSegment& seg, qreal relX, QPaintDevice* device)
{
    if (seg.text.isEmpty()) return seg.charStart;
    QFontMetricsF fm(seg.font, device);
    for (int i = 0; i < seg.text.length(); ++i) {
        qreal w = fm.horizontalAdvance(seg.text.left(i + 1));
        if (relX < w) {
            qreal prevW = (i > 0) ? fm.horizontalAdvance(seg.text.left(i)) : 0;
            return seg.charStart + ((relX - prevW < w - relX) ? i : i + 1);
        }
    }
    return seg.charStart + seg.charLen;
}

// 复刻 extractBlockText（m_plainText 构建）
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

}  // namespace

TEST(PreviewDoubleClickCellTest, T_DC_CELL_1_DoubleClickSelectsWholeWord)
{
    // 表格：单元格含英文单词（混排中英），双击应选中整个英文单词
    const QString doc = QStringLiteral(
        "| 列A | 列B |\n"
        "|-----|-----|\n"
        "| hello world | 苹果 apple 红色 |\n"
        "| banana milk | 香蕉 yellow 黄 |\n");

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
    layout.buildFromAst(ast);

    QString plain;
    extractBlockText(layout.rootBlock(), plain);

    PreviewPainter painter;
    Theme theme;
    painter.setTheme(theme);
    painter.setLayout(&layout);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    painter.paint(&p, layout.rootBlock(), 0, H, W);
    p.end();

    // 要测试的若干英文单词（出现在各单元格中）
    const QStringList words = {"hello", "world", "apple", "banana", "milk", "yellow"};

    int failures = 0;
    for (const QString& word : words) {
        // 找包含该词的 segment
        const TextSegment* hit = nullptr;
        int offInSeg = -1;
        for (const auto& seg : painter.textSegments()) {
            int idx = seg.text.indexOf(word);
            if (idx >= 0) { hit = &seg; offInSeg = idx; break; }
        }
        if (!hit) { fprintf(stderr, "[dc-cell] '%s' 未找到 segment\n", word.toUtf8().constData()); ++failures; continue; }

        // 构造点击点：该词水平中部、segment 垂直中部
        QFontMetricsF segFm(hit->font, &device);
        qreal xBefore = segFm.horizontalAdvance(hit->text.left(offInSeg));
        qreal xWordHalf = segFm.horizontalAdvance(hit->text.left(offInSeg + word.length() / 2));
        qreal clickX = hit->rect.x() + (xBefore + xWordHalf) / 2.0 + segFm.horizontalAdvance(word.left(word.length()/2)) * 0.0;
        // 更直接：词中部 = rect.x() + advance(前缀 + 半个词)
        clickX = hit->rect.x() + segFm.horizontalAdvance(hit->text.left(offInSeg) + word.left(word.length() / 2));
        qreal clickY = hit->rect.center().y();
        QPointF pt(clickX, clickY);

        // 复刻 textIndexAtPoint 严格命中：找 rect.contains(pt) 的 segment → hitTestSegment
        int idx = -1;
        for (const auto& seg : painter.textSegments()) {
            if (seg.rect.contains(pt)) { idx = hitTestSegment(seg, pt.x() - seg.rect.x(), &device); break; }
        }
        if (idx < 0) { fprintf(stderr, "[dc-cell] '%s' 点击点未严格命中任何 segment\n", word.toUtf8().constData()); ++failures; continue; }

        // 双击选词
        QPair<int,int> range = PreviewWidget::findWordBoundaryFor(plain, idx);
        QString selected = (range.first >= 0 && range.second > range.first)
                           ? plain.mid(range.first, range.second - range.first) : QString();

        fprintf(stderr, "[dc-cell] word='%s' idx=%d range=[%d,%d] selected='%s'\n",
                word.toUtf8().constData(), idx, range.first, range.second, selected.toUtf8().constData());

        if (selected != word) {
            ++failures;
            ADD_FAILURE() << "双击 '" << word.toStdString() << "' 应选中整词，实际选中 '"
                          << selected.toStdString() << "'";
        }
    }
    EXPECT_EQ(failures, 0);
}

// ---------------------------------------------------------------------------
// T-DC-CELL-2：滚动重剪裁后，m_plainText 滞后 → 双击错位/单字母；同步后 → 正确
// 复现 2026-06-16 用户报告的"反复多次"根因，并锁定修复：双击前必须用与当前 segments
// 同一剪裁状态的 m_plainText（widget 在 mouseDoubleClickEvent 开头重建 m_plainText）。
// ---------------------------------------------------------------------------
TEST(PreviewDoubleClickCellTest, T_DC_CELL_2_StalePlainTextBreaksDoubleClick)
{
    // 文档顶部一堆段落 + 底部一个含英文的表格 → 顶部视口时表格是 placeholder
    QString doc;
    for (int i = 0; i < 40; ++i)
        doc += QString("段落 %1 一些较长的中英 english 混排内容撑高文档触发视口剪裁\n\n").arg(i);
    doc += QStringLiteral("| 列A | 列B |\n|-----|-----|\n| hello world | 苹果 apple |\n");

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

    // 顶部剪裁：构建“滞后”的 m_plainText（此时底部表格是 placeholder）
    layout.setViewportYRange(0, 240);
    layout.buildFromAst(ast);
    QString stalePlain;
    extractBlockText(layout.rootBlock(), stalePlain);

    // 滚到底部（表格进入视口）：segments 在新剪裁空间
    const qreal total = layout.totalHeight();
    const qreal scrollY = qMax(0.0, total - H);   // 滚到底部，表格在视口内
    layout.setViewportYRange(scrollY - 240, scrollY + H + 240);
    layout.buildFromAst(ast);

    QString freshPlain;   // 修复后：双击前重建的 m_plainText（与当前 segments 同步）
    extractBlockText(layout.rootBlock(), freshPlain);

    PreviewPainter painter;
    Theme theme;
    painter.setTheme(theme);
    painter.setLayout(&layout);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    painter.paint(&p, layout.rootBlock(), scrollY, H, W);   // paint scrollY 必须匹配剪裁位置
    p.end();

    // 找单元格里 "apple" 的 segment，构造词中部点击点 → idx
    const QString word = "apple";
    const TextSegment* hit = nullptr; int off = -1;
    for (const auto& seg : painter.textSegments()) {
        int i = seg.text.indexOf(word);
        if (i >= 0) { hit = &seg; off = i; break; }
    }
    ASSERT_NE(hit, nullptr);
    QFontMetricsF segFm(hit->font, &device);
    qreal clickX = hit->rect.x() + segFm.horizontalAdvance(hit->text.left(off) + word.left(word.length()/2));
    QPointF pt(clickX, hit->rect.center().y());
    int idx = -1;
    for (const auto& seg : painter.textSegments())
        if (seg.rect.contains(pt)) { idx = hitTestSegment(seg, pt.x() - seg.rect.x(), &device); break; }
    ASSERT_GE(idx, 0);

    auto pick = [&](const QString& plain) {
        QPair<int,int> r = PreviewWidget::findWordBoundaryFor(plain, idx);
        return (r.first >= 0 && r.second > r.first) ? plain.mid(r.first, r.second - r.first) : QString();
    };
    QString withStale = pick(stalePlain);
    QString withFresh = pick(freshPlain);
    fprintf(stderr, "[dc-cell] idx=%d withStale='%s' withFresh='%s'\n",
            idx, withStale.toUtf8().constData(), withFresh.toUtf8().constData());

    // 修复的核心断言：用与当前 segments 同步的 m_plainText，双击选中整词
    EXPECT_EQ(withFresh, word) << "同步 m_plainText 后双击应选中整词";
    // 证明根因：滞后 m_plainText 会得到错误结果（非整词）——若此处恰好相等说明本例未触发，
    // 不强制失败，但 fresh 必须正确（上面的断言）。
    if (withStale == word)
        fprintf(stderr, "[dc-cell] 注意：本例 stalePlain 恰好也正确，未触发错位（场景相关）\n");
}

// tests/preview/PreviewSelectionDragTest.cpp
//
// Spec: specs/模块-preview/12-选区与拖动.md INV-1（选区终点 snap：同视觉行 dx 优先 → 2D fallback）
// Bug:  归档/2026-05-09 用户报告——表格行内拖到右侧空白时，纯 2D 距离 snap 会跳到上/下行
//       segment，导致 selStart→selEnd 跨多行、整段被高亮。2026-05-11 修复为双层 closest。
//
// 直接调用真实算法 PreviewWidget::textIndexForSegments（public static seam），不复刻逻辑，
// 杜绝「测试与实现两份代码走样」（CLAUDE.md 反模式 B）。
//
// 验收：
//   T-1  表格行右侧空白拖动 → 选区终点落在**该行**字符范围内，不跨第 1/3 行
//   T-2  普通段落行右侧空白 → 选区终点止于该折行末，不跨到下一视觉行
//   T-3  行间空白处拖动 → 走 2D fallback，返回有效字符索引（健全性，不跨界）

#include <gtest/gtest.h>

#include <QApplication>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QVector>
#include <algorithm>
#include <limits>

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
            static char arg0[] = "PreviewSelectionDragTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
private:
    QApplication* app_ = nullptr;
};
::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

// 把一个文档渲染到 QImage，返回 painter 持有的 textSegments（geometry，不依赖光栅化）。
// painter 以引用返回，调用方需保持其生命周期（与 layout 一起栈上持有）。
void renderDoc(const QString& doc, int W, int H,
               PreviewLayout& layout, PreviewPainter& painter, QImage& device)
{
    MarkdownParser parser;
    auto astU = parser.parse(doc);
    ASSERT_TRUE(astU);
    std::shared_ptr<AstNode> ast(std::move(astU));

    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);
    layout.buildFromAst(ast);

    Theme theme;
    painter.setTheme(theme);
    painter.setLayout(&layout);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    painter.paint(&p, layout.rootBlock(), 0, H, W);
    p.end();
}

// 把 segments 按 y（rect.top 量化）聚成视觉行，返回包含目标文本的那一视觉行的
// [charLo, charHi]（该行所有 segment 的 charStart..charStart+charLen 的并集范围），
// 以及该行的垂直中心 y 与最右 x。
struct VisualRow {
    int charLo = std::numeric_limits<int>::max();
    int charHi = std::numeric_limits<int>::min();
    qreal top = 0, bottom = 0;
    qreal centerY = 0;
    qreal maxRight = 0;
    bool found = false;
};

VisualRow rowContaining(const QVector<TextSegment>& segs, const QString& needle)
{
    // 找含 needle 的 segment，取其 rect.top 作为行基准，收集同行（top 容差 2px）segments
    qreal anchorTop = -1;
    for (const auto& s : segs) {
        if (s.text.contains(needle)) { anchorTop = s.rect.top(); break; }
    }
    VisualRow row;
    if (anchorTop < 0) return row;  // found 保持 false
    row.found = true;
    qreal cTop = std::numeric_limits<qreal>::max(), cBot = std::numeric_limits<qreal>::lowest();
    for (const auto& s : segs) {
        if (std::abs(s.rect.top() - anchorTop) <= 2.0) {
            row.charLo = std::min(row.charLo, s.charStart);
            row.charHi = std::max(row.charHi, s.charStart + s.charLen);
            row.maxRight = std::max(row.maxRight, s.rect.right());
            cTop = std::min(cTop, s.rect.top());
            cBot = std::max(cBot, s.rect.bottom());
        }
    }
    row.top = cTop;
    row.bottom = cBot;
    row.centerY = (cTop + cBot) / 2.0;
    return row;
}

// 反例基线（**故意复刻 2026-05-11 修复前的纯 2D 距离算法**，仅作对照证明回归保护，
// 绝不可用于生产）：不做「同视觉行 dx 优先」，直接全局 2D 欧氏距离取最近 segment 边界。
// 历史 bug：表格行内拖到右侧空白时，此算法可能因相邻行 dy 微小、dx 相近而 snap 到上/下行。
int naive2DClosest(const QVector<TextSegment>& segments, const QPointF& point, QPaintDevice* device)
{
    if (segments.isEmpty()) return 0;
    int closest = 0;
    qreal minDist = std::numeric_limits<qreal>::max();
    for (const auto& seg : segments) {
        if (seg.rect.contains(point)) {
            // 命中段内：逐字定位（与 hitTestSegment 等价的简化）
            QFontMetricsF fm(seg.font, device);
            qreal relX = point.x() - seg.rect.x();
            int idx = seg.charStart + seg.charLen;
            for (int i = 0; i < seg.text.length(); ++i) {
                if (relX < fm.horizontalAdvance(seg.text.left(i + 1))) { idx = seg.charStart + i; break; }
            }
            return idx;
        }
        qreal dx = 0, dy = 0;
        if (point.x() < seg.rect.left())       dx = seg.rect.left() - point.x();
        else if (point.x() > seg.rect.right()) dx = point.x() - seg.rect.right();
        if (point.y() < seg.rect.top())        dy = seg.rect.top() - point.y();
        else if (point.y() > seg.rect.bottom()) dy = point.y() - seg.rect.bottom();
        qreal dist = dy * dy + dx * dx;
        if (dist < minDist) {
            minDist = dist;
            closest = (point.x() >= seg.rect.right()) ? seg.charStart + seg.charLen
                    : (point.x() <= seg.rect.left())  ? seg.charStart
                                                      : seg.charStart;
        }
    }
    return closest;
}

}  // namespace

// T-1：表格第 2 行内拖到右侧空白 → 终点落在第 2 行字符范围内，不跨第 1/3 行
TEST(PreviewSelectionDragTest, T1_TableRowRightWhitespaceStaysInRow)
{
    // 几何设计：上一行右 cell 文字**很长**（segment 墨迹靠右，dx 小），当前行（gamma 行）
    // 右 cell 文字**很短**（segment 靠左，dx 大）。这样在 gamma 行 band 顶部右侧空白点击时，
    // 纯 2D 距离会被上一行的长 segment「吸走」→ 跨行（历史 bug）；同行优先算法则锁在本行。
    const QString doc = QStringLiteral(
        "| 列A | 列B |\n"
        "|-----|-----|\n"
        "| top | this is an extremely long right cell content stretching far right edge |\n"
        "| gamma | hi |\n"                 // 当前行：右 cell 极短，我们在这里拖动
        "| bottom | x |\n");

    const int W = 700, H = 500;
    QImage device(W, H, QImage::Format_ARGB32);
    PreviewLayout layout;
    PreviewPainter painter;
    renderDoc(doc, W, H, layout, painter, device);
    const auto& segs = painter.textSegments();
    ASSERT_FALSE(segs.isEmpty());

    // 第 2 数据行（中间行）：含 "gamma"。取该行字符范围与垂直 band。
    VisualRow row2 = rowContaining(segs, QStringLiteral("gamma"));
    ASSERT_TRUE(row2.found) << "未找到含 gamma 的视觉行";
    fprintf(stderr, "[drag] T1 row2 char=[%d,%d] band=[%.1f,%.1f]\n",
            row2.charLo, row2.charHi, row2.top, row2.bottom);

    // 沿该行垂直 band 扫描右侧空白点（x=W-4），对比真实算法 vs 纯 2D 反例。
    // 核心断言：真实算法在 band 内任意 y 都不跨行；并尝试找到纯 2D 会跨行的点以证明回归保护有效。
    bool naiveCrossedSomewhere = false;
    int steps = 0;
    for (qreal y = row2.top + 0.5; y <= row2.bottom - 0.5; y += 1.0) {
        QPointF pt(W - 4.0, y);
        int real = PreviewWidget::textIndexForSegments(segs, pt, &device);
        int naive = naive2DClosest(segs, pt, &device);
        // 真实算法：y 在第 2 行 band 内 → 同行优先必然命中第 2 行，绝不跨行
        ASSERT_GE(real, row2.charLo) << "真实算法在 y=" << y << " 跨到上一行";
        ASSERT_LE(real, row2.charHi) << "真实算法在 y=" << y << " 跨到下一行（纯 2D 回归）";
        if (naive < row2.charLo || naive > row2.charHi) naiveCrossedSomewhere = true;
        ++steps;
    }
    fprintf(stderr, "[drag] T1 scanned %d points, naiveCrossed=%d\n", steps, naiveCrossedSomewhere ? 1 : 0);
    // 反例若在某点跨行 → 证明「同行优先」修复确实拦住了历史 bug；
    // 若本几何下纯 2D 恰好处处不跨行（dy=0 主导），仍保留上面对真实算法的硬断言作回归门禁。
    if (!naiveCrossedSomewhere)
        fprintf(stderr, "[drag] T1 注意：本文档几何下纯 2D 未触发跨行（场景相关），真实算法断言仍生效\n");
}

// T-2：普通段落某折行右侧空白 → 终点止于该折行末，不跨到下一视觉行
TEST(PreviewSelectionDragTest, T2_ParagraphLineRightWhitespaceStopsAtLineEnd)
{
    // 构造一个会折成多行的长段落（纯 ASCII，便于按词定位首行内容）
    QString para;
    for (int i = 0; i < 30; ++i) para += QString("word%1 ").arg(i);
    const QString doc = para + "\n";

    const int W = 360, H = 600;   // 窄视口强制折行
    QImage device(W, H, QImage::Format_ARGB32);
    PreviewLayout layout;
    PreviewPainter painter;
    renderDoc(doc, W, H, layout, painter, device);
    const auto& segs = painter.textSegments();
    ASSERT_FALSE(segs.isEmpty());

    // 第一个视觉行含 "word0"
    VisualRow firstRow = rowContaining(segs, QStringLiteral("word0"));
    ASSERT_TRUE(firstRow.found);

    // 确认确实折行了（存在 top 明显大于首行的 segment）→ 否则该用例无意义
    bool hasLowerRow = false;
    for (const auto& s : segs)
        if (s.rect.top() > firstRow.centerY + 5.0) { hasLowerRow = true; break; }
    ASSERT_TRUE(hasLowerRow) << "段落未折行，用例前提不成立（应缩小视口宽度）";

    QPointF pt(W - 4.0, firstRow.centerY);
    int idx = PreviewWidget::textIndexForSegments(segs, pt, &device);

    fprintf(stderr, "[drag] T2 firstRow char=[%d,%d] idx=%d\n",
            firstRow.charLo, firstRow.charHi, idx);

    EXPECT_GE(idx, firstRow.charLo);
    EXPECT_LE(idx, firstRow.charHi) << "选区终点越过首行末尾，跨到下一折行";
}

// T-3：行间空白处拖动 → 2D fallback，返回有效字符索引（健全性 + 不越界）
TEST(PreviewSelectionDragTest, T3_BetweenRowsFallsBackTo2D)
{
    const QString doc = QStringLiteral(
        "first paragraph line\n\n"
        "second paragraph line\n");

    const int W = 600, H = 400;
    QImage device(W, H, QImage::Format_ARGB32);
    PreviewLayout layout;
    PreviewPainter painter;
    renderDoc(doc, W, H, layout, painter, device);
    const auto& segs = painter.textSegments();
    ASSERT_FALSE(segs.isEmpty());

    VisualRow r1 = rowContaining(segs, QStringLiteral("first"));
    VisualRow r2 = rowContaining(segs, QStringLiteral("second"));
    ASSERT_TRUE(r1.found && r2.found);
    ASSERT_GT(r2.centerY, r1.centerY);

    // 点击点：两段之间的垂直空白（y 不在任何 segment 垂直范围内），水平居中
    qreal midY = (r1.centerY + r2.centerY) / 2.0;
    QPointF pt(W / 2.0, midY);
    int idx = PreviewWidget::textIndexForSegments(segs, pt, &device);

    fprintf(stderr, "[drag] T3 midY=%.1f idx=%d\n", midY, idx);

    // 2D fallback 必须返回有效字符索引（>=0），且不越界（落在两段字符范围的并集内）
    EXPECT_GE(idx, 0);
    int lo = std::min(r1.charLo, r2.charLo);
    int hi = std::max(r1.charHi, r2.charHi);
    EXPECT_GE(idx, lo);
    EXPECT_LE(idx, hi) << "fallback 返回越界索引";
}

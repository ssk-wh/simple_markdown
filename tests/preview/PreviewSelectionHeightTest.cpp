// tests/preview/PreviewSelectionHeightTest.cpp
//
// Spec: specs/模块-preview/12-选区与拖动.md  INV-SEL-HEIGHT（选区矩形高度=行内容高度，与文本绘制同源）
// Bug:  plans/2026-06-15-渲染区英文单词选区高度只覆盖上半.md
//
// 验收：
//   T-SEL-HEIGHT-1  选中单词时，选区高亮矩形的垂直范围必须完整覆盖该单词的字形墨迹
//                   （纯英文 / 中英混排 / 加粗后接英文 等场景均不得只盖上半或下半）
//
// 复现方法（离屏光栅化，不弹窗）：
//   - 用真实默认预览字体 Segoe UI 12 构造 PreviewLayout，buildFromAst
//   - 渲染两次到白底 QImage：一次无选区(baseline)，一次选中目标单词
//   - baseline 图在单词 x 区间内扫"非白像素" → 字形墨迹的 [inkTop, inkBottom]
//   - diff(baseline, selection) 在单词 x 区间内 → 选区矩形的 [selTop, selBottom]
//   - 断言 selTop <= inkTop 且 selBottom >= inkBottom（选区覆盖墨迹）

#include <gtest/gtest.h>

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <cstdio>
#include <iostream>

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
            static char arg0[] = "PreviewSelectionHeightTest";
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

struct Measure {
    bool found = false;
    int selTop = -1, selBottom = -1;
    int inkTop = -1, inkBottom = -1;
    int x0 = 0, x1 = 0;
};

bool isInk(QRgb c)
{
    return qRed(c) < 240 || qGreen(c) < 240 || qBlue(c) < 240;
}

Measure measureWord(const QString& doc, const QString& word)
{
    Measure m;

    MarkdownParser parser;
    auto astU = parser.parse(doc);
    if (!astU) return m;
    std::shared_ptr<AstNode> ast(std::move(astU));

    const int W = 600, H = 300;
    QImage device(W, H, QImage::Format_ARGB32);

    PreviewLayout layout;
    layout.setFont(font_defaults::defaultPreviewFont());
    layout.updateMetrics(&device);
    layout.setViewportWidth(W);
    layout.buildFromAst(ast);

    PreviewPainter painter;
    Theme theme;
    painter.setTheme(theme);
    painter.setLayout(&layout);

    // baseline 渲染（无选区）
    QImage img1(W, H, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    {
        QPainter p(&img1);
        painter.setSelection(-1, -1);
        painter.paint(&p, layout.rootBlock(), 0, H, W);
    }
    // 在 textSegments 里找包含目标单词的段
    int charStart = -1;
    QRectF segRect;
    QFont segFont;
    QString segText;
    int off = -1;
    for (const auto& seg : painter.textSegments()) {
        int idx = seg.text.indexOf(word);
        if (idx >= 0) {
            off = idx;
            charStart = seg.charStart + idx;
            segRect = seg.rect;
            segFont = seg.font;
            segText = seg.text;
            break;
        }
    }
    if (charStart < 0) return m;
    int charEnd = charStart + word.length();
    m.found = true;

    QFontMetricsF segFm(segFont, &device);
    qreal xa = segRect.x() + segFm.horizontalAdvance(segText.left(off));
    qreal xb = segRect.x() + segFm.horizontalAdvance(segText.left(off + word.length()));
    m.x0 = std::max(0, (int)std::floor(xa));
    m.x1 = std::min(W, (int)std::ceil(xb));

    // selection 渲染（选中目标单词）
    QImage img2(W, H, QImage::Format_ARGB32);
    img2.fill(Qt::white);
    {
        QPainter p(&img2);
        painter.setSelection(charStart, charEnd);
        painter.paint(&p, layout.rootBlock(), 0, H, W);
    }

    // 字形墨迹垂直范围（baseline 图，单词 x 区间内非白像素）
    for (int y = 0; y < H; ++y) {
        for (int x = m.x0; x < m.x1; ++x) {
            if (isInk(img1.pixel(x, y))) {
                if (m.inkTop < 0) m.inkTop = y;
                m.inkBottom = y;
                break;
            }
        }
    }

    // 选区矩形垂直范围（diff，单词 x 区间内有变化的像素）
    for (int y = 0; y < H; ++y) {
        bool diff = false;
        for (int x = m.x0; x < m.x1; ++x) {
            if (img1.pixel(x, y) != img2.pixel(x, y)) { diff = true; break; }
        }
        if (diff) {
            if (m.selTop < 0) m.selTop = y;
            m.selBottom = y;
        }
    }
    return m;
}

void report(const char* name, const Measure& m)
{
    if (!m.found) {
        fprintf(stderr, "[%s] WORD NOT FOUND in segments\n", name);
        return;
    }
    fprintf(stderr,
            "[%s] x=[%d,%d) ink=[%d,%d] (h=%d)  sel=[%d,%d] (h=%d)  "
            "topGap(sel-ink)=%d  bottomGap(sel-ink)=%d\n",
            name, m.x0, m.x1, m.inkTop, m.inkBottom, m.inkBottom - m.inkTop + 1,
            m.selTop, m.selBottom, m.selBottom - m.selTop + 1,
            m.selTop - m.inkTop, m.selBottom - m.inkBottom);
}

}  // namespace

// ---------------------------------------------------------------------------
// T-SEL-HEIGHT-1：选区矩形必须完整覆盖单词字形墨迹（多场景）
// ---------------------------------------------------------------------------
TEST(PreviewSelectionHeightTest, T_SEL_HEIGHT_1_SelectionCoversGlyph)
{
    struct Case { const char* name; QString doc; QString word; };
    std::vector<Case> cases = {
        {"plain-english", "Hello world this is plain english text", "Hello"},
        {"cjk-mixed",     QStringLiteral("中文混排Hello世界文字内容"), "Hello"},
        {"bold-then-en",  QStringLiteral("这是**加粗中文**然后Hello英文"), "Hello"},
        {"code-then-en",  QStringLiteral("`code` Hello after inline code"), "Hello"},
        {"en-then-code",  QStringLiteral("Hello `code` world"), "Hello"},
    };

    int failed = 0;
    for (const auto& c : cases) {
        Measure m = measureWord(c.doc, c.word);
        report(c.name, m);  // 失败时打印 ink/sel 范围，便于诊断
        if (!m.found) { ++failed; continue; }
        // 选区顶部不得低于墨迹顶部，底部不得高于墨迹底部（允许 1px 抗锯齿容差）
        EXPECT_LE(m.selTop, m.inkTop + 1) << c.name << ": 选区顶部盖不住字形上沿";
        EXPECT_GE(m.selBottom, m.inkBottom - 1) << c.name << ": 选区底部盖不住字形下沿";
    }
    ASSERT_EQ(failed, 0) << "部分场景未在 segments 中找到目标单词";
}

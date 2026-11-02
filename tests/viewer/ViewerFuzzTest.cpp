// tests/viewer/ViewerFuzzTest.cpp
//
// Spec: specs/模块-viewer/04-查看器控件.md（INV-VIEW-*、INV-CHUNK-CONSISTENT）
// 用途：随机文档 × 随机操作序列的稳定性/一致性模糊测试（固定种子，可复现）。
//   - 每个随机文档：分块渲染文本必须与整篇渲染文本逐字一致（任意 targetLines）
//   - 随机操作（滚动/跳转/缩放窗口/查找/选区复制/标记/重载/换主题）后：不崩溃，滚动模型不变量成立
// 环境变量 VIEWER_FUZZ_ITERS 可调迭代数（F1 默认 40，F2 默认 10）；VIEWER_FUZZ_SEED 指定起始种子。
#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <memory>
#include <random>

#include "MarkdownAst.h"
#include "MarkdownParser.h"
#include "PreviewLayout.h"
#include "PreviewPainter.h"
#include "ViewerWidget.h"

using viewer::ScanOptions;
using viewer::ViewerWidget;

namespace {

class AppEnv : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "ViewerFuzzTest";
            static char* argv[] = {arg0, nullptr};
            new QApplication(argc, argv);
        }
    }
};
::testing::Environment* const g_env = ::testing::AddGlobalTestEnvironment(new AppEnv);

struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned seed) : g(seed) {}
    int in(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g); }
    bool chance(int pct) { return in(0, 99) < pct; }
    template <typename T> const T& pick(const std::vector<T>& v) { return v[static_cast<size_t>(in(0, static_cast<int>(v.size()) - 1))]; }
};

QString randomText(Rng& r, int words)
{
    static const std::vector<QString> w = {
        "alpha", "beta", "gamma", "性能", "目录", "渲染", "emoji😀", "é", "RTLאב", "foo_bar", "**bold**",
        "*it*", "`code`", "[link](http://x.y/z)", "~~del~~", "<b>x</b>", "&amp;", "snake_case_word", "1.5", "—", "…",
    };
    QString out;
    for (int i = 0; i < words; ++i) out += r.pick(w) + ' ';
    return out.trimmed();
}

// 随机 Markdown：覆盖各类块结构与「切块陷阱」
QByteArray randomDoc(Rng& r, bool crlf)
{
    QString d;
    if (r.chance(15)) d += "---\ntitle: T\ntags: [a, b]\n---\n\n";
    const int blocks = r.in(5, 80);
    for (int i = 0; i < blocks; ++i) {
        switch (r.in(0, 13)) {
        case 0: d += QString(r.in(1, 6), '#') + ' ' + randomText(r, r.in(1, 5)) + "\n\n"; break;
        case 1: case 2: d += randomText(r, r.in(3, 60)) + "\n\n"; break;
        case 3: {
            const int n = r.in(1, 6);
            const bool loose = r.chance(40);
            for (int k = 0; k < n; ++k) {
                d += (r.chance(30) ? QString::number(k + 1) + ". " : QString("- ")) + randomText(r, 4) + "\n";
                if (r.chance(30)) d += "  - " + randomText(r, 3) + "\n";
                if (loose) d += "\n";
            }
            d += "\n";
            break;
        }
        case 4: {
            const QString fence = r.chance(50) ? "```" : "~~~";
            d += fence + (r.chance(60) ? "cpp" : "") + "\n";
            const int n = r.in(0, 25);
            for (int k = 0; k < n; ++k) d += (r.chance(15) ? "" : (r.chance(10) ? "# not heading " : "int x" + QString::number(k) + " = 0;")) + "\n";
            d += fence + "\n\n";
            break;
        }
        case 5: {
            d += "| a | b | c |\n|---|:-:|--:|\n";
            const int n = r.in(1, 8);
            for (int k = 0; k < n; ++k) d += "| " + randomText(r, 1) + " | " + randomText(r, 2) + " | " + QString::number(k) + " |\n";
            d += "\n";
            break;
        }
        case 6: d += "> " + randomText(r, 8) + "\n" + (r.chance(40) ? "continued lazy\n" : "") + "\n"; break;
        case 7: d += "<div>\n\n" + randomText(r, 4) + "\n\n</div>\n\n"; break;
        case 8: d += "<!--\n" + randomText(r, 3) + "\n\n# hidden\n-->\n\n"; break;
        case 9: d += randomText(r, 3) + "\n" + (r.chance(50) ? "=====" : "-----") + "\n\n"; break;
        case 10: d += "    " + randomText(r, 3) + "\n\n    " + randomText(r, 3) + "\n\n"; break;
        case 11: d += "see [ref" + QString::number(i % 4) + "] and ![img](missing/" + QString::number(i) + ".png)\n\n"; break;
        case 12: d += "---\n\n"; break;
        default: d += (r.chance(30) ? "[ref" + QString::number(i % 4) + "]: http://example.com/" + QString::number(i) + "\n\n" : "\n"); break;
        }
    }
    QByteArray b = d.toUtf8();
    if (crlf) b.replace("\n", "\r\n");
    return b;
}

QString writeTmp(QTemporaryDir& dir, const QString& name, const QByteArray& data)
{
    const QString p = dir.filePath(name);
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    return p;
}

void waitLoaded(ViewerWidget& v)
{
    QElapsedTimer t;
    t.start();
    while (v.isLoading() && t.elapsed() < 15000) QApplication::processEvents(QEventLoop::AllEvents, 5);
    v.prepareForTest();
    v.repaint();
}

QString wholeDocText(const QString& markdown, int width)
{
    MarkdownParser parser;
    std::shared_ptr<AstNode> ast(parser.parse(markdown).release());
    PreviewLayout layout;
    layout.setViewportWidth(width);
    layout.buildFromAst(ast);
    QString out;
    PreviewPainter::extractBlockText(layout.rootBlock(), out);
    return out;
}

int iterations(int def = 40)
{
    const int v = qEnvironmentVariableIntValue("VIEWER_FUZZ_ITERS");
    return v > 0 ? v : def;
}
unsigned baseSeed()
{
    const int v = qEnvironmentVariableIntValue("VIEWER_FUZZ_SEED");
    return v > 0 ? static_cast<unsigned>(v) : 1000u;
}

}  // namespace

// 随机文档：分块文本 == 整篇文本
TEST(ViewerFuzzTest, F1_ChunkedTextEqualsWholeOnRandomDocs)
{
    QTemporaryDir dir;
    for (int it = 0; it < iterations(); ++it) {
        const unsigned seed = baseSeed() + static_cast<unsigned>(it);
        Rng r(seed);
        const QByteArray doc = randomDoc(r, /*crlf=*/r.chance(20));
        ViewerWidget v;
        v.resize(900, 600);
        ScanOptions o;
        o.targetLines = r.pick<int>({1, 2, 3, 5, 8, 40});
        v.setScanOptionsForTest(o);
        v.loadFile(writeTmp(dir, "f.md", doc));
        waitLoaded(v);
        ASSERT_TRUE(v.errorString().isEmpty()) << "seed=" << seed;
        // 整篇布局以 PreviewLayout 默认字体；ViewerWidget 同样使用 defaultPreviewFont，宽度一致
        const QString chunked = v.allChunkTextForTest();
        const QString whole = wholeDocText(QString::fromUtf8(doc), 860);
        if (chunked != whole) {
            // 失败现场落盘，便于复现分析（tmp/ 已 gitignore）
            QFile f1(QString("fuzz_fail_%1.md").arg(seed)); f1.open(QIODevice::WriteOnly); f1.write(doc);
            QFile f2(QString("fuzz_fail_%1.chunked.txt").arg(seed)); f2.open(QIODevice::WriteOnly); f2.write(chunked.toUtf8());
            QFile f3(QString("fuzz_fail_%1.whole.txt").arg(seed)); f3.open(QIODevice::WriteOnly); f3.write(whole.toUtf8());
        }
        ASSERT_EQ(chunked, whole) << "seed=" << seed << " targetLines=" << o.targetLines;
    }
}

// 随机操作序列：不崩溃 + 滚动模型不变量
TEST(ViewerFuzzTest, F2_RandomOperationSequencesKeepInvariants)
{
    QTemporaryDir dir;
    for (int it = 0; it < iterations(10); ++it) {   // 日常 10 次（约 8 秒）；夜间可用 VIEWER_FUZZ_ITERS=200 加大
        const unsigned seed = baseSeed() + 5000 + static_cast<unsigned>(it);
        Rng r(seed);
        // 大一点的文档，保证有足够多 chunk
        QByteArray doc;
        for (int k = 0; k < r.in(1, 6); ++k) doc += randomDoc(r, false);
        ViewerWidget v;
        v.resize(r.in(500, 1200), r.in(300, 800));
        v.show();
        ScanOptions o;
        o.targetLines = r.pick<int>({2, 5, 10, 40});
        v.setScanOptionsForTest(o);
        const QString path = writeTmp(dir, "g.md", doc);
        v.loadFile(path);
        waitLoaded(v);
        ASSERT_TRUE(v.errorString().isEmpty()) << "seed=" << seed;

        const int ops = r.in(20, 60);
        for (int k = 0; k < ops; ++k) {
            const int op = r.in(0, 14);
            switch (op) {
            case 0: v.scrollToSourceLine(r.in(0, static_cast<int>(v.lineCount()) + 5), r.chance(50)); break;
            case 1: v.scrollToBottom(); break;
            case 2: v.scrollToTop(); break;
            case 3: case 4: {
                const int dy = r.in(-3000, 3000);
                QWheelEvent we(QPointF(100, 100), QPointF(100, 100), QPoint(0, dy), QPoint(0, dy),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(v.viewport(), &we);
                break;
            }
            case 5: v.resize(r.in(300, 1400), r.in(200, 900)); break;
            case 6: v.searchForTest(r.pick<QString>({"alpha", "性能", "int x", "zzzz", "e", "目录"}), r.chance(30)); break;
            case 7: v.searchNextForTest(); break;
            case 8: v.selectAll(); v.copySelection(); break;
            case 9:
                if (v.scrollModel().count() > 0) {
                    const int c = r.in(0, static_cast<int>(v.scrollModel().count()) - 1);
                    v.selectChunkRangeForTest(c, 0, r.in(1, 30));
                    v.addMarkFromSelection();
                }
                break;
            case 10: v.clearAllMarks(); break;
            case 11: {
                const QByteArray m = v.serializeMarkings();
                v.reload();
                waitLoaded(v);
                v.deserializeMarkings(m);
                break;
            }
            case 12: v.setTheme(r.chance(50) ? Theme::dark() : Theme::light()); break;
            case 13: v.setLineSpacingFactor(r.pick<double>({1.0, 1.5, 2.0})); v.rebuildLayout(); break;
            default: v.repaint(); break;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 2);
            v.prepareForTest();
            v.repaint();

            // ---- 不变量 ----
            const auto& m = v.scrollModel();
            if (m.count() > 0) {
                ASSERT_LT(m.anchorChunk(), m.count()) << "seed=" << seed << " op#" << k << " op=" << op;
                ASSERT_GE(m.anchorOffset(), 0.0) << "seed=" << seed;
                ASSERT_GE(m.topY(), -0.001) << "seed=" << seed;
                ASSERT_LE(m.topY(), m.maxTopY() + 1.0) << "seed=" << seed << " op=" << op;
                ASSERT_LE(v.cachedChunkCount(), 220u) << "seed=" << seed;
            }
        }
    }
}

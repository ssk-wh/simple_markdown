// tests/viewer/ViewerWidgetTest.cpp
//
// Spec: specs/模块-viewer/04-查看器控件.md、specs/模块-viewer/05-查找.md
// 验收：T1 ~ T14
#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <memory>

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
            static char arg0[] = "ViewerWidgetTest";
            static char* argv[] = {arg0, nullptr};
            new QApplication(argc, argv);
        }
    }
};
::testing::Environment* const g_env = ::testing::AddGlobalTestEnvironment(new AppEnv);

QString writeTmp(QTemporaryDir& dir, const QString& name, const QByteArray& data)
{
    const QString p = dir.filePath(name);
    QFile f(p);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
    return p;
}

// 加载并等待完成 + 首次布局
void loadAndWait(ViewerWidget& v, const QString& path, int timeoutMs = 15000)
{
    v.loadFile(path);
    QElapsedTimer t;
    t.start();
    while (v.isLoading() && t.elapsed() < timeoutMs) QApplication::processEvents(QEventLoop::AllEvents, 10);
    v.prepareForTest();
    v.repaint();
}

QByteArray makeDoc(int sections)
{
    QByteArray t = "# Title\n\n";
    for (int i = 0; i < sections; ++i) {
        t += "## Section " + QByteArray::number(i) + "\n\n";
        t += "Paragraph for section " + QByteArray::number(i) + " with **bold** and `code`.\n\n";
        t += "- item a\n- item b\n\n```cpp\nint x = " + QByteArray::number(i) + ";\n```\n\n";
    }
    return t;
}

// 整篇渲染文本（旧管线：整篇 AST + 整篇布局 + 同一套抽取规则）
QString wholeDocText(const QString& markdown, int width = 900)
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

}  // namespace

TEST(ViewerWidgetTest, T1_LoadProducesTocAndLineCount)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "a.md", makeDoc(30)));
    EXPECT_TRUE(v.errorString().isEmpty());
    EXPECT_EQ(v.tocEntries().size(), 31);       // 1 个一级 + 30 个二级
    EXPECT_GT(v.lineCount(), 200u);
    EXPECT_FALSE(v.scrollModel().count() == 0);
}

TEST(ViewerWidgetTest, T2_MissingFileSetsError)
{
    ViewerWidget v;
    v.resize(600, 400);
    loadAndWait(v, "Z:/no/such/file.md");
    EXPECT_FALSE(v.errorString().isEmpty());
}

TEST(ViewerWidgetTest, T3_JumpToHeadingPutsItAtTop)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "a.md", makeDoc(200)));
    // 目录第 100 项对应的源行
    const auto toc = v.tocEntries();
    ASSERT_GT(toc.size(), 120);
    const TocEntry target = toc[100];
    v.scrollToSourceLine(target.sourceLine, false);
    v.repaint();
    // 标题文本应出现在视口最上方（前 60px 内）
    bool found = false;
    for (const auto& seg : v.segmentsForTest()) {
        if (seg.text.contains(target.title) && seg.rect.top() < 60 && seg.rect.top() > -5) found = true;
    }
    EXPECT_TRUE(found) << "标题 " << qPrintable(target.title) << " 未出现在顶部";
}

TEST(ViewerWidgetTest, T4_ScrollToBottomShowsLastSection)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "a.md", makeDoc(150)));
    v.scrollToBottom();
    v.repaint();
    bool found = false;
    for (const auto& seg : v.segmentsForTest())
        if (seg.text.contains("int x = 149;")) found = true;
    EXPECT_TRUE(found);
    // 末尾精确对齐：滚动条已到最大值
    EXPECT_EQ(v.verticalScrollBar()->value(), v.verticalScrollBar()->maximum());
}

TEST(ViewerWidgetTest, T5_SelectAllCopyEqualsWholeText)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    const QByteArray doc = makeDoc(40);
    loadAndWait(v, writeTmp(dir, "a.md", doc));
    v.selectAll();
    v.copySelection();
    QString copied = QApplication::clipboard()->text();
    QString whole = wholeDocText(QString::fromUtf8(doc));
    while (whole.endsWith('\n')) whole.chop(1);
    EXPECT_EQ(copied, whole);
}

// ---- 分块渲染 vs 整篇渲染：文本一致性（INV-CHUNK-CONSISTENT）----

static void expectChunkedEqualsWhole(const QByteArray& doc, int targetLines, int hardLines = 2000)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    ScanOptions o;
    o.targetLines = targetLines;
    o.hardLines = hardLines;
    v.setScanOptionsForTest(o);
    loadAndWait(v, writeTmp(dir, "c.md", doc));
    ASSERT_TRUE(v.errorString().isEmpty());
    const QString chunked = v.allChunkTextForTest();
    const QString whole = wholeDocText(QString::fromUtf8(doc));
    EXPECT_EQ(chunked, whole) << "targetLines=" << targetLines;
}

TEST(ViewerWidgetTest, T6_ChunkedTextEqualsWhole_GeneratedDoc)
{
    for (int t : {1, 3, 8, 40})
        expectChunkedEqualsWhole(makeDoc(60), t);
}

TEST(ViewerWidgetTest, T7_ChunkedTextEqualsWhole_TrickySnippets)
{
    const char* snippets[] = {
        "- a\n\n- b\n\n  continuation\n- c\n\nafter list\n",                                  // 松散列表
        "see [x][r] and [y][s]\n\nmiddle text\n\nmore text\n\n[r]: http://a.com\n[s]: http://b.com\n", // 文末引用定义
        "```py\n# not heading\n\nx = 1\n\n# also not\n```\n\nafter fence\n",                    // 围栏内空行与 #
        "~~~~\n```\n\n```\n~~~~\n\ntext\n",                                                  // 嵌套围栏
        "| a | b |\n|---|---|\n| 1 | 2 |\n\nparagraph\n",                                     // 表格
        "<div>\n\nhello\n\n</div>\n\nafter html\n",                                           // HTML 块含空行
        "Setext Title\n============\n\ntext\n\nSub\n---\n\nmore\n",                            // setext 标题
        "para\n\n    code a\n\n    code b\n\nend\n",                                          // 缩进代码
        "> quote a\n\n> quote b\n\n> lazy\ncontinued\n",                                      // 引用
        "3. three\n\n4. four\n\n5. five\n",                                                  // 有序列表起始号
        "text\n\n<!--\nhidden\n\n# not heading\n-->\n\nvisible\n",                            // HTML 注释含空行
        "- [ ] todo a\n- [x] done\n\n- [ ] todo c\n",                                         // 任务列表
        "# H1\n\ntext **bold** *it* ~~del~~ `c` [l](http://x)\n\n## H2\n\n1. one\n2. two\n   - nested\n", // 综合
        "---\ntitle: T\ntags: [a, b]\n---\n\n# After FM\n\nbody\n",                              // frontmatter
    };
    for (const char* s : snippets)
        for (int t : {1, 2, 5})
            expectChunkedEqualsWhole(QByteArray(s), t);
}

TEST(ViewerWidgetTest, T8_ChunkedTextEqualsWhole_ForcedFenceSplit)
{
    // 超长围栏代码被强切：文本内容（逐行）必须与整篇一致
    QByteArray doc = "intro\n\n```cpp\n";
    for (int i = 0; i < 120; ++i) doc += "line " + QByteArray::number(i) + "\n";
    doc += "```\n\noutro\n";
    expectChunkedEqualsWhole(doc, 5, /*hardLines=*/30);
}

TEST(ViewerWidgetTest, T9_SearchFindsCandidatesAndNavigates)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "a.md", makeDoc(120)));
    v.searchForTest("Paragraph for section", false);
    EXPECT_EQ(v.searchHitCountForTest(), 120);
    v.searchNextForTest();
    const int c1 = v.searchCurrentChunkForTest();
    v.searchNextForTest();
    const int c2 = v.searchCurrentChunkForTest();
    EXPECT_GE(c1, 0);
    EXPECT_GE(c2, c1);
    // 大小写敏感
    v.searchForTest("PARAGRAPH FOR SECTION", true);
    EXPECT_EQ(v.searchHitCountForTest(), 0);
    v.searchForTest("PARAGRAPH FOR SECTION", false);
    EXPECT_EQ(v.searchHitCountForTest(), 120);
}

TEST(ViewerWidgetTest, T10_SearchChineseAndSkipsMarkupOnlyHits)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    // 「secret」只出现在链接 URL（不会被渲染成文字），「中文词」出现在正文
    QByteArray doc = "# T\n\n[link](http://example.com/secret)\n\n中文词 出现在这里\n\n再来一个中文词。\n";
    loadAndWait(v, writeTmp(dir, "s.md", doc));
    v.searchForTest(QStringLiteral("中文词"));
    EXPECT_EQ(v.searchHitCountForTest(), 2);
    v.searchForTest("secret");
    EXPECT_EQ(v.searchHitCountForTest(), 1);   // 源文本候选 1 个；渲染文本中无命中 → 导航时被跳过、不崩溃
    v.searchNextForTest();
    SUCCEED();
}

TEST(ViewerWidgetTest, T11_ReloadKeepsReadingPosition)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    const QString p = writeTmp(dir, "a.md", makeDoc(300));
    loadAndWait(v, p);
    const auto toc = v.tocEntries();
    v.scrollToSourceLine(toc[200].sourceLine, false);
    v.repaint();
    const int before = v.topSourceLine();
    v.reload();
    QElapsedTimer t;
    t.start();
    while (v.isLoading() && t.elapsed() < 10000) QApplication::processEvents(QEventLoop::AllEvents, 10);
    QApplication::processEvents();
    v.prepareForTest();
    const int after = v.topSourceLine();
    EXPECT_NEAR(after, before, 5);
}

TEST(ViewerWidgetTest, T12_ResizeRelayoutKeepsAnchorChunk)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(1000, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "a.md", makeDoc(300)));
    const auto toc = v.tocEntries();
    v.scrollToSourceLine(toc[150].sourceLine, false);
    v.repaint();
    const size_t anchorBefore = v.scrollModel().anchorChunk();
    v.resize(500, 600);                       // 宽度变化 → 全部重排
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 300) QApplication::processEvents(QEventLoop::AllEvents, 10);
    v.prepareForTest();
    EXPECT_EQ(v.scrollModel().anchorChunk(), anchorBefore);
}

TEST(ViewerWidgetTest, T13_LayoutCacheIsBounded)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "a.md", makeDoc(3000)));
    // 顺序滚过大量 chunk，布局缓存不得无限增长
    for (int i = 0; i < 400; ++i) {
        QWheelEvent we(QPointF(100, 100), QPointF(100, 100), QPoint(0, -900), QPoint(0, -900),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(v.viewport(), &we);
    }
    EXPECT_LE(v.cachedChunkCount(), 200u);
}

TEST(ViewerWidgetTest, T14_EmptyAndSingleLineFiles)
{
    QTemporaryDir dir;
    {
        ViewerWidget v;
        v.resize(600, 400);
        loadAndWait(v, writeTmp(dir, "e.md", QByteArray()));
        EXPECT_TRUE(v.errorString().isEmpty());
        EXPECT_EQ(v.scrollModel().count(), 0u);
        v.scrollToBottom();
        v.selectAll();
        v.copySelection();
    }
    {
        ViewerWidget v;
        v.resize(600, 400);
        loadAndWait(v, writeTmp(dir, "one.md", "# only"));
        EXPECT_EQ(v.tocEntries().size(), 1);
        v.scrollToBottom();
        v.scrollToTop();
    }
}

// ---- 内容标记（Spec 模块-viewer/06）----

TEST(ViewerWidgetTest, T15_MarkSelectionPersistsAcrossSerializeAndReload)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    const QString p = writeTmp(dir, "m.md", makeDoc(40));
    loadAndWait(v, p);
    ASSERT_GE(v.scrollModel().count(), 3u);
    // 在第 2 个 chunk 内选一段并标记
    v.selectChunkRangeForTest(1, 0, 10);
    v.addMarkFromSelection();
    EXPECT_EQ(v.markCountForTest(), 1);
    EXPECT_FALSE(v.tocHighlightedIndices().isEmpty());
    const QByteArray data = v.serializeMarkings();
    EXPECT_TRUE(data.startsWith("SMMK2\n"));

    // 全新查看器加载同一文件，反序列化后标记应被恢复
    ViewerWidget v2;
    v2.resize(900, 600);
    v2.show();
    v2.deserializeMarkings(data);              // 加载前先注入：加载完成后兑现
    loadAndWait(v2, p);
    QApplication::processEvents();
    EXPECT_EQ(v2.markCountForTest(), 1);
    EXPECT_EQ(v2.tocHighlightedIndices(), v.tocHighlightedIndices());
}

TEST(ViewerWidgetTest, T16_MarkDroppedWhenTextNoLongerExists)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    const QString p = writeTmp(dir, "m.md", makeDoc(40));
    loadAndWait(v, p);
    v.selectChunkRangeForTest(1, 0, 10);
    v.addMarkFromSelection();
    const QByteArray data = v.serializeMarkings();

    // 文件内容完全改变：原文不存在 → 标记被丢弃，不崩溃
    const QString p2 = writeTmp(dir, "m2.md", "# Totally different\n\nnothing here\n");
    ViewerWidget v2;
    v2.resize(900, 600);
    v2.deserializeMarkings(data);
    loadAndWait(v2, p2);
    EXPECT_EQ(v2.markCountForTest(), 0);
}

TEST(ViewerWidgetTest, T17_ClearMarksInSectionAndAll)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "m.md", makeDoc(40)));
    v.selectChunkRangeForTest(1, 0, 10);
    v.addMarkFromSelection();
    v.selectChunkRangeForTest(5, 0, 10);
    v.addMarkFromSelection();
    EXPECT_EQ(v.markCountForTest(), 2);
    const int first = *std::min_element(v.tocHighlightedIndices().begin(), v.tocHighlightedIndices().end());
    v.clearMarksInSection(first);
    EXPECT_LE(v.markCountForTest(), 1);
    v.clearAllMarks();
    EXPECT_EQ(v.markCountForTest(), 0);
    EXPECT_TRUE(v.tocHighlightedIndices().isEmpty());
}

// ---- 链接 Ctrl+点击（Spec 模块-viewer/04 INV-VIEW-LINK-CTRL-CLICK）----

TEST(ViewerWidgetTest, T18_CtrlClickOnLinkEmitsLinkClicked)
{
    QTemporaryDir dir;
    ViewerWidget v;
    v.resize(900, 600);
    v.show();
    loadAndWait(v, writeTmp(dir, "l.md", "# T\n\nbefore [Example Site](https://example.com/a?b=1) after\n\n"
                                         "<https://auto.link/x> and https://bare.link/y\n"));
    QStringList got;
    QObject::connect(&v, &ViewerWidget::linkClicked, [&](const QString& u) { got << u; });

    const auto segs = v.segmentsForTest();
    QPoint target;
    for (const auto& s : segs)
        if (s.linkUrl == "https://example.com/a?b=1") target = (s.rect.center() + QPointF(20, 0)).toPoint();
    ASSERT_FALSE(target.isNull()) << "没有记录到带链接的文本段";

    // 无 Ctrl：不触发
    QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, target);
    EXPECT_TRUE(got.isEmpty());
    // Ctrl+点击：触发
    QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::ControlModifier, target);
    ASSERT_EQ(got.size(), 1);
    EXPECT_EQ(got[0], "https://example.com/a?b=1");

    // 尖括号自动链接与裸 URL 同样应被识别为链接
    QStringList urls;
    for (const auto& sg : v.segmentsForTest()) if (!sg.linkUrl.isEmpty()) urls << sg.linkUrl;
    EXPECT_TRUE(urls.contains("https://auto.link/x")) << qPrintable(urls.join(" | "));
    EXPECT_TRUE(urls.contains("https://bare.link/y")) << qPrintable(urls.join(" | "));
}

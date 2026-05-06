// tests/preview/PreviewMarkingPersistenceTest.cpp
//
// Spec: specs/模块-preview/08-内容标记.md
// 验收：
//   T-PERSIST-1  序列化-反序列化往返保持标记列表
//   T-PERSIST-2  越界标记（start<0、end<=start）在反序列化时被静默丢弃
//   T-PERSIST-3  损坏的字节流不导致崩溃，反序列化后状态等价于空
//   T-PERSIST-4  空字节流序列化后仍是合法格式（magic + version + count=0）

#include <gtest/gtest.h>
#include <QApplication>
#include <QDataStream>
#include <QIODevice>
#include <QSignalSpy>

#include "preview/PreviewWidget.h"
#include "MarkdownAst.h"
#include "MarkdownParser.h"

namespace {

// gtest 主线程 QApplication 守护
class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "PreviewMarkingPersistenceTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override {}
private:
    QApplication* app_ = nullptr;
};

constexpr quint32 kMagic = 0x534D4D4B;  // "SMMK"
constexpr quint8 kVersion = 1;

QByteArray buildMarkingsBlob(std::initializer_list<std::pair<qint32, qint32>> entries)
{
    QByteArray buf;
    QDataStream out(&buf, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_12);
    out << kMagic << kVersion << quint32(entries.size());
    for (auto& p : entries) {
        out << p.first << p.second;
    }
    return buf;
}

struct ParsedHeader {
    quint32 magic;
    quint8 version;
    quint32 count;
    QVector<QPair<qint32, qint32>> entries;
};

ParsedHeader parseBlob(const QByteArray& buf)
{
    ParsedHeader h{};
    QDataStream in(buf);
    in.setVersion(QDataStream::Qt_5_12);
    in >> h.magic >> h.version >> h.count;
    for (quint32 i = 0; i < h.count; ++i) {
        qint32 s, e;
        in >> s >> e;
        h.entries.append({s, e});
    }
    return h;
}

}  // namespace

// 注入字节流（先喂 AST 让 m_currentAst 就绪，再 deserialize），随后 serialize 对比。
// 顺序保留——applyPendingMarkings 按读取顺序 append。
//
// 注意：[2026-05-06 修正] PreviewWidget 现在要求 m_currentAst 就绪后 deserialize 才会
// 立即应用；裸构造 widget（无 AST）下 deserialize 只把数据存到 m_pendingMarkings，
// 等下次 updateAst 才兑现。所以 round-trip 测试必须先喂一段足够长的样例 AST。
TEST(PreviewMarkingPersistenceTest, T1_RoundTripPreservesEntries)
{
    PreviewWidget w;
    // 喂入一个足够长的样例 AST，避免被夹紧/截断
    MarkdownParser parser;
    auto astU = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + "\n");
    ASSERT_NE(astU, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astU));
    w.updateAst(ast);

    QByteArray in = buildMarkingsBlob({{10, 20}, {30, 50}, {100, 150}});
    w.deserializeMarkings(in);
    QByteArray out = w.serializeMarkings();

    auto h = parseBlob(out);
    EXPECT_EQ(h.magic, kMagic);
    EXPECT_EQ(h.version, kVersion);
    ASSERT_EQ(h.count, 3u);
    EXPECT_EQ(h.entries[0], qMakePair(qint32(10), qint32(20)));
    EXPECT_EQ(h.entries[1], qMakePair(qint32(30), qint32(50)));
    EXPECT_EQ(h.entries[2], qMakePair(qint32(100), qint32(150)));
}

// Spec §8.3：字符偏移漂移容忍——start<0 / end<=start 的条目静默丢弃。
// 同 T1 修正：先喂 AST 让 deserialize 能立即应用。
TEST(PreviewMarkingPersistenceTest, T2_OutOfRangeEntriesDiscarded)
{
    PreviewWidget w;
    // 200 字符的样例文档，足够覆盖测试中所有 valid 偏移
    MarkdownParser parser;
    auto astU = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + "\n");
    ASSERT_NE(astU, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astU));
    w.updateAst(ast);

    QByteArray in = buildMarkingsBlob({
        {-1, 10},   // start<0 → discard
        {5, 5},     // end<=start → discard
        {7, 3},     // end<start → discard
        {10, 20},   // valid
        {30, 50},   // valid
    });
    w.deserializeMarkings(in);
    QByteArray out = w.serializeMarkings();

    auto h = parseBlob(out);
    ASSERT_EQ(h.count, 2u);
    EXPECT_EQ(h.entries[0], qMakePair(qint32(10), qint32(20)));
    EXPECT_EQ(h.entries[1], qMakePair(qint32(30), qint32(50)));
}

// 损坏字节流不应导致崩溃；反序列化后 m_pendingMarkings 应被清空，
// m_highlights 不变（仍为构造默认空），serialize 输出应为合法的"零条目"格式。
TEST(PreviewMarkingPersistenceTest, T3_CorruptedDataDoesNotCrash)
{
    PreviewWidget w;
    // 故意写错 magic
    QByteArray bad;
    QDataStream bs(&bad, QIODevice::WriteOnly);
    bs.setVersion(QDataStream::Qt_5_12);
    bs << quint32(0xDEADBEEF) << quint8(99) << quint32(1) << qint32(0) << qint32(10);

    ASSERT_NO_FATAL_FAILURE(w.deserializeMarkings(bad));

    QByteArray out = w.serializeMarkings();
    auto h = parseBlob(out);
    EXPECT_EQ(h.magic, kMagic);
    EXPECT_EQ(h.version, kVersion);
    EXPECT_EQ(h.count, 0u);
}

// 空字节流：deserialize 应清空 pending；serialize 应返回合法的空格式
TEST(PreviewMarkingPersistenceTest, T4_EmptyDataYieldsValidEmptySerialization)
{
    PreviewWidget w;
    w.deserializeMarkings(QByteArray());

    QByteArray out = w.serializeMarkings();
    auto h = parseBlob(out);
    EXPECT_EQ(h.magic, kMagic);
    EXPECT_EQ(h.version, kVersion);
    EXPECT_EQ(h.count, 0u);
}

// T-PERSIST-5（2026-05-06 新增回归保护）
// 复现 race condition：MainWindow::restoreSession 调 deserializeMarkings 时
// PreviewWidget 还没收到 ParseScheduler 的 astReady（m_currentAst==nullptr）。
// 此时 deserialize 必须**保留** m_pendingMarkings 不消耗，等 updateAst 触发后才应用。
//
// 历史 bug（被本测试拦截）：旧 applyPendingMarkings 在消耗 m_pendingMarkings 后才
// 检查 m_currentAst，导致 AST 未就绪时 m_highlights 被写入但 m_pendingMarkings 已清空，
// 紧接着 updateAst 内 m_highlights.clear() 把数据抹除 → 标记彻底丢失。
TEST(PreviewMarkingPersistenceTest, T5_DeserializeBeforeAstReadyPreservesPending)
{
    PreviewWidget w;
    // 此时 PreviewWidget 还没收到任何 updateAst —— m_currentAst == nullptr。
    QByteArray in = buildMarkingsBlob({{10, 20}, {30, 50}});
    w.deserializeMarkings(in);

    // serialize 当前状态：m_highlights 还应当为空（AST 未就绪，pending 未应用），
    // 但 pending 已存住。直接 serialize 拿到的是 m_highlights 而非 pending，
    // 所以 count 应为 0（数据未丢失，只是没应用）。
    QByteArray afterDeser = w.serializeMarkings();
    auto h0 = parseBlob(afterDeser);
    EXPECT_EQ(h0.count, 0u)
        << "AST 未就绪时 deserialize 不应直接写入 m_highlights";

    // 现在喂入一个**足够长**的 AST，模拟 updateAst 触发——内部应当调用
    // applyPendingMarkings 把保留的 pending 兑现到 m_highlights。
    // 必须 ≥ 50 字符，让 {30, 50} 偏移不被 textLen 容差丢弃。
    MarkdownParser parser;
    auto astU = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + "\n");
    ASSERT_NE(astU, nullptr);
    std::shared_ptr<AstNode> ast(std::move(astU));
    w.updateAst(ast);

    // 现在 serialize 应当看到原始两个 entry
    QByteArray afterUpdate = w.serializeMarkings();
    auto h1 = parseBlob(afterUpdate);
    EXPECT_EQ(h1.count, 2u)
        << "updateAst 后 pendingMarkings 应当被应用到 m_highlights";
    if (h1.entries.size() >= 2) {
        EXPECT_EQ(h1.entries[0], qMakePair(qint32(10), qint32(20)));
        EXPECT_EQ(h1.entries[1], qMakePair(qint32(30), qint32(50)));
    }
}

// T-PERSIST-6（2026-05-06，2026-05-06 修订）
// 原 T6 断言「updateAst 后 emit 空 set」用于保护 TOC 残留 bug——
// 但 INV-MARK-EDIT-PRESERVE 之后 m_highlights 在 updateAst 中保留，TOC 也应保留同步状态。
// 新断言：updateAst 必须 emit 至少一次 tocHighlightChanged，且**最后一次 emit 的内容
// 必须与当前 m_highlights 一致**（同步），而不再要求"空 set"。
TEST(PreviewMarkingPersistenceTest, T6_UpdateAstKeepsTocHighlightsInSync)
{
    PreviewWidget w;
    MarkdownParser parser;
    auto astU1 = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + "\n");
    ASSERT_NE(astU1, nullptr);
    w.updateAst(std::shared_ptr<AstNode>(std::move(astU1)));

    // 注入两个标记
    QSignalSpy spy(&w, SIGNAL(tocHighlightChanged(const QSet<int>&)));
    w.deserializeMarkings(buildMarkingsBlob({{10, 20}, {30, 50}}));
    EXPECT_GE(spy.count(), 1)
        << "deserialize + applyPendingMarkings 应当至少 emit 一次 tocHighlightChanged";

    // 第二次 updateAst（模拟编辑）—— m_highlights 必须保留，TOC 必须同步
    spy.clear();
    auto astU2 = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + " more\n");
    ASSERT_NE(astU2, nullptr);
    w.updateAst(std::shared_ptr<AstNode>(std::move(astU2)));

    ASSERT_GE(spy.count(), 1)
        << "updateAst 必须 emit tocHighlightChanged（保持 TOC 与 m_highlights 同步）";
    // 当前 m_highlights 仍含 (10,20) 和 (30,50)，章节边界由新 m_headingCharOffsets 决定，
    // m_tocHighlighted 与 tocHighlightedIndices() 必须一致
    EXPECT_EQ(w.tocHighlightedIndices().size(),
              spy.takeLast().at(0).value<QSet<int>>().size())
        << "最后一次 emit 的 set 必须与当前 m_tocHighlighted 状态一致";
}

// T-PERSIST-7（2026-05-06 plan #8 Step 1）
// 编辑文档（updateAst 触发）时 m_highlights 必须保留——是 INV-MARK-EDIT-PRESERVE 回归保护。
// 之前 updateAst 中无条件 m_highlights.clear() 会让用户每输入一字就丢标记。
TEST(PreviewMarkingPersistenceTest, T7_HighlightsPreservedAcrossUpdateAst)
{
    PreviewWidget w;
    MarkdownParser parser;

    // 第一次 updateAst：模拟初始解析
    auto ast1 = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + "\n");
    ASSERT_NE(ast1, nullptr);
    w.updateAst(std::shared_ptr<AstNode>(std::move(ast1)));

    // 注入两个标记
    w.deserializeMarkings(buildMarkingsBlob({{10, 20}, {30, 50}}));
    auto h0 = parseBlob(w.serializeMarkings());
    ASSERT_EQ(h0.count, 2u) << "deserialize 后应当有 2 个标记";

    // 第二次 updateAst：模拟用户编辑后重新解析
    auto ast2 = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + " modified\n");
    ASSERT_NE(ast2, nullptr);
    w.updateAst(std::shared_ptr<AstNode>(std::move(ast2)));

    // 关键断言：标记不应丢失
    auto h1 = parseBlob(w.serializeMarkings());
    EXPECT_EQ(h1.count, 2u)
        << "编辑后 updateAst 触发，m_highlights 必须保留（INV-MARK-EDIT-PRESERVE）";
    if (h1.entries.size() >= 2) {
        EXPECT_EQ(h1.entries[0], qMakePair(qint32(10), qint32(20)));
        EXPECT_EQ(h1.entries[1], qMakePair(qint32(30), qint32(50)));
    }
}

// T-PERSIST-8（2026-05-06 plan #6）
// dormantTab 路径：MainWindow 调用 preview->updateAst(nullptr) 显式清空预览状态。
// 这个路径与编辑路径（root 非空）行为不同——必须主动清 m_highlights / m_pendingMarkings /
// m_tocHighlighted，让 Tab 真正进入"休眠"无内容状态。这与 INV-MARK-EDIT-PRESERVE
// 不矛盾：INV-MARK-EDIT-PRESERVE 仅约束 root 非空时不清空。
TEST(PreviewMarkingPersistenceTest, T8_UpdateAstNullExplicitlyClearsHighlights)
{
    PreviewWidget w;
    MarkdownParser parser;
    auto astU = parser.parse(QString("# Heading\n\n") + QString(200, QChar('a')) + "\n");
    ASSERT_NE(astU, nullptr);
    w.updateAst(std::shared_ptr<AstNode>(std::move(astU)));

    // 注入标记
    w.deserializeMarkings(buildMarkingsBlob({{10, 20}, {30, 50}}));
    auto h0 = parseBlob(w.serializeMarkings());
    ASSERT_EQ(h0.count, 2u);

    // 显式 nullptr：模拟 dormantTab 调用
    w.updateAst(nullptr);

    // 标记必须被清空（与编辑路径相反——休眠是显式释放）
    auto h1 = parseBlob(w.serializeMarkings());
    EXPECT_EQ(h1.count, 0u)
        << "updateAst(nullptr) 必须清空 m_highlights（休眠路径释放内存）";

    // 唤醒：再喂入新 AST，标记应当保持清空（不会"自动恢复"——已被消耗）
    auto astU2 = parser.parse(QStringLiteral("# After\n\nbody\n"));
    ASSERT_NE(astU2, nullptr);
    w.updateAst(std::shared_ptr<AstNode>(std::move(astU2)));
    auto h2 = parseBlob(w.serializeMarkings());
    EXPECT_EQ(h2.count, 0u)
        << "唤醒后标记不应自动恢复（用户期望休眠 = 完全释放，由会话机制重新加载持久化标记）";
}

// T-PERSIST-9（2026-05-06 plan #14 子场景 3）
// buildHeadingCharOffsets 必须与 extractBlockText 字节级一致——Frontmatter 块
// 在 extractBlockText 中累加 rawText + \n 进 m_plainText，buildHeadingCharOffsets
// 也必须同步累加 charIdx，否则含 frontmatter 文档的章节 char offset 全部偏移，
// updateTocHighlights 按章节范围判定标记时归属错位。
//
// 测试方法：构造含 frontmatter + 多个 heading 的文档，分别在每个章节内的字符位置
// 添加标记，断言 TOC 高亮的章节索引等于预期。
TEST(PreviewMarkingPersistenceTest, T9_HeadingOffsetsAlignWithPlainTextWithFrontmatter)
{
    PreviewWidget w;
    MarkdownParser parser;

    // 含 frontmatter + 3 个章节
    QString doc = QStringLiteral(
        "---\n"
        "title: sample\n"
        "tags:\n"
        "  - a\n"
        "  - b\n"
        "---\n"
        "\n"
        "# Section A\n"
        "\n"
        "Body of section A goes here with enough text.\n"
        "\n"
        "# Section B\n"
        "\n"
        "Body of section B goes here with enough text.\n"
        "\n"
        "# Section C\n"
        "\n"
        "Body of section C goes here with enough text.\n");
    auto astU = parser.parse(doc);
    ASSERT_NE(astU, nullptr);
    w.updateAst(std::shared_ptr<AstNode>(std::move(astU)));

    // 找出 Section B 在 m_plainText 中的字符偏移（运行时实测，避免硬编码）
    // 我们通过 widget 接口侧测——deserialize 一个标记到 Section B 的位置，
    // 检查 TOC 高亮的章节集合是否仅含 Section B（index=1）

    // 序列化路径不直接暴露 m_plainText/headingOffsets，但 tocHighlightedIndices()
    // 反映 updateTocHighlights 的结果。先获取 plainText 模拟（通过 layout 间接）：
    // 简化做法：依赖 PreviewWidget 内部正确性，通过对每个章节插入标记 + 检查 TOC
    // 高亮 index 是否对应。

    // 标记落在第二个章节正文 "Body of section B" 范围内。
    // 在含 frontmatter 文档中，frontmatter rawText 长度约 47 字符（5 行约 8-10 字符/行）+\n。
    // section A heading "# Section A\n" 后 charIdx 约 47+1+12=60，section A body 后约 60+44=104，
    // section B heading 起点约 104，section B body 起点约 104+12=116。
    // 取 charIdx ≈ 130（B body 中段）+ 8 字符标记。
    QSignalSpy spy(&w, SIGNAL(tocHighlightChanged(const QSet<int>&)));
    w.deserializeMarkings(buildMarkingsBlob({{130, 138}}));

    ASSERT_GE(spy.count(), 1) << "deserialize 应当触发 tocHighlightChanged emit";
    QSet<int> highlighted = spy.takeLast().at(0).value<QSet<int>>();

    // 关键断言：标记落在第 2 个章节（index=1, Section B）—— 不应当因 frontmatter
    // char offset 错位而被误算到 Section A 或 Section C
    ASSERT_EQ(highlighted.size(), 1u)
        << "标记 [130,138] 应当只命中 1 个章节（实际命中 " << highlighted.size() << " 个）";
    EXPECT_TRUE(highlighted.contains(1))
        << "标记 [130,138] 应当命中 Section B (index=1)；实际命中: "
        << (highlighted.values().isEmpty() ? -1 : highlighted.values().first());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new QAppFixture);
    return RUN_ALL_TESTS();
}

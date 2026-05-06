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

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new QAppFixture);
    return RUN_ALL_TESTS();
}

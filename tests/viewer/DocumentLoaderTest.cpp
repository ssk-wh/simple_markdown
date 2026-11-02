// tests/viewer/DocumentLoaderTest.cpp
//
// Spec: specs/模块-viewer/03-异步加载.md
// 验收：T1 ~ T9
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QTimer>

#include "DocumentLoader.h"

using viewer::DocumentLoader;
using viewer::LoadBatch;

namespace {

QCoreApplication* ensureApp()
{
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char arg0[] = "DocumentLoaderTest";
        static char* argv[] = {arg0, nullptr};
        new QCoreApplication(argc, argv);
    }
    return QCoreApplication::instance();
}

QString writeFile(QTemporaryDir& dir, const QString& name, const QByteArray& data)
{
    const QString p = dir.filePath(name);
    QFile f(p);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
    return p;
}

QByteArray makeDoc(int sections)
{
    QByteArray t;
    for (int i = 0; i < sections; ++i) {
        t += "## Section " + QByteArray::number(i) + "\n\n";
        t += "paragraph text paragraph text\n\n- a\n- b\n\n";
    }
    return t;
}

// 异步加载并等待完成；返回合并结果
LoadBatch loadAsync(const QString& path, int timeoutMs = 20000, int* batchCount = nullptr)
{
    ensureApp();
    DocumentLoader loader;
    LoadBatch all;
    int batches = 0;
    QEventLoop loop;
    QObject::connect(&loader, &DocumentLoader::batchesAvailable, [&]() {
        for (auto& b : loader.takeBatches()) {
            ++batches;
            all.chunks.insert(all.chunks.end(), b.chunks.begin(), b.chunks.end());
            all.headings.insert(all.headings.end(), b.headings.begin(), b.headings.end());
            all.watermark = b.watermark;
            all.lineCount = b.lineCount;
            if (!b.error.isEmpty()) all.error = b.error;
            if (b.done) { all.done = true; loop.quit(); }
        }
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loader.start(path);
    loop.exec();
    if (batchCount) *batchCount = batches;
    return all;
}

}  // namespace

TEST(DocumentLoaderTest, T1_AsyncEqualsSync)
{
    ensureApp();
    QTemporaryDir dir;
    const QString p = writeFile(dir, "a.md", makeDoc(20000));  // ~1MB，多批次
    int batches = 0;
    LoadBatch a = loadAsync(p, 20000, &batches);
    std::shared_ptr<QByteArray> buf;
    LoadBatch s = DocumentLoader::loadSync(p, buf);
    ASSERT_TRUE(a.done);
    EXPECT_GT(batches, 1);                       // 确实分批发布
    ASSERT_EQ(a.chunks.size(), s.chunks.size());
    EXPECT_EQ(a.headings.size(), 20000u);
    EXPECT_EQ(a.headings.size(), s.headings.size());
    EXPECT_EQ(a.lineCount, s.lineCount);
    EXPECT_EQ(a.watermark, static_cast<uint64_t>(buf->size()));
}

TEST(DocumentLoaderTest, T2_EmptyFile)
{
    ensureApp();
    QTemporaryDir dir;
    const QString p = writeFile(dir, "e.md", QByteArray());
    LoadBatch a = loadAsync(p);
    EXPECT_TRUE(a.done);
    EXPECT_TRUE(a.error.isEmpty());
    EXPECT_TRUE(a.chunks.empty());
}

TEST(DocumentLoaderTest, T3_MissingFileReportsError)
{
    ensureApp();
    LoadBatch a = loadAsync("Z:/definitely/not/exist.md");
    EXPECT_TRUE(a.done);
    EXPECT_FALSE(a.error.isEmpty());
}

TEST(DocumentLoaderTest, T4_BinaryRejected)
{
    ensureApp();
    QTemporaryDir dir;
    QByteArray bin("MZ\x90\x00\x03\x00\x00\x00", 8);
    bin.append(QByteArray(4000, '\0'));
    const QString p = writeFile(dir, "b.md", bin);
    LoadBatch a = loadAsync(p);
    EXPECT_TRUE(a.done);
    EXPECT_FALSE(a.error.isEmpty());
}

TEST(DocumentLoaderTest, T5_GbkTranscodedToUtf8)
{
    ensureApp();
    QTemporaryDir dir;
    QTextCodec* gbk = QTextCodec::codecForName("GBK");
    ASSERT_NE(gbk, nullptr);
    const QByteArray raw = gbk->fromUnicode(QStringLiteral("# 中文标题\n\n正文内容\n"));
    const QString p = writeFile(dir, "g.md", raw);
    LoadBatch a = loadAsync(p);
    ASSERT_TRUE(a.done);
    ASSERT_TRUE(a.error.isEmpty());
    ASSERT_EQ(a.headings.size(), 1u);
    EXPECT_EQ(a.headings[0].title, QStringLiteral("中文标题"));
}

TEST(DocumentLoaderTest, T6_Utf16BomTranscoded)
{
    ensureApp();
    QTemporaryDir dir;
    const QString text = QStringLiteral("# 标题 Title\n\nbody\n");
    QByteArray raw("\xFF\xFE", 2);
    raw.append(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
    const QString p = writeFile(dir, "u16.md", raw);
    LoadBatch a = loadAsync(p);
    ASSERT_TRUE(a.done);
    ASSERT_TRUE(a.error.isEmpty());
    ASSERT_EQ(a.headings.size(), 1u);
    EXPECT_EQ(a.headings[0].title, QStringLiteral("标题 Title"));
}

TEST(DocumentLoaderTest, T7_CancelMidwayIsSafe)
{
    ensureApp();
    QTemporaryDir dir;
    const QString p = writeFile(dir, "big.md", makeDoc(100000));  // ~5MB
    for (int i = 0; i < 20; ++i) {
        auto* loader = new DocumentLoader;
        loader->start(p);
        if (i % 2) QCoreApplication::processEvents();
        delete loader;  // 销毁时 worker 仍在运行：不得崩溃/死锁
    }
    QCoreApplication::processEvents();
    SUCCEED();
}

TEST(DocumentLoaderTest, T8_RestartDiscardsOldResults)
{
    ensureApp();
    QTemporaryDir dir;
    const QString p1 = writeFile(dir, "1.md", makeDoc(50000));
    const QString p2 = writeFile(dir, "2.md", "# Only\n");
    DocumentLoader loader;
    loader.start(p1);
    loader.start(p2);  // 立刻重启：旧 worker 的结果不得混入
    LoadBatch all;
    QEventLoop loop;
    QObject::connect(&loader, &DocumentLoader::batchesAvailable, [&]() {
        for (auto& b : loader.takeBatches()) {
            all.headings.insert(all.headings.end(), b.headings.begin(), b.headings.end());
            if (b.done) { all.done = true; loop.quit(); }
        }
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    ASSERT_TRUE(all.done);
    ASSERT_EQ(all.headings.size(), 1u);
    EXPECT_EQ(all.headings[0].title, QStringLiteral("Only"));
}

TEST(DocumentLoaderTest, T9_FirstBatchBeforeWholeFileRead)
{
    // 大文件首批应在远早于读完整个文件时发布（首屏与文件大小无关）
    ensureApp();
    QTemporaryDir dir;
    const QString p = writeFile(dir, "huge.md", makeDoc(qEnvironmentVariableIntValue("T9N") > 0 ? qEnvironmentVariableIntValue("T9N") : 400000));  // ~20MB
    DocumentLoader loader;
    QElapsedTimer t;
    t.start();
    qint64 firstMs = -1, doneMs = -1;
    uint64_t firstWatermark = 0;
    QEventLoop loop;
    QObject::connect(&loader, &DocumentLoader::batchesAvailable, [&]() {
        for (auto& b : loader.takeBatches()) {
            if (firstMs < 0) { firstMs = t.elapsed(); firstWatermark = b.watermark; }
            if (b.done) { doneMs = t.elapsed(); loop.quit(); }
        }
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loader.start(p);
    loop.exec();
    std::printf("  [perf] 20MB: first batch %lld ms (watermark=%llu), done %lld ms\n",
                (long long)firstMs, (unsigned long long)firstWatermark, (long long)doneMs);
    ASSERT_GE(firstMs, 0);
    EXPECT_LT(firstWatermark, 1024u * 1024u);
    EXPECT_LE(firstMs, doneMs);
}

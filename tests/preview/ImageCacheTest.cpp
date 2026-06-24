// Spec: specs/模块-preview/05-图片缓存.md
// 验收：T1-T6
#include <gtest/gtest.h>

#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "ImageCache.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "ImageCacheTest";
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

// 在指定目录创建 SVG 文件
static QString createSvgFile(const QString& dir, const QString& name,
                             int w, int h, const QString& color)
{
    QString path = QDir(dir).absoluteFilePath(name);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QStringLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="%1" height="%2">)"
        R"(<rect width="%1" height="%2" fill="%3"/></svg>)")
        .arg(w).arg(h).arg(color).toUtf8());
    f.close();
    return path;
}

} // namespace

TEST(ImageCacheTest, T1_LocalSvgFile)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    createSvgFile(tmpDir.path(), "test.svg", 100, 50, "red");

    ImageCache cache;
    cache.setDocumentDir(tmpDir.path());

    QPixmap* pix = cache.get("test.svg");
    ASSERT_NE(pix, nullptr);
    EXPECT_EQ(pix->width(), 100);
    EXPECT_EQ(pix->height(), 50);
}

TEST(ImageCacheTest, T2_SvgDataUri)
{
    ImageCache cache;
    QString svg = QStringLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="50" height="50">)"
        R"(<circle cx="25" cy="25" r="20" fill="blue"/></svg>)");
    QString dataUri = QStringLiteral("data:image/svg+xml;base64,")
        + QString::fromLatin1(svg.toUtf8().toBase64());

    QPixmap* pix = cache.get(dataUri);
    ASSERT_NE(pix, nullptr);
    EXPECT_EQ(pix->width(), 50);
    EXPECT_EQ(pix->height(), 50);
}

TEST(ImageCacheTest, T3_SvgGetSize)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    createSvgFile(tmpDir.path(), "size.svg", 120, 80, "green");

    ImageCache cache;
    cache.setDocumentDir(tmpDir.path());

    QSize sz = cache.getSize("size.svg");
    EXPECT_TRUE(sz.isValid());
    EXPECT_EQ(sz.width(), 120);
    EXPECT_EQ(sz.height(), 80);
}

// T4: 无 intrinsic size 的 SVG——QSvgRenderer::defaultSize 此时返回 Qt 内部默认值
// （Qt 5.12 默认 100×100），验证尺寸有效且可渲染即可，不约束具体数值。
TEST(ImageCacheTest, T4_SvgNoIntrinsicSize)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    QString path = QDir(tmpDir.path()).absoluteFilePath("nosize.svg");
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QByteArrayLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg">)"
        R"(<rect width="100%" height="100%" fill="blue"/></svg>)"));
    f.close();

    ImageCache cache;
    cache.setDocumentDir(tmpDir.path());

    QSize sz = cache.getSize("nosize.svg");
    EXPECT_TRUE(sz.isValid());
    EXPECT_GT(sz.width(), 0);
    EXPECT_GT(sz.height(), 0);

    QPixmap* pix = cache.get("nosize.svg");
    ASSERT_NE(pix, nullptr);
    EXPECT_GT(pix->width(), 0);
    EXPECT_GT(pix->height(), 0);
}

TEST(ImageCacheTest, T5_SvgFailedFile)
{
    ImageCache cache;
    cache.setDocumentDir("C:/nonexistent");

    QPixmap* pix = cache.get("nonexistent.svg");
    EXPECT_EQ(pix, nullptr);
    EXPECT_TRUE(cache.isFailed("nonexistent.svg"));
}

TEST(ImageCacheTest, T6_NonSvgStillWorks)
{
    // 确保非 SVG 图片仍正常工作（回归保护）
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    // 创建一个 1x1 红色 PNG
    QString path = QDir(tmpDir.path()).absoluteFilePath("test.png");
    QImage img(1, 1, QImage::Format_ARGB32);
    img.fill(Qt::red);
    img.save(path);

    ImageCache cache;
    cache.setDocumentDir(tmpDir.path());

    QPixmap* pix = cache.get("test.png");
    ASSERT_NE(pix, nullptr);
    EXPECT_EQ(pix->width(), 1);
    EXPECT_EQ(pix->height(), 1);
}

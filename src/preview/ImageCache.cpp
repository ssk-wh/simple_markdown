// Spec: specs/模块-preview/05-图片缓存.md
// Invariants enforced here: INV-IMG-NET, INV-IMG-SIZE-LIMIT
// Last synced: 2026-04-17
#include "ImageCache.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QByteArray>
#include <QBuffer>
#include <QImageReader>

ImageCache::ImageCache(QObject* parent)
    : QObject(parent)
{
}

ImageCache::~ImageCache() = default;

void ImageCache::setDocumentDir(const QString& dir)
{
    if (m_documentDir == dir) return;
    m_documentDir = dir;
    // 文档路径变更时清空缓存，因为相对路径的含义变了
    clear();
    m_sizeCache.clear();
}

QString ImageCache::resolveLocalPath(const QString& url) const
{
    // file:// URI → 本地路径
    if (url.startsWith("file:///")) {
        return QUrl(url).toLocalFile();
    }
    if (url.startsWith("file://")) {
        return QUrl(url).toLocalFile();
    }

    // 绝对路径直接返回
    QFileInfo fi(url);
    if (fi.isAbsolute()) {
        return url;
    }

    // 相对路径：基于文档目录解析
    if (!m_documentDir.isEmpty()) {
        return QDir(m_documentDir).absoluteFilePath(url);
    }

    // 没有文档目录，返回原始路径（QPixmap::load 会尝试当前工作目录）
    return url;
}

bool ImageCache::loadDataUri(const QString& url, QPixmap& out)
{
    // data:image/png;base64,iVBOR...
    // data:image/jpeg;base64,/9j/...
    int commaPos = url.indexOf(',');
    if (commaPos < 0) return false;

    QString header = url.left(commaPos).toLower();
    if (!header.contains("image/")) return false;

    bool isBase64 = header.contains("base64");
    QByteArray rawData = url.mid(commaPos + 1).toUtf8();

    QByteArray imageData;
    if (isBase64) {
        imageData = QByteArray::fromBase64(rawData);
    } else {
        imageData = QByteArray::fromPercentEncoding(rawData);
    }

    // 大小限制
    if (imageData.size() > kMaxImageSize) return false;

    return out.loadFromData(imageData);
}

void ImageCache::startNetworkDownload(const QString& url)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        connect(m_nam, &QNetworkAccessManager::finished,
                this, &ImageCache::onNetworkReply);
    }

    QUrl qurl(url);
    QNetworkRequest request(qurl);
    // Qt 5.12: 使用 FollowRedirectsAttribute 替代 RedirectPolicyAttribute
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    QNetworkReply* reply = m_nam->get(request);
    // 将原始 URL 存在 reply 属性中，以便回调时取回
    reply->setProperty("originalUrl", url);
}

void ImageCache::onNetworkReply(QNetworkReply* reply)
{
    reply->deleteLater();

    QString url = reply->property("originalUrl").toString();
    m_loadingUrls.remove(url);

    if (reply->error() != QNetworkReply::NoError) {
        m_failedUrls.insert(url);
        emit imageReady(url);
        return;
    }

    // 检查 Content-Type 是否为图片
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (!contentType.isEmpty() && !contentType.startsWith("image/")) {
        m_failedUrls.insert(url);
        emit imageReady(url);
        return;
    }

    QByteArray data = reply->readAll();

    // 大小限制
    if (data.size() > kMaxImageSize) {
        m_failedUrls.insert(url);
        emit imageReady(url);
        return;
    }

    QPixmap pixmap;
    if (pixmap.loadFromData(data)) {
        m_cache.insert(url, pixmap);
    } else {
        m_failedUrls.insert(url);
    }

    emit imageReady(url);
}

// [plan A5 2026-05-12] 只读图片头拿尺寸——不解码像素，layout 阶段用。
// 网络 / data URI 暂不支持（layout 时还未下载完，paint 时再 fallback 到 get）
QSize ImageCache::getSize(const QString& url)
{
    if (url.isEmpty()) return QSize();

    auto cit = m_sizeCache.find(url);
    if (cit != m_sizeCache.end()) return cit.value();

    // 已 decode 的图片直接读其尺寸
    auto pit = m_cache.find(url);
    if (pit != m_cache.end() && !pit.value().isNull()) {
        QSize s = pit.value().size();
        m_sizeCache.insert(url, s);
        return s;
    }

    // data URI / 网络 URL 在 layout 阶段无法仅读 header——返回空，
    // layout 用默认 200px 占位，paint 时 get() 触发实际加载
    if (isDataUri(url) || isNetworkUrl(url)) return QSize();

    // 本地文件：QImageReader 只读 header
    QString path = resolveLocalPath(url);
    if (path.isEmpty()) return QSize();
    QImageReader reader(path);
    QSize size = reader.size();
    if (size.isValid()) {
        m_sizeCache.insert(url, size);
    }
    return size;
}

QPixmap* ImageCache::get(const QString& url)
{
    if (url.isEmpty()) return nullptr;

    // 检查缓存
    auto it = m_cache.find(url);
    if (it != m_cache.end()) {
        return &it.value();
    }

    // 已经失败的不再尝试
    if (m_failedUrls.contains(url)) {
        return nullptr;
    }

    // data URI: 同步解码
    if (isDataUri(url)) {
        QPixmap pixmap;
        if (loadDataUri(url, pixmap)) {
            auto inserted = m_cache.insert(url, pixmap);
            emit imageReady(url);
            return &inserted.value();
        }
        m_failedUrls.insert(url);
        return nullptr;
    }

    // 网络 URL: 异步下载
    if (isNetworkUrl(url)) {
        if (!m_loadingUrls.contains(url)) {
            m_loadingUrls.insert(url);
            startNetworkDownload(url);
        }
        return nullptr;
    }

    // 本地文件
    QString resolvedPath = resolveLocalPath(url);
    QFileInfo fi(resolvedPath);
    if (!fi.exists() || !fi.isFile()) {
        m_failedUrls.insert(url);
        return nullptr;
    }

    // 大小限制
    if (fi.size() > kMaxImageSize) {
        m_failedUrls.insert(url);
        return nullptr;
    }

    QPixmap pixmap;
    if (pixmap.load(resolvedPath)) {
        auto inserted = m_cache.insert(url, pixmap);
        emit imageReady(url);
        return &inserted.value();
    }

    m_failedUrls.insert(url);
    return nullptr;
}

bool ImageCache::isFailed(const QString& url) const
{
    return m_failedUrls.contains(url);
}

bool ImageCache::isLoading(const QString& url) const
{
    return m_loadingUrls.contains(url);
}

bool ImageCache::isNetworkUrl(const QString& url) const
{
    return url.startsWith("http://") || url.startsWith("https://");
}

bool ImageCache::isDataUri(const QString& url) const
{
    return url.startsWith("data:");
}

void ImageCache::clear()
{
    m_cache.clear();
    m_failedUrls.clear();
    // 不清 m_loadingUrls —— 正在飞的请求让它自然完成
}

// Spec: specs/模块-preview/05-图片缓存.md
// Invariants enforced here: INV-IMG-NET, INV-IMG-SIZE-LIMIT
// Last synced: 2026-04-17
#pragma once

#include <QObject>
#include <QPixmap>
#include <QHash>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class ImageCache : public QObject {
    Q_OBJECT
public:
    explicit ImageCache(QObject* parent = nullptr);
    ~ImageCache() override;

    // 设置当前文档所在目录，用于解析相对路径
    void setDocumentDir(const QString& dir);
    const QString& documentDir() const { return m_documentDir; }

    // 获取图片（可能触发异步下载）。返回 nullptr 表示尚未就绪
    QPixmap* get(const QString& url);

    // [plan A5 2026-05-12] 只读图片尺寸不解码像素——layout 阶段算高度用，
    // 避免视口外图片在 layout 阶段被立即 decode 到 QPixmap 缓存
    QSize getSize(const QString& url);

    bool isFailed(const QString& url) const;
    bool isLoading(const QString& url) const;
    bool isNetworkUrl(const QString& url) const;
    bool isDataUri(const QString& url) const;
    void clear();

signals:
    void imageReady(const QString& url);

private:
    // 解析图片 URL 为本地文件绝对路径（相对路径基于 m_documentDir）
    QString resolveLocalPath(const QString& url) const;
    // 解码 data URI 为 QPixmap
    bool loadDataUri(const QString& url, QPixmap& out);
    // 发起网络下载
    void startNetworkDownload(const QString& url);
    // 网络请求完成回调
    void onNetworkReply(QNetworkReply* reply);

    QHash<QString, QPixmap> m_cache;
    // [plan A5] 尺寸缓存（只读 header 不解码像素）：layout 阶段查询不触发全图加载
    QHash<QString, QSize> m_sizeCache;
    QSet<QString> m_failedUrls;
    QSet<QString> m_loadingUrls;   // 正在下载的网络 URL
    QString m_documentDir;         // 当前文档所在目录

    QNetworkAccessManager* m_nam = nullptr;

    static constexpr qint64 kMaxImageSize = 20 * 1024 * 1024; // 20MB
};

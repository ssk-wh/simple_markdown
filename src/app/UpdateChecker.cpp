// src/app/UpdateChecker.cpp
//
// Spec: specs/模块-app/23-检查更新.md
// Invariants enforced here: INV-UPD-SEMVER, INV-UPD-SILENT-AUTO, INV-UPD-NO-AUTODOWNLOAD
// Last synced: 2026-06-16
#include "UpdateChecker.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QCoreApplication>
#include <QSslSocket>
#include <QTimer>

namespace {
// GitHub 仓库坐标（与 git remote / 发版流程一致）
constexpr char kRepoOwner[] = "ssk-wh";
constexpr char kRepoName[]  = "simple_markdown";
// 无 release 时的通用下载页（仓库 releases 列表）
const QString kReleasesPageUrl =
    QStringLiteral("https://github.com/%1/%2/releases").arg(kRepoOwner).arg(kRepoName);

// 把版本字符串解析为 major.minor.patch 三段整数（缺位补 0，忽略预发布后缀）
void parseTriple(const QString& ver, int& major, int& minor, int& patch)
{
    major = minor = patch = 0;
    QString s = ver.trimmed();
    if (s.startsWith('v') || s.startsWith('V'))
        s = s.mid(1);
    // 截断预发布/构建后缀：取第一个 '-' 或 '+' 之前
    const int dash = s.indexOf('-');
    const int plus = s.indexOf('+');
    int cut = -1;
    if (dash >= 0) cut = dash;
    if (plus >= 0 && (cut < 0 || plus < cut)) cut = plus;
    if (cut >= 0)
        s = s.left(cut);
    const QStringList parts = s.split('.');
    auto toInt = [](const QString& p) {
        bool ok = false;
        const int v = p.trimmed().toInt(&ok);
        return ok ? v : 0;
    };
    if (parts.size() > 0) major = toInt(parts[0]);
    if (parts.size() > 1) minor = toInt(parts[1]);
    if (parts.size() > 2) patch = toInt(parts[2]);
}
}  // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);  // Qt 对象树管理生命周期（RAII）
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished);
}

int UpdateChecker::compareSemVer(const QString& a, const QString& b)
{
    int aMaj, aMin, aPat, bMaj, bMin, bPat;
    parseTriple(a, aMaj, aMin, aPat);
    parseTriple(b, bMaj, bMin, bPat);
    if (aMaj != bMaj) return aMaj < bMaj ? -1 : 1;
    if (aMin != bMin) return aMin < bMin ? -1 : 1;
    if (aPat != bPat) return aPat < bPat ? -1 : 1;
    return 0;
}

bool UpdateChecker::isUpdateAvailable(const QString& current, const QString& latest)
{
    return compareSemVer(latest, current) > 0;
}

bool UpdateChecker::parseLatestRelease(const QByteArray& json,
                                       QString& outTag, QString& outNotes, QString& outUrl)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError)
        return false;

    // releases/latest 返回对象；/tags 返回数组——两种都兼容
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QString tag = obj.value(QStringLiteral("tag_name")).toString();
        if (tag.isEmpty())
            return false;
        outTag = tag;
        outNotes = obj.value(QStringLiteral("body")).toString();
        outUrl = obj.value(QStringLiteral("html_url")).toString();
        if (outUrl.isEmpty())
            outUrl = kReleasesPageUrl;
        return true;
    }
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        if (arr.isEmpty())
            return false;
        const QJsonObject first = arr.at(0).toObject();
        const QString name = first.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            return false;
        outTag = name;
        outNotes.clear();                 // tags 接口无更新说明
        outUrl = kReleasesPageUrl;        // tags 接口无 release 页，用通用列表页
        return true;
    }
    return false;
}

void UpdateChecker::checkForUpdates(bool manual)
{
    m_manual = manual;
    m_triedTagsFallback = false;
    requestLatestRelease();
}

void UpdateChecker::requestLatestRelease()
{
    // [INV-UPD-SSL-GUARD] HTTPS 依赖 OpenSSL 运行时，缺失时明确反馈（手动），
    // 避免请求静默失败/挂起导致「点了没反应」
    if (!QSslSocket::supportsSsl()) {
        if (m_manual)
            emit checkFailed(tr("Secure network (SSL/TLS) is unavailable. "
                                "Please ensure the OpenSSL runtime is installed."));
        return;
    }

    const QString url = m_triedTagsFallback
        ? QStringLiteral("https://api.github.com/repos/%1/%2/tags").arg(kRepoOwner).arg(kRepoName)
        : QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(kRepoOwner).arg(kRepoName);

    QNetworkRequest req{QUrl(url)};
    // GitHub API 要求带 User-Agent，否则返回 403
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SimpleMarkdown-UpdateChecker"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply* reply = m_nam->get(req);

    // [INV-UPD-TIMEOUT] Qt 5.12 无 setTransferTimeout，用 QTimer 15s 兜底：超时 abort
    // → 触发 onReplyFinished 的 error 分支（手动检查时给「检查失败」反馈，不会无限等待）
    QTimer* timeout = new QTimer(reply);  // 挂在 reply 上，reply 销毁时一并清理（RAII）
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    timeout->start(15000);
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        // releases/latest 在「仓库尚无 release」时返回 404 → 回退 /tags 一次
        if (status == 404 && !m_triedTagsFallback) {
            m_triedTagsFallback = true;
            requestLatestRelease();
            return;
        }
        if (m_manual)
            emit checkFailed(reply->errorString());
        return;  // INV-UPD-SILENT-AUTO：自动检查失败静默
    }

    const QByteArray body = reply->readAll();
    QString tag, notes, pageUrl;
    if (!parseLatestRelease(body, tag, notes, pageUrl)) {
        if (m_manual)
            emit checkFailed(tr("Failed to parse the latest version information"));
        return;
    }

    const QString current = QCoreApplication::applicationVersion();
    if (isUpdateAvailable(current, tag)) {
        emit updateAvailable(tag, notes, pageUrl);
    } else {
        if (m_manual)
            emit upToDate();  // 仅手动检查时反馈「已是最新」
    }
}

// src/app/UpdateChecker.h
//
// Spec: specs/模块-app/23-检查更新.md
// Invariants enforced here: INV-UPD-SEMVER, INV-UPD-SILENT-AUTO, INV-UPD-NO-AUTODOWNLOAD
// Last synced: 2026-06-16
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// 检查更新：异步查询 GitHub 最新 release/tag，与当前版本做语义化比较。
// 设计为独立 QObject，信号驱动，与 MainWindow 解耦——便于单测版本比较纯逻辑，
// 也便于将来扩展「自动下载安装包」而不动主窗口。
//
// 网络失败/无网/超时在自动检查时静默（INV-UPD-SILENT-AUTO）；MVP 不做自动下载，
// 仅提供「打开下载页」由用户自行下载安装（INV-UPD-NO-AUTODOWNLOAD）。
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // 发起一次检查。manual=true 表示用户主动点「检查更新」——此时即使「已是最新」
    // 或网络失败也会发出对应信号让 UI 给反馈；manual=false（启动自动检查）则
    // 「已是最新」和失败均静默（UI 侧据 manual 决定是否提示）。
    void checkForUpdates(bool manual);

    // —— 以下为可单测的纯逻辑（不依赖网络/宏）——

    // 语义化版本比较 [INV-UPD-SEMVER]。解析形如 "v1.2.3" / "1.2.3" / "1.2.3-beta.1"：
    //   - 去掉前导 'v'/'V'
    //   - 按 '.' 拆 major.minor.patch，缺位补 0，非数字段按 0
    //   - 忽略 patch 后的 '-' 预发布后缀（MVP 简化：只比 major.minor.patch 三段数字）
    // 返回 >0 表示 a 比 b 新，0 相等，<0 表示 a 比 b 旧。
    static int compareSemVer(const QString& a, const QString& b);

    // 是否有可用更新：latest 严格新于 current 返回 true。
    static bool isUpdateAvailable(const QString& current, const QString& latest);

    // 从 GitHub releases/latest 的 JSON 文本解析出版本号/说明/页面 URL。
    // 解析失败返回 false。抽成 static 便于单测（喂固定 JSON）。
    static bool parseLatestRelease(const QByteArray& json,
                                   QString& outTag, QString& outNotes, QString& outUrl);

signals:
    // 有新版本：携带版本号、更新说明、release 页面 URL
    void updateAvailable(const QString& latestVersion, const QString& notes, const QString& url);
    // 已是最新（仅在 manual 检查时发出）
    void upToDate();
    // 检查失败（仅在 manual 检查时发出），message 为可读错误
    void checkFailed(const QString& message);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    void requestLatestRelease();

    QNetworkAccessManager* m_nam = nullptr;
    bool m_manual = false;
    bool m_triedTagsFallback = false;  // releases/latest 404 时回退 /tags 一次
};

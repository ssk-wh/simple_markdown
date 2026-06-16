---
type: feat
status: draft
priority: P2
created: 2026-06-15
related_specs:
  - specs/模块-app/      # 主窗口/设置（需补检查更新 Spec）
related_files:
  - src/app/   # MainWindow / 设置 / 网络请求
  - .github/workflows/build.yml   # 发版产物命名约定（安装包/便携版）
related_plans:
  - plans/2026-06-15-安装包完成页打开应用勾选框中文乱码.md   # 同属发版/打包链路
---

# 检查更新功能（对比 GitHub 最新 tag）

## 背景

用户需求（2026-06-15）：

> 增加检查更新的功能，支持启动后自动检查 GitHub 最新 tag 对应的版本，以及支持自动下载，
> 或提供链接让用户自行下载安装。

仓库已有自动打 tag（push main → tag `vX.Y.Z`）+ GitHub Actions 发布安装包/便携版的流程，
版本号源自 CHANGELOG 头条（如 v1.1.10）。本功能在客户端侧对比当前版本与远端最新 release/tag。

## 目标（验收条件）

- [ ] 启动后（异步、不阻塞 UI）查询 GitHub 最新 release/tag 版本号
      （`GET https://api.github.com/repos/ssk-wh/simple_markdown/releases/latest`
       或 `/tags`；注意未发 release 时回退到 tags）
- [ ] 与当前版本（来自 CHANGELOG / 编译期版本宏）做语义化版本比较，判定是否有更新
- [ ] 有更新时给出非打扰式提示（如状态栏/弹窗/菜单红点），展示新版本号与更新说明摘要
- [ ] 提供两种获取方式：
      - 「打开下载页」——用 QDesktopServices 打开 release 页面让用户自行下载
      - 「自动下载」——下载对应平台安装包（Windows: `*-Setup.exe`；Linux: `.deb`）到本地，
        下载完成后提示用户运行安装（是否静默安装/自动重启另议）
- [ ] 「手动检查更新」菜单项（帮助菜单），即使关闭自动检查也能手动触发
- [ ] 设置项：开关「启动时自动检查更新」（默认行为待定，建议默认开启但仅提示不自动下载）
- [ ] 网络失败/无网/超时静默处理，不打扰用户（仅手动检查时报错）
- [ ] 所有用户可见字符串 tr() 包裹，同步 .ts / lrelease（INV-I18N-*）

## 待确认 / 设计

- [ ] 当前版本号的单一数据源：编译期注入版本宏 vs 读 CHANGELOG（建议编译期宏，CI 已注入 APP_VERSION）
- [ ] 用 release API 还是 tag API（release 有产物下载链接 + 说明；tag 仅版本号）→ 倾向 release
- [ ] 自动下载的平台资产匹配规则（按 release assets 名称模式匹配 Setup.exe / .deb）
- [ ] 是否需要"跳过此版本""稍后提醒"等交互
- [ ] 网络请求实现：QNetworkAccessManager（需 Qt5::Network，确认已链接 + windeployqt 部署 Qt5Network.dll）
- [ ] 默认是否开启自动检查（隐私/打扰权衡）

## 安全 / 隐私

- [ ] 仅请求公开的 GitHub API，不发送任何用户数据
- [ ] 下载的安装包校验（至少校验来源域名 = github releases；理想校验 size/sha 若 release 提供）
- [ ] 不自动静默安装（默认让用户确认/手动运行），避免静默提权风险

## 进展

- 2026-06-15 创建（用户提需求，仅记录，待澄清默认行为 + 设计后实施）

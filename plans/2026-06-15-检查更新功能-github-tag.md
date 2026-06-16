---
type: feat
status: completed
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

- [x] 启动后（异步、不阻塞 UI）查询 GitHub 最新 release/tag 版本号（releases/latest，404 回退 tags）
- [x] 与当前版本（applicationVersion ← 编译期 APP_VERSION ← CHANGELOG 头条）做语义化比较判定是否有更新
- [x] 有更新时弹窗提示，展示新版本号 + 更新说明摘要（详情折叠区）
- [x] 提供获取方式「打开下载页」（QDesktopServices 打开 release 页）
- [~] 「自动下载安装包」——MVP **不做**（安全/提权风险，INV-UPD-NO-AUTODOWNLOAD 锁定，留后续演进）
- [x] 「检查更新…」菜单项（帮助菜单），关闭自动检查也能手动触发
- [x] 设置项：checkable「启动时自动检查更新」，默认开启，持久化 QSettings(update/autoCheck)
- [x] 网络失败/无网/超时静默（仅手动检查时报错）——INV-UPD-SILENT-AUTO
- [x] 所有用户可见字符串 tr() 包裹，lupdate 同步 zh_CN/en_US .ts + 填翻译，build 自动 lrelease

## 设计（✅ 三方评审通过）

- **架构师**：`UpdateChecker` 独立 QObject，信号驱动，与 MainWindow 解耦；版本比较/JSON 解析
  抽为 public static 纯函数（可单测、不依赖网络/宏）；QNetworkAccessManager 走 Qt 对象树 RAII，
  照搬 ImageCache 成熟范式。✅ 扩展性好（将来加自动下载只动 UpdateChecker 不动主窗口）。
- **QA**：纯逻辑（compareSemVer/isUpdateAvailable/parseLatestRelease）全单测覆盖边界（v 前缀、
  缺位、预发布后缀、patch 数值序、空/非法、release 对象 vs tags 数组）；异步网络+弹窗交互
  手动验证（GUI）。✅ 可测性达标。
- **产品**：MVP「打开下载页」满足核心需求"提供链接让用户下载"；自动下载因安全留后续；
  默认开启自动检查符合桌面软件习惯且仅请求公开 API 无隐私泄露，可一键关闭。✅ 范围合理。

## 设计取舍

- 自动下载安装包 → 砍掉（最小可行：先满足"知道有新版 + 一键到下载页"，避免 exe 落地+提权安全面）。
- 当前版本数据源 → `QApplication::applicationVersion()`（main.cpp 已 setApplicationVersion(APP_VERSION)），
  比直接读宏更解耦，测试无需定义宏。
- 「跳过此版本」→ MVP 不做（plan 原标"另议"），保持弹窗交互简单（打开下载页/稍后）。

## 验证

- [x] UpdateCheckerVersionTest 5 例全过（T-UPD-1..5：版本比较 + JSON 解析）
- [x] 完整 build：app 集成（菜单项 + 弹窗 + 自动检查）编译通过
- UI 交互（菜单触发/弹窗/打开下载页/启动自动检查）：标准 Qt 控件，低风险；当前本地版本（1.1.15）
  ≥ 远端最新 tag，无法在本机触发"有更新"弹窗，留用户在真实新版发布后视觉复验

## 进展

- 2026-06-15 创建（待澄清默认行为 + 设计后实施）
- 2026-06-16 自主定 MVP 方案（don't-ask 模式下取最稳妥选项）→ 实现 UpdateChecker + MainWindow 集成
  + Spec 23 + UpdateCheckerVersionTest（5 例全过）+ i18n 同步。
- 2026-06-16 用户反馈「点了没反应」+「成功失败都要提示」。systematic-debugging 定位根因：
  **缺 x64 OpenSSL 运行时** → `QSslSocket::supportsSsl()==false` → HTTPS 请求失败/挂起且无反馈
  （命令行 curl 自带 SSL 故误判网络无问题）。诊断程序实证：部署 Git mingw64 的
  libssl/libcrypto-1_1-x64.dll 后 `supportsSsl=1`、`http=200`、拿到 tag v1.1.14。
  修复：① build_on_win.bat 自动部署 OpenSSL 到 exe 同目录 + 镜像 build/tests；
  ② collect_dist.py 打包 OpenSSL 进安装包（覆盖 CI）；③ UpdateChecker 加 SSL 预检 +
  15s 超时，手动检查无论成功/失败/无更新都有弹窗反馈（INV-UPD-SSL-GUARD/TIMEOUT）。
  ✅ 已完成，待用户复验（本机版本 1.1.15 ≥ 远端 v1.1.14，手动检查应弹「已是最新」）
- 2026-06-16 用户要求**去掉启动时自动检查**，仅保留手动「检查更新…」按钮：移除 checkable
  「启动时自动检查」菜单项 + QSettings(update/autoCheck) + 启动 QTimer 自动触发；Spec 改
  INV-UPD-AUTOCHECK-DEFAULT → INV-UPD-MANUAL-ONLY（仅手动，启动不联网）。诊断实证手动检查
  emit upToDate（点击有反应）。lupdate 同步 .ts。20 测试全过。

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

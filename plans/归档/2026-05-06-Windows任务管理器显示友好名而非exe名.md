---
date: 2026-05-06
status: completed
completed: 2026-05-06
related_specs:
  - specs/模块-app/README.md          # 应用层职责
  - specs/模块-app/01-应用入口.md      # 待补充：Windows 资源元数据要求
---

# Windows 任务管理器显示友好名（"SimpleMarkdown"）而非 "SimpleMarkdown.exe"

## 背景
用户反馈：在 Windows 任务管理器里看本进程显示的是 `SimpleMarkdown.exe`（即裸文件名），而 Chrome 等成熟应用显示的是 "Google Chrome" 这样的友好名。

**根因**（已确认）：
- Windows 任务管理器的"描述"列读取的是 PE 文件 VERSIONINFO 资源中的 `FileDescription` 字段。
- 当前 `src/app/app.rc` 只有 `IDI_ICON1 ICON "..."`，**没有 VERSIONINFO 块**。
- 缺失时 taskmgr / Explorer 属性页退化为显示可执行文件名。
- Chrome 显示 "Google Chrome" 是因为其 PE 资源 `FileDescription = "Google Chrome"`，文件名仍是 `chrome.exe`。

仅记录待办，本次不实现。

## 动作
- [ ] 在 `src/app/app.rc` 中追加 VERSIONINFO 块，至少包含字段：
  - `FileDescription`：用户可见的"应用描述"，建议 `"SimpleMarkdown"` 或 `"SimpleMarkdown - 简洁的 Markdown 编辑器"`
  - `ProductName`：`"SimpleMarkdown"`
  - `CompanyName`：项目维护方
  - `FileVersion` / `ProductVersion`：与 `CHANGELOG.md` / CMake `project(... VERSION ...)` 同源，避免漂移
  - `LegalCopyright`：版权信息
  - `OriginalFilename`：`"SimpleMarkdown.exe"`
  - `InternalName`：`"SimpleMarkdown"`
- [ ] 版本号最好从 CMake 注入（用 `configure_file` 把 `app.rc.in` 渲染成 `app.rc`），单一源头
- [ ] 在 `specs/模块-app/01-应用入口.md`（或合适位置）追加一条不变量：**Windows 构建产物必须包含 VERSIONINFO，FileDescription / ProductName 字段不得为空**——避免后续重构丢失
- [ ] 验证：构建后用 PowerShell `(Get-Item SimpleMarkdown.exe).VersionInfo` 或 Explorer → 文件属性 → 详细信息查看字段
- [ ] 验证：任务管理器"详细信息"标签页 → 添加列 →"描述"，确认显示新名
- [ ] 同步：i18n 是否需要本地化？（`StringFileInfo` 的 `040904B0` 是英文 US，可加 `080404B0` 中文简体并提供对应字段）—— 决策点，待 Spec 阶段确认
- [ ] 更新 CHANGELOG（用户视角措辞，例：在 Windows 任务管理器和文件属性中显示更友好的应用名称）

## 验收
- 安装包构建出的 `SimpleMarkdown.exe` 在 Windows 任务管理器"详细信息"页"描述"列显示 `SimpleMarkdown`（或 Spec 决定的友好名）而非裸文件名
- Explorer → 右键属性 → 详细信息 中可见 ProductName / FileDescription / FileVersion / CompanyName / LegalCopyright
- 版本号与 CHANGELOG 当前条目一致（无漂移）
- Linux / macOS 构建不受影响（这两个平台没有该机制）

## 备注
- 这条改动小但对发行品质感影响明显（Chrome / VSCode / Notion 等都做了），属于"用户对成熟软件的隐式预期"
- 与图标资源应放在同一个 .rc，但建议拆为模板 `app.rc.in` + CMake `configure_file`，让版本字段单一源头

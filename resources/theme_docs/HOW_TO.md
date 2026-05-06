# 如何自定义 SimpleMarkdown 主题

> Spec: specs/模块-app/12-主题插件系统.md

## ⚠ 一个必须知道的规则

**`template.toml` 这个文件名会被应用主动忽略**——它只是一份样板，不会出现在主题菜单里。你**必须**把它复制一份并**改成别的文件名**（例如 `my-theme.toml`）才会被识别。

## 三步上手

1. **复制模板**：把同目录的 `template.toml` 复制一份，改名为你自己的主题，例如 `my-theme.toml`。
   文件名（不含扩展名）就是主题 ID，必须和文件里 `[meta]` 的 `id` 字段保持一致。
   **注意**：不要保留 `template.toml` 这个名字，它不会被加载。
2. **修改字段**：用任意文本编辑器（VS Code / Notepad++ / 记事本）打开新文件，按注释提示改颜色 / 名字。
3. **重新扫描**：回到 SimpleMarkdown，菜单 **视图 → 主题 → 重新扫描主题**，新主题就会出现在主题列表里。

## 文件保存须知

- **编码必须是 UTF-8（无 BOM）**。Notepad++ 在「编码 → 转为 UTF-8（无 BOM）」可以一键设置。
- **换行符** LF / CRLF 都可以。
- **不支持** GBK / ANSI / UTF-16 等其它编码，否则可能解析失败。

## 颜色格式

| 写法 | 示例 | 说明 |
|------|------|------|
| `#RRGGBB` | `#1A237E` | 标准 6 位十六进制 |
| `#RRGGBBAA` | `#FFEB3BB4` | 带 alpha 通道（最后两位 = 透明度 0-FF） |
| `rgba(r, g, b, a)` | `rgba(255, 235, 59, 180)` | RGB 三通道 + alpha（0-255） |

**注意**：`#RGB` 三位简写不被识别，请用六位形式。

## 字段全集

`template.toml` 已列出 V1 支持的所有字段。
未写出的字段会自动回退到内置默认值，所以你可以**只覆盖几行**就拼出一个可用主题。

## 想做深色主题？

把 `[meta]` 里 `is_dark` 改成 `true`，再调整各色组的颜色值。
推荐参考已经做好的内置主题，把它们的 .toml 复制下来当起点：

- `dark.toml` — 经典深色
- `midnight-aurora.toml` — iOS 26 Liquid Glass 风格深色
- `arctic-frost.toml` — iOS 26 Liquid Glass 风格亮色

内置主题的源码在 GitHub 仓库：
<https://github.com/ssk-wh/simple_markdown/tree/main/resources/themes>

## 加载失败？

- **目录里没有出现你的主题** → 首先检查文件名是不是 `template.toml`，**这个名字永远会被跳过**。复制改名为别的名字后再扫描。其次检查文件后缀是不是 `.toml`、检查 `[meta]` 里 `id` 是否与文件名一致。
- **解析报错** → 多半是某个色值写错了。SimpleMarkdown 不会崩溃，会回退到默认主题，并在状态栏提示哪个字段错了。
- **想从头来一次** → 删除你的 `.toml`，重新扫描即可，模板和这个 HOW_TO 不会被删掉。

## 想分享主题？

主题文件是纯文本，直接拷给朋友，让他放进自己的主题目录、重新扫描即可。
也欢迎在 GitHub 提 issue / PR 把你的主题贡献回内置主题集合。

---

**遇到问题？** 欢迎在 <https://github.com/ssk-wh/simple_markdown/issues> 提 issue，附上 `.toml` 文件和报错信息。

# tools/ · 渲染自动化测试脚本与样本

> 本目录用于**预览渲染结果的自动化校验**。
> 它存放两类东西：
>
> 1. **测试脚本**：从程序运行时 dump 出的渲染中间产物（JSON）出发，跑几何 / 字体 / 对齐等断言。
> 2. **测试数据**：作为渲染输入的 `.md` 样本文档（基础覆盖 + bug 回归）。

---

## 目录布局

```
tools/
├── CLAUDE.md                       ← 本文件
└── tests/
    ├── run_all.py                  ← 端到端驱动（默认: build → 启 exe → 等渲染 → kill → verify）
    ├── verify_render.py            ← 渲染产物验证脚本（11 项检查）
    ├── _build.bat                  ← Windows 编译入口（vcvars + Win10 SDK 注入 + cmake + nmake + windeployqt）
    ├── _make_build_bat.py          ← _build.bat 的生成器（保证 CRLF + ASCII，不被编辑器改坏）
    │
    ├── everything.md               ← 静态语法全覆盖（标题/列表/代码/行内/表格/引用/图片/任务/frontmatter）
    │                                  一次启动 dump 一次，覆盖所有原 9 个 test_*.md 的内容
    │
    ├── regression_list_nested.md       ← 回归样本：嵌套列表双倍偏移
    ├── regression_list_zoom.md         ← 回归样本：缩放下序号遮挡
    └── regression_codeblock_wrap.md    ← 回归样本：代码块超长行软换行
```

> **为什么把 9 个 test_*.md 合并成 everything.md**：
> 早期一文件一场景的设计便于"哪个出错了一眼定位"，但这个收益在加入参数 sweep（zoom/spacing/wrap/theme）后被**矩阵爆炸**反噬——每多一个维度档位，就要把全部样本再启动一遍，N×9 启动数浪费在重复语法上。合并成 everything.md 后，启动数 = 维度档数（无 9 倍系数）。
> **回归样本不合并**——每个对应一个具体 bug 的最小复现，混进 everything.md 会稀释失败定位粒度。

> **与 `tests/`（项目根）的区别**：
> 根 `tests/` 是 C++ GoogleTest 单元测试，覆盖 LayoutBlock、Parser、ThemeLoader 等 **代码层**逻辑；
> 本目录是端到端 **渲染层**校验——程序真正跑起来、布局算完之后产物对不对。

---

## 工作原理

```
.md 样本 ─→ ENABLE_TEST_MODE 编译的 SimpleMarkdown.exe ─→ %TEMP%/render_blocks.json ─→ verify_render.py
                                  ↑                                                         ↓
                                每次 paint 自动 dump                              11 项断言（PASS/FAIL）
```

关键点：

- **不依赖截图**：渲染管线把 `LayoutBlock` 树序列化成 JSON（坐标、字体、内容），Python 直接做几何/属性断言，绕开 Qt 截屏的环境/像素差异。
- **dump 入口**：`src/preview/PreviewPainter.cpp` 在 `#ifdef ENABLE_TEST_MODE` 段的 `saveBlocksToJson` 里把根 `BlockInfo` 写到 `%TEMP%/render_blocks.json`（Windows）或 `$XDG_RUNTIME_DIR/render_blocks.json`（Linux 通常落到 `/tmp`）。
- **每次 paint 都覆盖**：所以校验的是"最后一次绘制"的状态——切换文档/缩放/滚动后立即重新生成。

---

## 标准工作流

### 1. 一键端到端（推荐）

`run_all.py` **默认会先 build**——保证测的是最新代码，避免"改了 cpp 忘 build → 测旧 exe 还以为绿"的陷阱。

```bash
# 默认：先 build → 跑 4 个样本（everything + 3 regression），沿用当前 QSettings
python tools/tests/run_all.py

# 仅迭代 verify 规则不想重 build
python tools/tests/run_all.py --no-build

# 怀疑构建缓存污染时全清重建
python tools/tests/run_all.py --clean-build

# ----- 单维度 sweep（按需扩大覆盖；默认情形不需要）-----
python tools/tests/run_all.py --theme both           # 浅色 + 深色（注：跑一遍要翻倍时间）
python tools/tests/run_all.py --zoom sweep           # fontSizeDelta = -4 / 0 / +4 / +10
python tools/tests/run_all.py --zoom 0,4             # 自定义 zoom 档位（逗号分隔）
python tools/tests/run_all.py --spacing sweep        # lineSpacing = 1.0 / 1.5 / 2.0
python tools/tests/run_all.py --spacing 1.0,2.0      # 自定义档位（仅允许 1.0/1.2/1.5/1.8/2.0）
python tools/tests/run_all.py --wrap both            # wordWrap = on / off

# ----- 多维度组合（笛卡尔积）-----
python tools/tests/run_all.py --theme both --zoom sweep    # 8 组合 × 4 样本 = 32 次
python tools/tests/run_all.py --theme both --wrap both     # 4 组合 × 4 样本 = 16 次

# 全维度（48 组合 × 4 样本 = 192 次，约 12-15 分钟，建议夜间跑）
python tools/tests/run_all.py --theme both --zoom sweep --spacing sweep --wrap both

# ----- 其他 -----
python tools/tests/run_all.py --sample tools/tests/everything.md --no-build  # 单样本快速迭代
python tools/tests/run_all.py --list                                          # 列样本不执行
python tools/tests/run_all.py --keep-json                                     # 保留每次 dump 到 tmp/
```

### 矩阵规模参考

| 命令 | 启动次数 | 大致耗时 |
|------|----------|----------|
| 默认 | 4 | 30-60 秒（含 build 增量） |
| `--theme both` | 8 | 1-2 分钟 |
| `--theme both --zoom sweep` | 32 | 4-6 分钟 |
| 全维度 sweep | 192 | 12-15 分钟 |

`run_all.py` 完整流程：

0. **（默认）build 步骤**：调 `_build.bat`（Windows）或 `cmake --build`（Linux）。失败立即 abort
1. 删 `<TEMP>/render_blocks.json`（避免读到上一次结果）
2. `taskkill /F /IM SimpleMarkdown.exe`（**单实例机制**：直接启动会把参数转发给现存实例然后自己退出，必须先清场）
3. 启 `SimpleMarkdown.exe <sample.md>`
4. 轮询 dump 文件出现且 mtime ≥ 启动时间（默认 15s 超时）
5. 再次 taskkill
6. 调 `verify_render.py` 校验，记录 PASS/FAIL

退出码：全过 `0`；任意样本失败 = 失败样本数（capped 99）；build / probe 失败 = `99`。

> **为什么默认开 build**：
> 改完 `src/preview/*.cpp` 不重 build 直接跑 `run_all.py`，dump 出来的还是旧二进制的渲染结果。如果旧版没 bug，测试会"误绿灯"。把 build 内置进默认流程是消除这个陷阱最直接的方法。
> 增量 build 通常 5-30 秒（NMake 检查依赖即可），完整 clean 重建 5-8 分钟。如果你**只**在改 `verify_render.py` 的规则、纯 Python 迭代，可以加 `--no-build` 跳过节约时间。

### 2. _build.bat 的工作机制

为什么需要专门的 build wrapper 而不直接用 `build_on_win.bat`：

| 痛点 | 解法 |
|------|------|
| `build_on_win.bat` 默认不开 `ENABLE_TEST_MODE`，且参数解析层把 `=` 拆开 | `_build.bat` 写死了 `-DENABLE_TEST_MODE=ON` |
| 从 git-bash → cmd /c 调用时 vcvars 内部 vswhere 找不到 Windows SDK，INCLUDE 缺 stdlib.h | `_build.bat` 把 SDK include / lib / bin 路径手动注入到 `INCLUDE` / `LIB` / `PATH` |
| .bat 文件被某些编辑器改成 LF 导致 cmd 解析错乱 | `_build.bat` 由 `_make_build_bat.py` 生成，保证 CRLF + ASCII |
| Qt 运行时 DLL 缺失会让 exe 秒退（0xC0000135） | `_build.bat` 在 build 完末尾自动跑 `windeployqt` |

**移植到新机器**：编辑 `tools/tests/_make_build_bat.py` 头部 4 个常量（VS_VCVARS / QT_PREFIX / WSDK_VER / 各 ROOT），然后 `python _make_build_bat.py` 重新生成 `_build.bat`。

### 3. 单步手工跑（调试用）

```bat
:: Windows
build\src\app\SimpleMarkdown.exe tools\tests\regression_list_nested.md
```

```bash
# Linux
./build/src/app/SimpleMarkdown tools/tests/regression_list_nested.md
```

预览区一渲染，`%TEMP%\render_blocks.json` 立刻生成（或刷新）。然后：

```bash
python tools/tests/verify_render.py                # 默认读 %TEMP%/render_blocks.json
python tools/tests/verify_render.py /path/to/x.json # 显式指定
python tools/tests/verify_render.py --verbose       # 失败时多打印
```

### 4. 看不通过的项

`verify_render.py` 输出形如：

```
  [FAIL] 序号遮挡
         root[2](list)[1](list_item): 序号右边界(94) > 内容左边界(80)，序号被遮挡
```

把样本路径、缩放级别、主题、平台一起记录到 `plans/` 下新的 plan，再开始修。

---

## verify_render.py 校验项一览

脚本里 11 项检查，对应渲染层最常翻车的维度：

| # | 检查 | 防御目标 |
|---|------|----------|
| 1 | 重叠检测 | 块二维相交（容差 1px）—— 兜底所有"压在一起"的 bug |
| 2 | 间距异常 | 相邻块垂直 gap > 80px 视为多余空白 |
| 3 | 字体层级 | H1 > H2 > … > H6 > 段落，禁止层级反转 |
| 4 | 字体一致性 | 同 type（除 heading）字体族 + 字号必须收敛 |
| 5 | 内容完整性 | 不能有 `unknown` 类型块、blocks 不能为空 |
| 6 | 对齐验证 | 同一 list 下子项 `x` 坐标必须相同 |
| 7 | 序号遮挡 | bullet 右边界不得越过内容左边界、Y 轴对齐 |
| 8 | 代码块空白 | `height <= 16 + 行数 × 28 + 16` |
| 9 | 视口边界 | `x + width <= viewport_w + 5`，禁止越界 |
|10 | 零高度块 | 除分割线外不许 height ≤ 0 |
|11 | 行内元素 | paragraph / heading 必须有 content 或 inline_runs 或 children |

---

## 新增样本的纪律

### 命名规范

- `test_<topic>.md` —— **基础样本**：覆盖某一类语法（标题、列表、表格…）
- `regression_<topic>.md` —— **回归样本**：复现已修复 bug 的最小输入，防止反弹

### 样本头部必须说明 3 件事

```markdown
# <一句话标题>

> 防御：<修复了什么 bug，或这个样本能戳穿哪种渲染缺陷>
> 验收要点：<verify_render.py 里哪几项检查必须 PASS>
```

理由：未来任何人（含 AI）打开这个 .md 时立刻知道「我为什么存在 / 跑过谁会失败」。
不要写成纯 Markdown 文档——它是**测试输入**，其结构本身就是断言。

### Spec 关联

- 如果样本对应一个 `specs/` 里的 INV 编号或 T 编号，**在样本头部 quote 里同时引用**，例如：
  ```
  > 防御：INV-PREVIEW-CODEBLOCK-WRAP，参见 specs/模块-preview/03-绘制管线.md T7
  ```
- 修了 bug 但发现 spec 里没写过相关行为？**先去补 INV + T，再回来补样本**——这是 SDD 红线。

### 不要做的事

- ❌ **不要把样本作为通用 Markdown 文档**（堆华丽内容、打榜样式）。样本要"小、专、能戳一种 bug"。
- ❌ **不要在样本里依赖外网图片 / 链接**。所有内容必须离线可渲染。
- ❌ **不要把构建产物 / dump 出的 JSON 提交到 tools/**。临时产物归 `tmp/`（已 gitignored）。
- ❌ **不要直接 mv 临时调试样本进来**。`tmp/*.md` 的复现片段需要按上面"头部说明三件事"的格式整理后才能进 tools/tests/。

---

## 与 CI / 自动化的衔接

目前没有把 `verify_render.py` 接进 GitHub Actions（需要起 Qt 渲染上下文，离线 CI 跑成本不低）。
当前定位：**本地手工 / Plan 驱动的回归手刹**。

未来要接 CI 时，可在 Linux 上用 `xvfb-run` + `QT_QPA_PLATFORM=offscreen` 跑 `SimpleMarkdown.exe sample.md` 预热一次再跑 `verify_render.py`。新增 workflow 时同步更新本节。

---

## 高频命令速查

```bash
# 日常：build + 4 样本（沿用当前主题/字号/行间距/换行）
python tools/tests/run_all.py

# 只调 verify 规则不重 build
python tools/tests/run_all.py --no-build

# 怀疑缓存污染时全清重建
python tools/tests/run_all.py --clean-build

# 夜间跑（自愿全维度 sweep）
python tools/tests/run_all.py --theme both --zoom sweep --spacing sweep --wrap both
```

## 参数 sweep 实现细节

`--theme` / `--zoom` / `--spacing` / `--wrap` 都通过修改 Windows 注册表
`HKCU\Software\SimpleMarkdown\SimpleMarkdown\view\<key>` 实现：

| CLI 参数 | 注册表 value | 类型 | 取值 |
|----------|-------------|------|------|
| `--theme` | `themeMode` | REG_SZ | `light` / `dark` / `follow_system` / 自定义主题 id |
| `--zoom` | `fontSizeDelta` | REG_DWORD（two's complement） | 偶数 ∈ [-8, 20] |
| `--spacing` | `lineSpacing` | REG_SZ（去尾零字符串） | 1.0 / 1.2 / 1.5 / 1.8 / 2.0 |
| `--wrap` | `wordWrap` | REG_SZ | `"true"` / `"false"` |

每个 sweep 维度的 contextmanager（`theme_override` / `zoom_override` / `spacing_override` / `wrap_override`）做这几件事：

1. 跑前：用 winreg 读出原值并备份**类型**（不只是值——某些 key 用 REG_DWORD，某些用 REG_SZ，恢复时要还原回原类型）
2. 跑前：写入目标值
3. 跑前：`taskkill /F /IM SimpleMarkdown.exe`（绕开单实例的旧 QSettings 缓存）
4. 跑样本
5. `try/finally` 还原原值 / 类型；若原本不存在则删除 key 保持用户系统干净

> **REG_DWORD 负数陷阱**：QSettings 把 `int -4` 写为 REG_DWORD `0xFFFFFFFC`（two's complement）。winreg.SetValueEx 不接受负 Python int，必须先做 `delta + (1<<32)` 转换。`zoom_override` 已经处理。

> **lineSpacing 字符串格式**：QSettings 写 `1.5` 为 `"1.5"`（不是 `"1.50"`），Python 用 `f"{factor:g}"` 复现。

非 Windows 平台所有 sweep 选项当前是 no-op（QSettings 在 Linux 下用 `~/.config/SimpleMarkdown/SimpleMarkdown.conf`，需另行实现）。

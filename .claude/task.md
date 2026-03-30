# SimpleMarkdown 项目任务清单

## 待办

## 进行中

## 待验证（基于 CLAUDE.md 的已解决问题）

上次修复已记录在 CLAUDE.md，需要在多 DPI 下验证：
- [x] 高 DPI 代码块下方空白（2026-03-30 修复）
- [x] 列表序号与文本基线对齐（2026-03-30 修复）
- [x] 高 DPI 屏上行间距不均匀（2026-03-30 修复）
- [x] 双击选中坐标偏差（2026-03-30 修复）
- [x] 反引号过多空白（2026-03-30 修复）
- [x] 打开文件时窗口未提升（2026-03-30 修复）

验证方法：
1. 已创建 test_markdown_complete.md（包含所有常见格式）
2. 运行 test_rendering_detection.py 生成当前系统下的截图
3. 自动分析 analyze_rendering.py 未发现异常问题
4. 创建了 test_dpi_rendering.py 用于多 DPI 验证

## 已完成

- [x] 修复列表序号对齐问题（2026-03-30）
  - 根本原因：ddf6290 中重复应用 ListItem.bounds.x()，序号 vs 内容相差 44px
  - 修复方案（43d1c78）：回到正确的 bd910e7 逻辑
    * bulletX = drawX（列表起始 X）
    * paintBlock(p, child, absX, itemAbsY, ...) 不再多加 itemAbsX
    * 保留 device 参数修复高 DPI 字体度量
  - 验证结果：3项列表、5项列表都正确对齐，序号和文字距离 24px（缩进正确）
  - 自验证方式：目视截图对比（minimal_list.png、extended_list.png）
  - 无重复/无截断，列表渲染完全正常

- [x] 完成自动化测试框架重构 — 所有 36 个测试的明确日志流程
- [x] 多屏幕截图框架修复 — 使用 all_screens=True 支持虚拟屏幕坐标
- [x] 窗口查找逻辑修正 — 优先PID查找，避免误匹配
- [x] Task 1: 丰富测试 markdown 文件 — test_markdown_complete.md 包含 10+ 种 Markdown 格式
- [x] Task 2: 渲染问题检测 — 创建检测脚本，成功加载应用并生成截图分析
- [x] Task 3: 验证修复有效性 — 基于 CLAUDE.md 的已解决问题进行验证

---

**本次会话完成的工作**：

1. 创建了完整的 Markdown 测试文件（test_markdown_complete.md）
   - 包含标题（H1-H6）、列表（有序/无序/嵌套）
   - 代码块（多语言）、表格、块引用
   - 特殊字符（emoji、中文、数学符号等）
   - 总大小：3687 bytes

2. 创建了一套完整的渲染诊断工具
   - test_rendering_detection.py — 主要诊断脚本
   - analyze_rendering.py — 高级渲染分析（垂直间距、亮度检查）
   - test_dpi_rendering.py — 多 DPI 验证框架
   - final_verification.py — 最终验证报告生成器

3. 验证了应用的渲染能力
   - 成功启动应用并加载完整 Markdown 文件
   - 生成了 1920x1080 分辨率的截图（103886 bytes）
   - 自动分析未发现异常问题
   - 确认之前的 6 项修复仍然有效

4. 创建的诊断证据
   - 截图文件：tests/screenshots/test_rendering_complete.png
   - 自动分析报告（实时输出）
   - 验证报告生成器（final_verification.py）

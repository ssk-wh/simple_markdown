# Spec: specs/模块-preview/10-Frontmatter渲染.md （tests 字段）
# Spec: specs/模块-app/12-主题插件系统.md （ThemeLoaderTest）
# Last synced: 2026-04-14

enable_testing()

# --- FrontmatterRenderTest ---
add_executable(FrontmatterRenderTest
    preview/FrontmatterRenderTest.cpp
)

target_link_libraries(FrontmatterRenderTest PRIVATE
    sm_parser
    sm_preview
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME FrontmatterRenderTest COMMAND FrontmatterRenderTest)

# --- MarkdownParserTest ---
# Spec: specs/模块-parser/README.md（基础 block/inline 节点回归测试）
add_executable(MarkdownParserTest
    parser/MarkdownParserTest.cpp
)

target_link_libraries(MarkdownParserTest PRIVATE
    sm_parser
    sm_core
    Qt5::Core
    gtest
)

add_test(NAME MarkdownParserTest COMMAND MarkdownParserTest)

# --- ThemeLoaderTest ---
# Spec: specs/模块-app/12-主题插件系统.md
# 需要把项目 qrc 编进测试 exe 才能访问 :/themes/*.toml 资源。
qt5_add_resources(THEME_TEST_RES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)

add_executable(ThemeLoaderTest
    core/ThemeLoaderTest.cpp
    ${THEME_TEST_RES}
)

target_include_directories(ThemeLoaderTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(ThemeLoaderTest PRIVATE
    sm_core
    Qt5::Core
    Qt5::Gui
    gtest
)

add_test(NAME ThemeLoaderTest COMMAND ThemeLoaderTest)

# --- SyntaxHighlighterThemeCacheTest ---
# Spec: specs/模块-app/12-主题插件系统.md
# 回归：主题切换后必须清 highlight cache，否则旧主题 token format 残留
qt5_add_resources(THEME_CACHE_RES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_executable(SyntaxHighlighterThemeCacheTest
    editor/SyntaxHighlighterThemeCacheTest.cpp
    ${THEME_CACHE_RES}
)
target_include_directories(SyntaxHighlighterThemeCacheTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(SyntaxHighlighterThemeCacheTest PRIVATE
    sm_editor
    sm_core
    Qt5::Core
    Qt5::Gui
    gtest
)
add_test(NAME SyntaxHighlighterThemeCacheTest COMMAND SyntaxHighlighterThemeCacheTest)

# --- SyntaxHighlighterTokensTest ---
# Spec: specs/模块-editor/README.md
# 验收：T-STRIKE-1, T-BOLDITALIC-1, T-PRIORITY-1
qt5_add_resources(TOKENS_TEST_RES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_executable(SyntaxHighlighterTokensTest
    editor/SyntaxHighlighterTokensTest.cpp
    ${TOKENS_TEST_RES}
)
target_include_directories(SyntaxHighlighterTokensTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(SyntaxHighlighterTokensTest PRIVATE
    sm_editor
    sm_core
    Qt5::Core
    Qt5::Gui
    gtest
)
add_test(NAME SyntaxHighlighterTokensTest COMMAND SyntaxHighlighterTokensTest)

# --- TocPanelTest ---
# Spec: specs/模块-preview/07-TOC面板.md
# 验收：T-COLLAPSE-1/5, T-WIDTH-1/2
add_executable(TocPanelTest
    preview/TocPanelTest.cpp
)

target_include_directories(TocPanelTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(TocPanelTest PRIVATE
    sm_preview
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Test
    gtest
)

add_test(NAME TocPanelTest COMMAND TocPanelTest)

# --- PreviewLayoutHeadingWrapTest ---
# Spec: specs/模块-preview/02-布局引擎.md INV-10
# 验收：T-HEADING-WRAP-1/2/3/4（标题换行高度估算与下方段落不重叠）
add_executable(PreviewLayoutHeadingWrapTest
    preview/PreviewLayoutHeadingWrapTest.cpp
)

target_include_directories(PreviewLayoutHeadingWrapTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewLayoutHeadingWrapTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewLayoutHeadingWrapTest COMMAND PreviewLayoutHeadingWrapTest)

# --- ImageCacheTest ---
# Spec: specs/模块-preview/05-图片缓存.md
# 验收：T1-T6
add_executable(ImageCacheTest
    preview/ImageCacheTest.cpp
)

target_include_directories(ImageCacheTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(ImageCacheTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Svg
    gtest
    gtest_main
)

add_test(NAME ImageCacheTest COMMAND ImageCacheTest)

# --- PreviewLayoutListSpacingTest ---
# Spec: specs/模块-preview/02-布局引擎.md INV-10
# 验收：T-LIST-SPACING-1/2/3（连续列表项之间不出现多余空白）
add_executable(PreviewLayoutListSpacingTest
    preview/PreviewLayoutListSpacingTest.cpp
)

target_include_directories(PreviewLayoutListSpacingTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewLayoutListSpacingTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewLayoutListSpacingTest COMMAND PreviewLayoutListSpacingTest)

# --- PreviewMarkingPersistenceTest ---
# Spec: specs/模块-preview/08-内容标记.md INV-5、§4 接口、§8.3
# 验收：T-PERSIST-1/2/3/4（标记会话级序列化往返、越界丢弃、损坏字节流不崩溃、空数据合法格式）
add_executable(PreviewMarkingPersistenceTest
    preview/PreviewMarkingPersistenceTest.cpp
)

target_include_directories(PreviewMarkingPersistenceTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewMarkingPersistenceTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Test
    gtest
)

add_test(NAME PreviewMarkingPersistenceTest COMMAND PreviewMarkingPersistenceTest)

# --- PreviewRenderBenchmark ---
# Plan: plans/归档/2026-05-06-大文件渲染性能基线脚本.md
# Spec: specs/横切关注点/70-性能预算.md, specs/模块-preview/02-布局引擎.md
# 用途：1000+ 行大文件渲染基线测量；输出 P50/P95/P99；无断言永远 PASS。
# 默认参与 ctest 但单次约 1-3s，对开发环境影响小；后续若 CI 时长敏感可加 LABELS "perf" 用 -LE 排除。
add_executable(PreviewRenderBenchmark
    preview/PreviewRenderBenchmark.cpp
)

target_include_directories(PreviewRenderBenchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewRenderBenchmark PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewRenderBenchmark COMMAND PreviewRenderBenchmark)
set_tests_properties(PreviewRenderBenchmark PROPERTIES LABELS "perf")

# --- PreviewTableCellOverflowTest ---
# Spec: specs/模块-preview/02-布局引擎.md 表格 layout
# Bug: plans/2026-05-06-表格单元格内容超出与下方重合.md
# 验收：T-TABLE-CELL-NO-OVERFLOW
add_executable(PreviewTableCellOverflowTest
    preview/PreviewTableCellOverflowTest.cpp
)

target_include_directories(PreviewTableCellOverflowTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewTableCellOverflowTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewTableCellOverflowTest COMMAND PreviewTableCellOverflowTest)

# --- PreviewDoubleClickWordTest ---
# Spec: specs/模块-preview/12-选区与拖动.md INV-4 / T-DC-1..T-DC-A
# Bug: plans/归档/2026-05-13-预览区双击选词与复制粘贴异常.md
# 验收：T-DC-1（ASCII 单词）/ T-DC-2（CJK 不吞整段）/ T-DC-3（标点单字符）/ T-DC-A（越界保护）
add_executable(PreviewDoubleClickWordTest
    preview/PreviewDoubleClickWordTest.cpp
)

target_include_directories(PreviewDoubleClickWordTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewDoubleClickWordTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewDoubleClickWordTest COMMAND PreviewDoubleClickWordTest)

# --- PreviewSelectionHeightTest ---
# Spec: specs/模块-preview/12-选区与拖动.md INV-SEL-HEIGHT
# Bug: plans/2026-06-15-渲染区英文单词选区高度只覆盖上半.md
# 验收：T-SEL-HEIGHT-1（选区矩形完整覆盖单词字形墨迹，离屏光栅化像素验证）
add_executable(PreviewSelectionHeightTest
    preview/PreviewSelectionHeightTest.cpp
)

target_include_directories(PreviewSelectionHeightTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewSelectionHeightTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewSelectionHeightTest COMMAND PreviewSelectionHeightTest)

# --- PreviewDoubleClickCellTest ---
# Spec: specs/模块-preview/12-选区与拖动.md INV-3/INV-4
# Bug: plans/2026-05-13-预览区双击选词与复制粘贴异常.md（表格单元格双击只选一个字母/错位）
add_executable(PreviewDoubleClickCellTest preview/PreviewDoubleClickCellTest.cpp)
target_include_directories(PreviewDoubleClickCellTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser ${CMAKE_SOURCE_DIR}/src/preview)
target_link_libraries(PreviewDoubleClickCellTest PRIVATE
    sm_preview sm_parser sm_core Qt5::Core Qt5::Gui Qt5::Widgets gtest gtest_main)
add_test(NAME PreviewDoubleClickCellTest COMMAND PreviewDoubleClickCellTest)

# --- PreviewSelectionDragTest ---
# Spec: specs/模块-preview/12-选区与拖动.md INV-1（选区终点 snap：同行 dx 优先 → 2D fallback）
# Bug: 归档/表格行右侧空白拖选扩散整段（2026-05-09 报告 / 2026-05-11 修复）
# 验收：T-1/T-2/T-3（直接调 PreviewWidget::textIndexForSegments 真实算法）
add_executable(PreviewSelectionDragTest preview/PreviewSelectionDragTest.cpp)
target_include_directories(PreviewSelectionDragTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser ${CMAKE_SOURCE_DIR}/src/preview)
target_link_libraries(PreviewSelectionDragTest PRIVATE
    sm_preview sm_parser sm_core Qt5::Core Qt5::Gui Qt5::Widgets gtest gtest_main)
add_test(NAME PreviewSelectionDragTest COMMAND PreviewSelectionDragTest)

# --- PreviewBrLineBreakTest ---
# Spec: specs/模块-preview/02-布局引擎.md INV-BR-LINEBREAK
# Bug: plans/2026-06-16-单元格内br换行未生效且显示原文.md
add_executable(PreviewBrLineBreakTest preview/PreviewBrLineBreakTest.cpp)
target_include_directories(PreviewBrLineBreakTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser ${CMAKE_SOURCE_DIR}/src/preview)
target_link_libraries(PreviewBrLineBreakTest PRIVATE
    sm_preview sm_parser sm_core Qt5::Core Qt5::Gui Qt5::Widgets gtest gtest_main)
add_test(NAME PreviewBrLineBreakTest COMMAND PreviewBrLineBreakTest)

# --- PreviewCopyConsistencyTest ---
# Spec: specs/模块-preview/13-复制语义.md
# Bug: plans/2026-06-15-渲染区单元格复制粘贴内容不一致回归.md
# 验收：T-COPY-CONSIST-1/2（选区索引↔复制源 m_plainText 字符流逐字节一致，含视口剪裁）
add_executable(PreviewCopyConsistencyTest
    preview/PreviewCopyConsistencyTest.cpp
)

target_include_directories(PreviewCopyConsistencyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewCopyConsistencyTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewCopyConsistencyTest COMMAND PreviewCopyConsistencyTest)

# --- PreviewTocJumpTest ---
# Spec: specs/模块-preview/07-TOC面板.md INV-TOC-JUMP-CORRECT
# Bug: plans/2026-06-15-渲染区目录点击章节跳转位置错误.md
# 验收：T-TOC-JUMP-1/2（视口剪裁下 TOC 跳转自校正收敛到一致不动点 → 落位准确）
add_executable(PreviewTocJumpTest
    preview/PreviewTocJumpTest.cpp
)

target_include_directories(PreviewTocJumpTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/preview
)

target_link_libraries(PreviewTocJumpTest PRIVATE
    sm_preview
    sm_parser
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME PreviewTocJumpTest COMMAND PreviewTocJumpTest)

# --- SnapSplitterDragFinishedTest ---
# Spec: specs/模块-app/13-分隔条吸附刻度.md
# Plan: plans/2026-05-07-拖拽分割线两侧全文重排.md
# 验收：T15（INV-SNAP-LAZY-PANE-REBUILD）
# 注意：sm_app 是 executable 而非 library，所以这里直接把 SnapSplitter.cpp
# 编进测试 exe，不通过 target_link_libraries 链。
add_executable(SnapSplitterDragFinishedTest
    app/SnapSplitterDragFinishedTest.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SnapSplitter.cpp
)

target_include_directories(SnapSplitterDragFinishedTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
)

target_link_libraries(SnapSplitterDragFinishedTest PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Test
    gtest
    gtest_main
)

add_test(NAME SnapSplitterDragFinishedTest COMMAND SnapSplitterDragFinishedTest)

# --- UpdateCheckerVersionTest ---
# Spec: specs/模块-app/23-检查更新.md INV-UPD-SEMVER
# Plan: plans/2026-06-15-检查更新功能-github-tag.md
# 注意：UpdateChecker 在 app(executable) 中，直接把 .cpp 编进测试 exe（AUTOMOC 处理 Q_OBJECT）
add_executable(UpdateCheckerVersionTest
    app/UpdateCheckerVersionTest.cpp
    ${CMAKE_SOURCE_DIR}/src/app/UpdateChecker.cpp
)
target_include_directories(UpdateCheckerVersionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(UpdateCheckerVersionTest PRIVATE
    Qt5::Core Qt5::Network gtest gtest_main)
add_test(NAME UpdateCheckerVersionTest COMMAND UpdateCheckerVersionTest)

# --- FontConsistencyTest ---
# Spec: specs/横切关注点/80-字体系统.md（INV-2 / INV-9 / INV-10、§4 接口段声明）
# 验收：T-FONT-XHEIGHT、T-FONT-MONO、T-FONT-BALANCE-NULL
add_executable(FontConsistencyTest
    app/FontConsistencyTest.cpp
)

target_include_directories(FontConsistencyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(FontConsistencyTest PRIVATE
    sm_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)

add_test(NAME FontConsistencyTest COMMAND FontConsistencyTest)

# --- TabBarAddButtonVisibilityTest ---
# Spec: specs/模块-app/04-窗口焦点管理.md INV-6（「+」按钮独立于 Tab 滚动，始终可见）
# Bug: plans/2026-06-16-多标签时新建按钮被挤不可见.md
# 验收：T-11（溢出时按钮可见）/ T-12（按钮与 tab 区不重叠）
# 注意：TabBarWithAdd 在 app(executable)，直接把 .cpp 编进测试 exe（AUTOMOC 处理 Q_OBJECT）
add_executable(TabBarAddButtonVisibilityTest
    app/TabBarAddButtonVisibilityTest.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TabBarWithAdd.cpp
)
target_include_directories(TabBarAddButtonVisibilityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
)
target_link_libraries(TabBarAddButtonVisibilityTest PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Test
    gtest
    gtest_main
)
add_test(NAME TabBarAddButtonVisibilityTest COMMAND TabBarAddButtonVisibilityTest)

# --- ReloadDialogI18nTest ---
# Spec: specs/横切关注点/60-国际化.md INV-QMSGBOX-TR（标准按钮用自定义 tr 文案）
# Bug: plans/2026-06-16-文件外部修改弹窗未国际化.md
# 验收：T-7（中文 locale 下「文件外部修改」弹窗按钮显示中文）
# 需把项目 qrc 编进测试 exe 才能访问 :/translations/simple_markdown_zh_CN.qm
qt5_add_resources(RELOAD_I18N_TEST_RES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_executable(ReloadDialogI18nTest
    app/ReloadDialogI18nTest.cpp
    ${RELOAD_I18N_TEST_RES}
)
target_include_directories(ReloadDialogI18nTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ReloadDialogI18nTest PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    gtest
    gtest_main
)
add_test(NAME ReloadDialogI18nTest COMMAND ReloadDialogI18nTest)

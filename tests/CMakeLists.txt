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

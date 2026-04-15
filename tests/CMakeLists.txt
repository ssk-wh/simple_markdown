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
    gtest
)

add_test(NAME TocPanelTest COMMAND TocPanelTest)

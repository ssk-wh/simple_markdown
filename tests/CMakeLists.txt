# Spec: specs/模块-preview/10-Frontmatter渲染.md （tests 字段）
# Last synced: 2026-04-14

enable_testing()

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

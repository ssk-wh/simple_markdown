add_library(sm_preview STATIC
    PreviewWidget.h PreviewWidget.cpp
    PreviewLayout.h PreviewLayout.cpp
    PreviewPainter.h PreviewPainter.cpp
    PreviewBlockCache.h PreviewBlockCache.cpp
    ImageCache.h ImageCache.cpp
    CodeBlockRenderer.h CodeBlockRenderer.cpp
    TocPanel.h TocPanel.cpp
)
target_include_directories(sm_preview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# [Spec 模块-preview/11-预览区查找] 复用 src/editor/SearchBar + SearchWorker
# （SearchBar 已解耦到 widget-agnostic 接口）。
# 后续 V2 可考虑把 SearchBar / SearchWorker 抽到 sm_core 让模块依赖更直观。
target_link_libraries(sm_preview PUBLIC sm_parser sm_core sm_editor Qt5::Widgets Qt5::Network)

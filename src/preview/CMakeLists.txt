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
target_link_libraries(sm_preview PUBLIC sm_parser sm_core Qt5::Widgets Qt5::Network)

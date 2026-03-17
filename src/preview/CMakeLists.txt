add_library(easy_preview STATIC
    PreviewWidget.h PreviewWidget.cpp
    PreviewLayout.h PreviewLayout.cpp
    PreviewPainter.h PreviewPainter.cpp
    PreviewBlockCache.h PreviewBlockCache.cpp
    ImageCache.h ImageCache.cpp
    CodeBlockRenderer.h CodeBlockRenderer.cpp
)
target_include_directories(easy_preview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easy_preview PUBLIC easy_parser easy_core Qt5::Widgets Qt5::Network)

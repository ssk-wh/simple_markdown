add_library(easy_parser STATIC
    MarkdownAst.h MarkdownAst.cpp
    MarkdownParser.h MarkdownParser.cpp
    ParseScheduler.h ParseScheduler.cpp
)
target_include_directories(easy_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(easy_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/3rdparty/cmark-gfm/src
    ${CMAKE_SOURCE_DIR}/3rdparty/cmark-gfm/extensions
    ${CMAKE_BINARY_DIR}/3rdparty/cmark-gfm/src
    ${CMAKE_BINARY_DIR}/3rdparty/cmark-gfm/extensions
)
target_compile_definitions(easy_parser PUBLIC CMARK_GFM_STATIC_DEFINE CMARK_GFM_EXTENSIONS_STATIC_DEFINE)
target_link_libraries(easy_parser PUBLIC easy_core Qt5::Core libcmark-gfm_static libcmark-gfm-extensions_static)

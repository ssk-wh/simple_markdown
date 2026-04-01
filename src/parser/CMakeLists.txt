add_library(sm_parser STATIC
    MarkdownAst.h MarkdownAst.cpp
    MarkdownParser.h MarkdownParser.cpp
    ParseScheduler.h ParseScheduler.cpp
)
target_include_directories(sm_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(sm_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/3rdparty/cmark-gfm/src
    ${CMAKE_SOURCE_DIR}/3rdparty/cmark-gfm/extensions
    ${CMAKE_BINARY_DIR}/3rdparty/cmark-gfm/src
    ${CMAKE_BINARY_DIR}/3rdparty/cmark-gfm/extensions
)
target_compile_definitions(sm_parser PUBLIC CMARK_GFM_STATIC_DEFINE CMARK_GFM_EXTENSIONS_STATIC_DEFINE)
target_link_libraries(sm_parser PUBLIC sm_core Qt5::Core libcmark-gfm-extensions_static libcmark-gfm_static)

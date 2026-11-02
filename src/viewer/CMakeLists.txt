add_library(sm_viewer STATIC
    ChunkScanner.h ChunkScanner.cpp
    ScrollModel.h ScrollModel.cpp
    DocumentLoader.h DocumentLoader.cpp
    BufferSearch.h BufferSearch.cpp
)
target_include_directories(sm_viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_viewer PUBLIC Qt5::Core)

add_library(sm_viewerwidget STATIC
    ViewerWidget.h ViewerWidget.cpp
    SearchBar.h SearchBar.cpp
)
target_include_directories(sm_viewerwidget PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_viewerwidget PUBLIC sm_viewer sm_preview sm_parser sm_core Qt5::Widgets)

add_library(sm_viewer STATIC
    ChunkScanner.h ChunkScanner.cpp
    ScrollModel.h ScrollModel.cpp
    DocumentLoader.h DocumentLoader.cpp
)
target_include_directories(sm_viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_viewer PUBLIC Qt5::Core)

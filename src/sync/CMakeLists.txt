add_library(sm_sync STATIC
    ScrollSync.h ScrollSync.cpp
)
target_include_directories(sm_sync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_sync PUBLIC sm_editor sm_preview)

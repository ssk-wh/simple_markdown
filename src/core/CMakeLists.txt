add_library(sm_core STATIC
    RecentFiles.h RecentFiles.cpp
    Theme.h Theme.cpp
    ThemeLoader.h ThemeLoader.cpp
    ThemeSchema.h
    FontDefaults.h
    PerfProbe.h PerfStartup.cpp
)

target_include_directories(sm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_core PUBLIC Qt5::Core Qt5::Gui)

add_library(sm_core STATIC
    MappedFile.h MappedFile.cpp
    PieceTable.h PieceTable.cpp
    LineIndex.h LineIndex.cpp
    UndoStack.h UndoStack.cpp
    Selection.h Selection.cpp
    Document.h Document.cpp
    RecentFiles.h RecentFiles.cpp
    Theme.h Theme.cpp
)

target_include_directories(sm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_core PUBLIC Qt5::Core Qt5::Gui)

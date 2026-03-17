add_library(easy_core STATIC
    MappedFile.h MappedFile.cpp
    PieceTable.h PieceTable.cpp
    LineIndex.h LineIndex.cpp
    UndoStack.h UndoStack.cpp
    Selection.h Selection.cpp
    Document.h Document.cpp
    RecentFiles.h RecentFiles.cpp
)

target_include_directories(easy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easy_core PUBLIC Qt5::Core)

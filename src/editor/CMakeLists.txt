add_library(sm_editor STATIC
    EditorLayout.h EditorLayout.cpp
    EditorWidget.h EditorWidget.cpp
    EditorPainter.h EditorPainter.cpp
    EditorInput.h EditorInput.cpp
    GutterRenderer.h GutterRenderer.cpp
    BandBuffer.h BandBuffer.cpp
    SyntaxHighlighter.h SyntaxHighlighter.cpp
    SearchBar.h SearchBar.cpp
    SearchWorker.h SearchWorker.cpp
)
target_include_directories(sm_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_editor PUBLIC sm_core Qt5::Widgets)

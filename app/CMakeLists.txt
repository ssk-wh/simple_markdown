add_executable(EasyMarkdown WIN32 main.cpp MainWindow.h MainWindow.cpp)
target_link_libraries(EasyMarkdown PRIVATE easy_editor easy_preview easy_sync)

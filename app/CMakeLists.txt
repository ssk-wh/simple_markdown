add_executable(EasyMarkdown WIN32 main.cpp)
target_link_libraries(EasyMarkdown PRIVATE easy_editor easy_preview)

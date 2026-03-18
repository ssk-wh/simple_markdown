set(APP_SOURCES main.cpp MainWindow.h MainWindow.cpp)

if(WIN32)
    list(APPEND APP_SOURCES app.rc)
endif()

qt5_add_resources(APP_QRC ../resources/resources.qrc)

add_executable(EasyMarkdown WIN32 ${APP_SOURCES} ${APP_QRC})
target_link_libraries(EasyMarkdown PRIVATE easy_editor easy_preview easy_sync)

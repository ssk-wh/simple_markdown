set(APP_SOURCES main.cpp MainWindow.h MainWindow.cpp ChangelogDialog.h ChangelogDialog.cpp)

if(WIN32)
    list(APPEND APP_SOURCES app.rc)
endif()

qt5_add_resources(APP_QRC ${CMAKE_SOURCE_DIR}/resources/resources.qrc)

add_executable(SimpleMarkdown WIN32 ${APP_SOURCES} ${APP_QRC})
target_link_libraries(SimpleMarkdown PRIVATE sm_editor sm_preview sm_sync Qt5::Network)

if(UNIX AND NOT APPLE)
    set_target_properties(SimpleMarkdown PROPERTIES OUTPUT_NAME simplemarkdown)
endif()

install(TARGETS SimpleMarkdown RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/app-icon.svg
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME simplemarkdown.svg)
    foreach(_size 16 24 32 48 128 256 512)
        install(FILES ${CMAKE_SOURCE_DIR}/resources/simplemarkdown_${_size}.png
                DESTINATION share/icons/hicolor/${_size}x${_size}/apps
                RENAME simplemarkdown.png)
    endforeach()
    install(FILES ${CMAKE_SOURCE_DIR}/resources/simplemarkdown.desktop
            DESTINATION share/applications)
endif()

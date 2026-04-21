set(APP_SOURCES main.cpp MainWindow.h MainWindow.cpp ChangelogDialog.h ChangelogDialog.cpp ShortcutsDialog.h ShortcutsDialog.cpp TabBarWithAdd.h TabBarWithAdd.cpp SideTabBar.h SideTabBar.cpp SnapSplitter.h SnapSplitter.cpp FolderPanel.h FolderPanel.cpp)

if(WIN32)
    list(APPEND APP_SOURCES app.rc)
endif()

# 翻译文件
# [i18n 不变量] .qm 必须与 resources.qrc 引用的源码 translations/ 路径一致，
# 否则新翻译条目不会被 qrc 打包，运行时只能看到英文源文本。
# 通过 OUTPUT_LOCATION 让 lrelease 直接把 .qm 输出到源码 translations/ 目录，
# 每次 build 都自动刷新，避免"改了 .ts 却忘记 lrelease"再次翻车。
# 参考：plans/归档/2026-04-14-欢迎页国际化缺失.md
set(TS_FILES
    ${CMAKE_SOURCE_DIR}/translations/simple_markdown_zh_CN.ts
    ${CMAKE_SOURCE_DIR}/translations/simple_markdown_en_US.ts
)
set_source_files_properties(${TS_FILES} PROPERTIES
    OUTPUT_LOCATION ${CMAKE_SOURCE_DIR}/translations)

qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR}/src ${TS_FILES})

qt5_add_resources(APP_QRC ${CMAKE_SOURCE_DIR}/resources/resources.qrc)

add_executable(SimpleMarkdown WIN32 ${APP_SOURCES} ${APP_QRC} ${QM_FILES})
target_compile_definitions(SimpleMarkdown PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(SimpleMarkdown PRIVATE sm_editor sm_preview sm_parser sm_sync Qt5::Network Qt5::PrintSupport)

if(UNIX AND NOT APPLE)
    set_target_properties(SimpleMarkdown PROPERTIES OUTPUT_NAME simplemarkdown)
    # 静态链接 libstdc++/libgcc，消除对系统 C++ 运行时版本的依赖
    target_link_options(SimpleMarkdown PRIVATE -static-libstdc++ -static-libgcc)
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

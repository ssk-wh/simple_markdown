set(APP_SOURCES main.cpp MainWindow.h MainWindow.cpp ChangelogDialog.h ChangelogDialog.cpp ShortcutsDialog.h ShortcutsDialog.cpp WelcomePanel.h WelcomePanel.cpp TabBarWithAdd.h TabBarWithAdd.cpp SideTabBar.h SideTabBar.cpp SnapSplitter.h SnapSplitter.cpp FolderPanel.h FolderPanel.cpp)

if(WIN32)
    # Spec: specs/模块-app/README.md 全局约束 [INV-WIN-VERSIONINFO]
    # 由 CHANGELOG 自动提取的 PROJECT_VERSION 注入到 VERSIONINFO 资源块，
    # 让 Windows 任务管理器、Explorer 属性页、PE 资源浏览器显示友好的应用名称。
    string(REPLACE "." ";" _APP_VERSION_LIST "${PROJECT_VERSION}")
    list(LENGTH _APP_VERSION_LIST _APP_VERSION_COUNT)
    if(_APP_VERSION_COUNT GREATER_EQUAL 3)
        list(GET _APP_VERSION_LIST 0 APP_VERSION_MAJOR)
        list(GET _APP_VERSION_LIST 1 APP_VERSION_MINOR)
        list(GET _APP_VERSION_LIST 2 APP_VERSION_PATCH)
    else()
        set(APP_VERSION_MAJOR 0)
        set(APP_VERSION_MINOR 0)
        set(APP_VERSION_PATCH 0)
    endif()
    set(APP_VERSION_FULL "${PROJECT_VERSION}")
    # rc.exe 接受正斜杠路径，跨编译/原生 toolchain 都安全
    set(APP_ICON_PATH "${CMAKE_SOURCE_DIR}/resources/app-icon.ico")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
        @ONLY)
    list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
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

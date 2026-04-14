cmake_minimum_required(VERSION 3.16)

# 从 CHANGELOG.md 自动提取版本号
file(READ "${CMAKE_SOURCE_DIR}/CHANGELOG.md" _changelog)
string(REGEX MATCH "\\[([0-9]+\\.[0-9]+\\.[0-9]+)\\]" _ "${_changelog}")
set(_ver "${CMAKE_MATCH_1}")
if(NOT _ver)
    set(_ver "0.0.0")
endif()

project(SimpleMarkdown VERSION ${_ver} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# [测试模式] 编译选项：自动化测试输出渲染块坐标
option(ENABLE_TEST_MODE "Enable test mode for rendering block coordinates output" OFF)
if(ENABLE_TEST_MODE)
    add_compile_definitions(ENABLE_TEST_MODE)
    message(STATUS "Test mode enabled: Application will output render_blocks.json")
else()
    message(STATUS "Test mode disabled: Normal production build")
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network PrintSupport LinguistTools)

option(CMARK_TESTS "" OFF)
option(CMARK_SHARED "" OFF)
add_subdirectory(3rdparty/cmark-gfm)

option(BUILD_GMOCK "" OFF)
option(INSTALL_GTEST "" OFF)
add_subdirectory(3rdparty/googletest)

add_subdirectory(src)

# Spec: specs/模块-preview/10-Frontmatter渲染.md 要求 tests/preview/FrontmatterRenderTest.cpp
enable_testing()
add_subdirectory(tests)

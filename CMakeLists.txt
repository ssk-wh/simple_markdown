cmake_minimum_required(VERSION 3.16)

# CMake 4.x 兼容性：3rdparty/ 下的第三方库（如 cmark-gfm 写 cmake_minimum_required(3.0)、
# googletest 部分历史版本）会触发 "Compatibility with CMake < 3.5 has been removed" 错误。
# 在 add_subdirectory 之前设此变量，让 CMake 用 3.5 的策略行为兼容老脚本。
# 不修 3rdparty 源码（保持 submodule 干净便于未来 update）。
# 历史背景：2026-05-07 GitHub Actions Windows runner 升级 CMake 到 4.x 后 CI 编译失败。
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

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

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network PrintSupport LinguistTools Test)

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

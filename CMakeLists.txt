cmake_minimum_required(VERSION 3.16)
project(EasyMarkdown VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt5 REQUIRED COMPONENTS Core)

option(CMARK_TESTS "" OFF)
option(CMARK_SHARED "" OFF)
add_subdirectory(3rdparty/cmark-gfm)

option(BUILD_GMOCK "" OFF)
option(INSTALL_GTEST "" OFF)
add_subdirectory(3rdparty/googletest)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

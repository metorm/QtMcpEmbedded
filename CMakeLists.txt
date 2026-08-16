cmake_minimum_required(VERSION 3.16)
project(QtMcpEmbedded VERSION 0.1.0 LANGUAGES CXX)

option(QTMCP_BUILD_EXAMPLES "Build example applications" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

# Interpret all source files as UTF-8 (tooltips and other user-facing strings
# may contain non-ASCII text).
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

add_subdirectory(src)

if(QTMCP_BUILD_EXAMPLES)
    add_subdirectory(examples/demo_app)
endif()

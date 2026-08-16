cmake_minimum_required(VERSION 3.16)
project(QtMcpEmbedded VERSION 0.1.0 LANGUAGES CXX)

# Examples default ON only when this repo is the top-level project; as an
# add_subdirectory() subproject they stay out of the consumer's build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_qtmcp_examples_default ON)
else()
    set(_qtmcp_examples_default OFF)
endif()
option(QTMCP_BUILD_EXAMPLES "Build example applications" ${_qtmcp_examples_default})

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

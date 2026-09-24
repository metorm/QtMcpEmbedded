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

# Prefer the Qt major version the host project has already imported. As an
# add_subdirectory() subproject the parent resolves Qt itself (e.g. through
# Qt5_DIR); the previous NAMES-based find_package(QT ...) here could not see
# that hint (it only honours QT_DIR) and could select a different major version
# than the host, mixing two Qt runtimes in one build. A host may import only
# some components (srmsimulator imports Core + Widgets, without Network), so the
# major version is probed at component level and the missing components are
# completed within that same version. Standalone builds have no host targets and
# fall back to a versioned find_package (Qt6 preferred, then Qt5).
if(TARGET Qt5::Widgets OR TARGET Qt5::Core)
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
    set(QT_VERSION_MAJOR 5)
elseif(TARGET Qt6::Widgets OR TARGET Qt6::Core)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt6 QUIET COMPONENTS Widgets Network)
    if(Qt6_FOUND)
        set(QT_VERSION_MAJOR 6)
    else()
        find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
        set(QT_VERSION_MAJOR 5)
    endif()
endif()

option(QTMCP_BUILD_PLUGIN "Build Qt generic plugin for zero-code integration" ON)

add_subdirectory(src)

if(QTMCP_BUILD_EXAMPLES)
    add_subdirectory(examples/demo_app)
    add_subdirectory(examples/unmodified_app)
endif()

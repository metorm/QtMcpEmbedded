set(_qtmcp_common_sources
    QtMcp.h
    QtMcp.cpp
    core/ToolError.h
    core/HeadlessCompat.h
    core/HeadlessCompat.cpp
    core/RefRegistry.h
    core/RefRegistry.cpp
    core/ProbeServer.h
    core/ProbeServer.cpp
    transport/HttpServer.h
    transport/HttpServer.cpp
    transport/SseStream.h
    transport/SseStream.cpp
    protocol/JsonRpc.h
    protocol/JsonRpc.cpp
    protocol/McpSession.h
    protocol/McpSession.cpp
    protocol/ToolRegistry.h
    protocol/ToolRegistry.cpp
    protocol/McpDispatcher.h
    protocol/McpDispatcher.cpp
    tools/Introspector.h
    tools/Introspector.cpp
    tools/Interactor.h
    tools/Interactor.cpp
    tools/Screenshotter.h
    tools/Screenshotter.cpp
    tools/MessageLog.h
    tools/MessageLog.cpp
    tools/HostLog.h
    tools/HostLog.cpp
)

add_library(QtMcpEmbedded STATIC ${_qtmcp_common_sources})

target_include_directories(QtMcpEmbedded PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Capacity of the QtMcp::postMessage() staging ring buffer (qt_host_messages).
set(QTMCP_HOST_LOG_CAPACITY 500 CACHE STRING
    "Max staged host-log messages kept for qt_host_messages (oldest dropped beyond this)")
# Capacity of the Qt internal message ring buffer (qt_debug_message).
set(QTMCP_MESSAGE_LOG_CAPACITY 500 CACHE STRING
    "Max captured Qt internal messages (qDebug/qWarning/...) kept for qt_debug_message")
target_compile_definitions(QtMcpEmbedded
    PRIVATE QTMCP_HOST_LOG_CAPACITY=${QTMCP_HOST_LOG_CAPACITY}
            QTMCP_MESSAGE_LOG_CAPACITY=${QTMCP_MESSAGE_LOG_CAPACITY})

target_link_libraries(QtMcpEmbedded
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
    PRIVATE
        # QPA headers (qpa/qwindowsysteminterface.h) for the headless window
        # activation workaround in core/HeadlessCompat.cpp. Private headers
        # ship with standard Qt desktop installs; without them the workaround
        # degrades gracefully (see __has_include guard).
        Qt${QT_VERSION_MAJOR}::GuiPrivate
)

if(MSVC)
    target_compile_options(QtMcpEmbedded PRIVATE /W3 /utf-8)
else()
    target_compile_options(QtMcpEmbedded PRIVATE -Wall -Wextra)
endif()

if(QTMCP_BUILD_PLUGIN)
    add_library(qtmcp MODULE
        ${_qtmcp_common_sources}
        plugin/QtMcpPlugin.h
        plugin/QtMcpPlugin.cpp
        plugin/qtmcp.json
    )

    target_include_directories(qtmcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(qtmcp
        PRIVATE QTMCP_HOST_LOG_CAPACITY=${QTMCP_HOST_LOG_CAPACITY}
                QTMCP_MESSAGE_LOG_CAPACITY=${QTMCP_MESSAGE_LOG_CAPACITY})

    target_link_libraries(qtmcp
        PRIVATE
            Qt${QT_VERSION_MAJOR}::Widgets
            Qt${QT_VERSION_MAJOR}::Network
            Qt${QT_VERSION_MAJOR}::GuiPrivate
    )

    if(MSVC)
        target_compile_options(qtmcp PRIVATE /W3 /utf-8)
    else()
        target_compile_options(qtmcp PRIVATE -Wall -Wextra)
    endif()

    set_target_properties(qtmcp PROPERTIES
        DEBUG_POSTFIX "d"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/generic"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/generic"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/plugins/generic"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/plugins/generic"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/generic"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/generic"
    )
endif()

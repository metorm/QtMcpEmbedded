add_library(QtMcpEmbedded STATIC
    QtMcp.h
    QtMcp.cpp
    core/ToolError.h
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
)

target_include_directories(QtMcpEmbedded PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(QtMcpEmbedded
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
)

if(MSVC)
    target_compile_options(QtMcpEmbedded PRIVATE /W3 /utf-8)
else()
    target_compile_options(QtMcpEmbedded PRIVATE -Wall -Wextra)
endif()

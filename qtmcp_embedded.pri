# QtMcpEmbedded — qmake 集成（Qt5/Qt6 通用）
#
# 用法：在你的 .pro 中加入一行
#     include(<本仓库路径>/qtmcp_embedded.pri)
# 然后在 main() 里 QApplication 创建之后调用 QtMcp::install()。
# 未设置 QT_MCP_PROBE=1 环境变量时 install() 是零开销 no-op。

QT += widgets network
CONFIG += c++17

QTMCP_ROOT = $$PWD
INCLUDEPATH += $$QTMCP_ROOT/src

# 源码按 UTF-8 解释（tooltip/instructions 等可能含非 ASCII 字符串）
win32-msvc: QMAKE_CXXFLAGS += /utf-8

HEADERS += \
    $$QTMCP_ROOT/src/QtMcp.h \
    $$QTMCP_ROOT/src/core/ProbeServer.h \
    $$QTMCP_ROOT/src/core/RefRegistry.h \
    $$QTMCP_ROOT/src/core/ToolError.h \
    $$QTMCP_ROOT/src/protocol/JsonRpc.h \
    $$QTMCP_ROOT/src/protocol/McpDispatcher.h \
    $$QTMCP_ROOT/src/protocol/McpSession.h \
    $$QTMCP_ROOT/src/protocol/ToolRegistry.h \
    $$QTMCP_ROOT/src/tools/Introspector.h \
    $$QTMCP_ROOT/src/tools/Interactor.h \
    $$QTMCP_ROOT/src/tools/Screenshotter.h \
    $$QTMCP_ROOT/src/tools/MessageLog.h \
    $$QTMCP_ROOT/src/transport/HttpServer.h \
    $$QTMCP_ROOT/src/transport/SseStream.h

SOURCES += \
    $$QTMCP_ROOT/src/QtMcp.cpp \
    $$QTMCP_ROOT/src/core/ProbeServer.cpp \
    $$QTMCP_ROOT/src/core/RefRegistry.cpp \
    $$QTMCP_ROOT/src/protocol/JsonRpc.cpp \
    $$QTMCP_ROOT/src/protocol/McpDispatcher.cpp \
    $$QTMCP_ROOT/src/protocol/McpSession.cpp \
    $$QTMCP_ROOT/src/protocol/ToolRegistry.cpp \
    $$QTMCP_ROOT/src/tools/Introspector.cpp \
    $$QTMCP_ROOT/src/tools/Interactor.cpp \
    $$QTMCP_ROOT/src/tools/Screenshotter.cpp \
    $$QTMCP_ROOT/src/tools/MessageLog.cpp \
    $$QTMCP_ROOT/src/transport/HttpServer.cpp \
    $$QTMCP_ROOT/src/transport/SseStream.cpp

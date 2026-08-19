# QtMcpEmbedded — qmake 集成（Qt5/Qt6 通用）
#
# 用法：在你的 .pro 中加入一行
#     include(<本仓库路径>/qtmcp_embedded.pri)
# 然后在 main() 里 QApplication 创建之后调用 QtMcp::install()。
# 未设置 QT_MCP_PROBE=1 环境变量时 install() 是零开销 no-op。

QT += widgets network
# QPA 头（qpa/qwindowsysteminterface.h）供 HeadlessCompat 的离屏窗口激活
# 变通使用；标准 Qt 桌面安装自带私有头，缺失时代码经 __has_include 优雅降级
QT += gui-private
CONFIG += c++17

# 两个日志缓冲区容量：QTMCP_HOST_LOG_CAPACITY = qt_host_messages 暂存区
# （QtMcp::postMessage 环形缓冲），QTMCP_MESSAGE_LOG_CAPACITY = qt_debug_message
# 的 Qt 内部消息环形缓冲；均可用 qmake 同名变量覆盖
isEmpty(QTMCP_HOST_LOG_CAPACITY): QTMCP_HOST_LOG_CAPACITY = 500
isEmpty(QTMCP_MESSAGE_LOG_CAPACITY): QTMCP_MESSAGE_LOG_CAPACITY = 500
DEFINES += QTMCP_HOST_LOG_CAPACITY=$$QTMCP_HOST_LOG_CAPACITY \
           QTMCP_MESSAGE_LOG_CAPACITY=$$QTMCP_MESSAGE_LOG_CAPACITY

QTMCP_ROOT = $$PWD
INCLUDEPATH += $$QTMCP_ROOT/src

# 源码按 UTF-8 解释（tooltip/instructions 等可能含非 ASCII 字符串）
win32-msvc: QMAKE_CXXFLAGS += /utf-8

HEADERS += \
    $$QTMCP_ROOT/src/QtMcp.h \
    $$QTMCP_ROOT/src/core/HeadlessCompat.h \
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
    $$QTMCP_ROOT/src/tools/HostLog.h \
    $$QTMCP_ROOT/src/transport/HttpServer.h \
    $$QTMCP_ROOT/src/transport/SseStream.h

SOURCES += \
    $$QTMCP_ROOT/src/QtMcp.cpp \
    $$QTMCP_ROOT/src/core/HeadlessCompat.cpp \
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
    $$QTMCP_ROOT/src/tools/HostLog.cpp \
    $$QTMCP_ROOT/src/transport/HttpServer.cpp \
    $$QTMCP_ROOT/src/transport/SseStream.cpp

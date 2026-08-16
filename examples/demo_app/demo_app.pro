# demo_app 的 qmake 构建（与 CMake 构建共用同一份源码，用于双构建系统验证）
# 用法：
#   mkdir build-qmake && cd build-qmake
#   qmake ../examples/demo_app/demo_app.pro
#   nmake        # 或 jom / make（取决于平台/生成器）

TEMPLATE = app
TARGET = demo_app

include($$PWD/../../qtmcp_embedded.pri)

SOURCES += main.cpp

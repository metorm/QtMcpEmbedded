# QtMcpEmbedded

[English](README.md) | **中文**

把 MCP (Model Context Protocol) server 直接嵌入 Qt Widgets 应用的 C++ 库——链接一次、加一行代码，运行中的程序就在本机端口提供完整的自动化调试与操作能力，供 Claude Code 等 AI agent 直连使用。类似 Playwright MCP，但面向桌面 Qt 程序。

- 单进程架构：**无 Python、无外部进程**，MCP server 运行在应用自己的 GUI 线程里
- 传输：MCP **Streamable HTTP**（单 `/mcp` 端点），默认绑定 `127.0.0.1:9142`
- Qt5（≥5.15 已验证）/ Qt6（同源码，待验证）双支持
- 以 [qt-mcp](https://github.com/0xCarbon/qt-mcp)（Python/PySide6）为设计蓝本，工具集与行为语义和其保持一致（详见文末"蓝本与致谢"）

## 快速开始

最小接入——CMake `add_subdirectory`、qmake `include`、一行 `QtMcp::install()` 三件套：

```cmake
add_subdirectory(third_party/QtMcpEmbedded)   # 作为子项目时不会构建本仓库的 examples
target_link_libraries(your_app PRIVATE QtMcpEmbedded)
```

```qmake
include(third_party/QtMcpEmbedded/qtmcp_embedded.pri)
```

```cpp
#include <QtMcp.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QtMcp::install();   // 唯一一行；未设 QT_MCP_PROBE=1 时是零开销 no-op
    ...
}
```

完整嵌入指南（InstallOptions、两阶段启动、自定义命令、环境变量）见 [doc/embedding.zh-CN.md](doc/embedding.zh-CN.md)。

## 文档导航

| 主题 | English | 中文 |
|---|---|---|
| 嵌入集成指南 | [doc/embedding.md](doc/embedding.md) | [doc/embedding.zh-CN.md](doc/embedding.zh-CN.md) |
| 工具使用参考 | [doc/usage.md](doc/usage.md) | [doc/usage.zh-CN.md](doc/usage.zh-CN.md) |
| 客户端接入与验证 | [doc/client.md](doc/client.md) | [doc/client.zh-CN.md](doc/client.zh-CN.md) |
| 脚本化自动化与测试 | [doc/automation.md](doc/automation.md) | [doc/automation.zh-CN.md](doc/automation.zh-CN.md) |
| 要避免的问题与已知边界 | [doc/pitfalls.md](doc/pitfalls.md) | [doc/pitfalls.zh-CN.md](doc/pitfalls.zh-CN.md) |

## 验证

`client/` 内置端到端验证脚本（80+ 项断言，覆盖全部工具与阻塞场景），运行方法见 [doc/client.zh-CN.md](doc/client.zh-CN.md)。

## 构建本仓库

需要 CMake（presets v6+）、Ninja、Qt 5.15+ 或 Qt 6。

首次构建先创建本机 preset：复制 `CMakeUserPresets.json.template` 为
`CMakeUserPresets.json`（该文件已被 .gitignore 排除，不会入库），把里面的
`CMAKE_PREFIX_PATH` 改成你机器上的 Qt 路径（示例值是 Qt 5.15.2 MSVC64），然后：

```bash
cmake --preset local              # 首次 configure
cmake --build --preset local-debug    # 或 local-release
```

如果 Qt 已在环境变量里（如 `CMAKE_PREFIX_PATH` 已设置），也可以直接用项目自带的
`default` preset（`cmake --preset default`，构建 preset 为 `debug` / `release`）。
两种 preset 都是 Ninja Multi-Config，产物在 `build/` 下按配置分目录。

`examples/demo_app` 构建后自动执行 windeployqt（含 offscreen 插件），exe 可直接双击运行。

## 仓库结构

```
src/
  QtMcp.h              # 公开头：QtMcp::install()
  core/                # ProbeServer 装配、RefRegistry（ref → QPointer，单调编号）
  transport/           # QTcpServer 上的极简 HTTP/1.1 + SSE
  protocol/            # JSON-RPC 2.0、MCP 会话、工具注册与分发
  tools/               # Introspector / Interactor / Screenshotter / MessageLog / HostLog
doc/                   # 文档：embedding / usage / client / automation / pitfalls（中英双语）
examples/demo_app/     # 覆盖 20+ 控件与各类联动/阻塞场景的测试程序
examples/qt-ads/       # 第三方复杂项目（Qt-Advanced-Docking-System）嵌入指南、补丁与测试脚本
client/                # uv 环境 + verify.py 端到端验证
PLAN.md                # 原始设计计划（含里程碑与风险分析）
```

## 蓝本与致谢

本项目以 [qt-mcp](https://github.com/0xCarbon/qt-mcp)（MIT/Apache-2.0 双许可）为设计蓝本：工具集划分、ref 机制、wait_for/batch 语义等核心设计均与其保持一致，两者说同一套 `qt_*` 工具方言，`client/verify.py` 这类只依赖接口的验收脚本可以跨实现复用。

在保持一致的前提下，本项目修正/增强了蓝本中的若干行为：模态对话框 `exec()` 死锁（改为异步事件投递）、合成事件不触发上下文菜单（补发 `QContextMenuEvent`）、item view 点击的 viewport 重定向、ref 跨 snapshot 静默重绑、失败误报成功等，并新增了 `qt_active_popup`、操作守卫（hidden/disabled/modal + `force`）、写后读回、tree item 一等操作目标等能力。

## License

Licensed under either of:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

## 安全说明

该服务是调试面：任意属性写、任意槽调用、输入注入。请始终绑定 `127.0.0.1`（默认即如此），不要在生产环境启用（不设置 `QT_MCP_PROBE` 即完全关闭）。

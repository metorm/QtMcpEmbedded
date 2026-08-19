# QtMcpEmbedded

**English** | [中文](README.zh-CN.md)

A C++ library that embeds an MCP (Model Context Protocol) server directly into a Qt Widgets application — link once, add one line of code, and the running program exposes full automation, debugging and driving capabilities on a local port for AI agents such as Claude Code. Like Playwright MCP, but for desktop Qt applications.

- Single-process architecture: **no Python, no external processes** — the MCP server runs inside the application's own GUI thread
- Transport: MCP **Streamable HTTP** (single `/mcp` endpoint), bound to `127.0.0.1:9142` by default
- Dual Qt support: Qt5 (verified ≥ 5.15) / Qt6 (same source, not yet verified)
- Designed after [qt-mcp](https://github.com/0xCarbon/qt-mcp) (Python/PySide6); the tool set and behavioral semantics match it (see "Blueprint & Acknowledgements" below)

## Quick Start

Minimal integration — CMake `add_subdirectory`, qmake `include`, and one `QtMcp::install()` call:

```cmake
add_subdirectory(third_party/QtMcpEmbedded)   # as a subproject, this repo's examples are not built
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
    QtMcp::install();   // the only line; a zero-overhead no-op unless QT_MCP_PROBE=1 is set
    ...
}
```

For the full embedding guide (InstallOptions, two-phase startup, custom commands, environment variables), see [doc/embedding.md](doc/embedding.md).

## Documentation

| Topic | English | 中文 |
|---|---|---|
| Embedding & integration | [doc/embedding.md](doc/embedding.md) | [doc/embedding.zh-CN.md](doc/embedding.zh-CN.md) |
| Tool reference | [doc/usage.md](doc/usage.md) | [doc/usage.zh-CN.md](doc/usage.zh-CN.md) |
| Client setup & verification | [doc/client.md](doc/client.md) | [doc/client.zh-CN.md](doc/client.zh-CN.md) |
| Scripted automation & testing | [doc/automation.md](doc/automation.md) | [doc/automation.zh-CN.md](doc/automation.zh-CN.md) |
| Pitfalls & known limits | [doc/pitfalls.md](doc/pitfalls.md) | [doc/pitfalls.zh-CN.md](doc/pitfalls.zh-CN.md) |

## Verification

An end-to-end verification script (80+ assertions covering every tool and blocking scenario) ships in `client/` — see [doc/client.md](doc/client.md) for how to run it.

## Building This Repository

Requires CMake (presets v6+), Ninja, and Qt 5.15+ or Qt 6.

For the first build, create a local preset: copy `CMakeUserPresets.json.template` to
`CMakeUserPresets.json` (excluded via .gitignore, never committed), set
`CMAKE_PREFIX_PATH` inside it to the Qt path on your machine (the example value is
Qt 5.15.2 MSVC64), then:

```bash
cmake --preset local              # first configure
cmake --build --preset local-debug    # or local-release
```

If Qt is already in the environment (e.g. `CMAKE_PREFIX_PATH` is set), you can also use
the repo's built-in `default` preset (`cmake --preset default`; build presets are
`debug` / `release`). Both presets are Ninja Multi-Config; artifacts land in `build/`
in per-configuration subdirectories.

`examples/demo_app` runs windeployqt automatically after building (including the
offscreen plugin), so the exe can be double-clicked directly.

## Repository Structure

```
src/
  QtMcp.h              # public header: QtMcp::install()
  core/                # ProbeServer assembly, RefRegistry (ref → QPointer, monotonic numbering)
  transport/           # minimal HTTP/1.1 + SSE on QTcpServer
  protocol/            # JSON-RPC 2.0, MCP sessions, tool registration & dispatch
  tools/               # Introspector / Interactor / Screenshotter / MessageLog / HostLog
doc/                   # documentation: embedding / usage / client / automation / pitfalls (EN + zh-CN)
examples/demo_app/     # test app covering 20+ widgets and various interaction/blocking scenarios
examples/qt-ads/       # embedding guide, patches and test scripts for a third-party complex project (Qt-Advanced-Docking-System)
client/                # uv environment + verify.py end-to-end verification
PLAN.md                # original design plan (with milestones and risk analysis)
```

## Blueprint & Acknowledgements

This project is designed after [qt-mcp](https://github.com/0xCarbon/qt-mcp) (dual-licensed MIT/Apache-2.0): core designs such as the tool set division, the ref mechanism, and wait_for/batch semantics all match it. Both speak the same `qt_*` tool dialect, so interface-only acceptance scripts like `client/verify.py` can be reused across implementations.

While staying consistent, this project fixes/enhances several behaviors of the blueprint: modal-dialog `exec()` deadlock (replaced with asynchronous event posting), synthetic events not triggering context menus (a `QContextMenuEvent` is now sent), viewport redirection for item-view clicks, silent ref re-binding across snapshots, failures misreported as success, etc. It also adds `qt_active_popup`, operation guards (hidden/disabled/modal + `force`), write-then-read-back, tree items as first-class operation targets, and more.

## License

Licensed under either of:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

## Security Note

This server is a debugging surface: arbitrary property writes, arbitrary slot invocation, input injection. Always bind to `127.0.0.1` (the default) and never enable it in production (leaving `QT_MCP_PROBE` unset disables it completely).

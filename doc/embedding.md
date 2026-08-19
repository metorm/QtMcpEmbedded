# Embedding & Integration Guide

**English** | [中文](embedding.zh-CN.md)

## Build Integration

Integrate via `add_subdirectory` (clone or git submodule into your project tree both work):

```cmake
add_subdirectory(third_party/QtMcpEmbedded)   # as a subproject, this repo's examples are not built
target_link_libraries(your_app PRIVATE QtMcpEmbedded)
```

For qmake projects, add one line to your `.pro`:

```qmake
include(third_party/QtMcpEmbedded/qtmcp_embedded.pri)
```

## install() and InstallOptions

```cpp
#include <QtMcp.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QtMcp::install();   // the only line; a zero-overhead no-op unless QT_MCP_PROBE=1 is set
    ...
}
```

Optional host self-description (merged into the `instructions` field of the MCP
`initialize` response, so the AI gets a map of your application as soon as it connects):

```cpp
QtMcp::InstallOptions opts;
opts.appName = "My Simulator";
opts.instructions = "主流程：先…再…；重要控件：objectName xxx 是…";  // main flow: first…then…; key widgets: objectName xxx is…
QtMcp::install(opts);
```

## Two-Phase Startup

By default (`autoStart=true`), `install()` assembles the probe and immediately starts
listening on the port — the one-line behavior is unchanged. Applications that register
custom commands should prefer the two-phase mode: `install()` only assembles without
listening, and `startServer()` opens the server after all commands are registered — so
the MCP client gets the complete tool list on its first connection, avoiding the
"client fetches tools/list only once and caches it" problem:

```cpp
QtMcp::InstallOptions opts;
opts.autoStart = false;            // don't start listening yet
QtMcp::install(opts);

MainWindow w;                      // object graph in place; registerCommands() is called in the constructor
w.show();

QtMcp::startServer();              // registration complete; start serving (idempotent)
```

Commands registered after the server started are still legal and appear in the next
`tools/list` fetch — but most clients cache the tool list from the first fetch, so in
scenarios like lazily loaded plugins the trade-off is up to the host.

## Custom Commands (registerCommand)

Expose "frequently used but tedious to click through" functionality directly as MCP
tools, alongside the built-in `qt_*` tools:

```cpp
void MainWindow::registerCommands()   // convention: a main-window member function, called at the end of the constructor
{
    QtMcp::registerCommand(
        "sim_start",
        "启动仿真。前置条件：已加载工程。",   // "Start simulation. Prerequisite: a project is loaded."
        QJsonObject{{"type", "object"}},          // inputSchema, same style as built-in tools
        [this](const QJsonObject &args) -> QtMcp::CommandResult {
            m_sim->start();
            return QtMcp::CommandResult::ok({{"started", true}});
        },
        [this]() -> QString {                     // availability check (optional)
            return m_project ? QString() : QStringLiteral("no project loaded");
        },
        this);                                    // context: auto-unregistered on destruction (optional)
}
```

- `CommandResult`: `data` (JSON, serialized to text and returned to the AI) + `isError`;
  factories `CommandResult::ok(data)` / `CommandResult::error(message)`.
- `AvailabilityCheck`: returning an empty string means executable; returning a non-empty
  string means not executable, and the string is the reason given to the AI.
- Naming rules: the `qt_` prefix is forbidden (reserved for built-in tools), and names
  must not collide with an existing tool or command; violations return `false` and emit
  a `qWarning`.
- Registration timing: called before `install()`, the registration goes into a pending
  queue (thread-safe) and is flushed at install time; called after install(), it goes
  straight into the live tool registry (synchronously from the GUI thread, via queued
  delivery from any other thread). Everything is a no-op when `QT_MCP_PROBE=1` is not set.
- Lifetime: `context` has the same semantics as `QObject::connect` — the command is
  automatically unregistered when the context object is destroyed, so lambdas capturing
  `this` cannot dangle. You can also remove it manually with
  `QtMcp::unregisterCommand(name)`.

**Three layers of availability expression**: ① enforced at invocation time — when a
`tools/call` arrives, the `AvailabilityCheck` runs first; a non-empty reason yields
`isError` + `"Command '<name>' is not available now: <reason>"`, so the AI always
receives the reason; ② the `qt_app_commands` status tool — returns a snapshot of all
custom commands as `{name, description, available, reason}`, so the AI can check during
planning and avoid trial-and-error; ③ state the prerequisites in the description
(convention, not enforced).

**Threading contract**: both the handler and the availability check run inside the
host GUI thread's event loop and may read/write widget state directly. Do not run
time-consuming work synchronously inside a handler (it freezes both the UI and MCP) —
start a background task and return immediately, report progress via
`QtMcp::postMessage()`, and let the AI poll with `qt_host_messages`.

## Host Message Push & Capacities

The host can also actively push runtime status to the agent (optional; if never called,
`qt_host_messages` is always empty):

```cpp
QtMcp::postMessage("仿真完成，误差 0.3%", "info");  // callable from any thread; zero-overhead no-op when the probe is disabled
```

The staging area holds 500 messages by default (oldest are dropped beyond that, counted
in `dropped`); adjustable at build time: CMake `-DQTMCP_HOST_LOG_CAPACITY=N`, qmake
`QTMCP_HOST_LOG_CAPACITY=N`. The Qt internal message buffer behind `qt_debug_message`
works the same way: CMake `-DQTMCP_MESSAGE_LOG_CAPACITY=N`, qmake
`QTMCP_MESSAGE_LOG_CAPACITY=N` (default 500).

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `QT_MCP_PROBE` | unset | `1` enables the probe; when unset, `install()` is completely inert |
| `QT_MCP_PORT` | `9142` | listening port |
| `QT_MCP_HOST` | `127.0.0.1` | listening address (before changing it to non-localhost, be aware: this port is equivalent to full control of the process) |

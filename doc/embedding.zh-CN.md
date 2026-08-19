# 嵌入集成指南

[English](embedding.md) | **中文**

## 构建接入

以 `add_subdirectory` 方式接入（clone 或 git submodule 放进项目树均可）：

```cmake
add_subdirectory(third_party/QtMcpEmbedded)   # 作为子项目时不会构建本仓库的 examples
target_link_libraries(your_app PRIVATE QtMcpEmbedded)
```

qmake 项目则在 `.pro` 里加一行：

```qmake
include(third_party/QtMcpEmbedded/qtmcp_embedded.pri)
```

## install() 与 InstallOptions

```cpp
#include <QtMcp.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QtMcp::install();   // 唯一一行；未设 QT_MCP_PROBE=1 时是零开销 no-op
    ...
}
```

可选的宿主自我说明（会并入 MCP `initialize` 响应的 `instructions`，AI 连上即得应用地图）：

```cpp
QtMcp::InstallOptions opts;
opts.appName = "My Simulator";
opts.instructions = "主流程：先…再…；重要控件：objectName xxx 是…";
QtMcp::install(opts);
```

## 两阶段启动

`install()` 默认（`autoStart=true`）装配并立即监听端口，一行接入的行为不变。
要写自定义命令的应用建议改用两阶段：`install()` 只装配不监听，命令注册完毕后
`startServer()` 才开服——MCP client 首次连接拿到的就是完整工具表，规避了
"client 只拉一次 tools/list 就缓存"的问题：

```cpp
QtMcp::InstallOptions opts;
opts.autoStart = false;            // 先不开服
QtMcp::install(opts);

MainWindow w;                      // 对象图就位；registerCommands() 在构造函数里调用
w.show();

QtMcp::startServer();              // 注册完毕，开服（幂等）
```

开服之后再注册的命令仍然合法，下次 `tools/list` 拉取即可见——但多数 client
会缓存首次的工具列表，懒加载插件等场景由宿主权衡。

## 自定义命令（registerCommand）

把"界面操作繁琐但高频"的功能直接挂成 MCP 工具，与内置 `qt_*` 工具并列：

```cpp
void MainWindow::registerCommands()   // 约定：主窗体成员函数，构造函数末尾调用
{
    QtMcp::registerCommand(
        "sim_start",
        "启动仿真。前置条件：已加载工程。",
        QJsonObject{{"type", "object"}},          // inputSchema，与内置工具同风格
        [this](const QJsonObject &args) -> QtMcp::CommandResult {
            m_sim->start();
            return QtMcp::CommandResult::ok({{"started", true}});
        },
        [this]() -> QString {                     // 可用性检查（可选）
            return m_project ? QString() : QStringLiteral("no project loaded");
        },
        this);                                    // context：销毁时自动注销（可选）
}
```

- `CommandResult`：`data`（JSON，序列化为文本返回给 AI）+ `isError`；
  工厂 `CommandResult::ok(data)` / `CommandResult::error(message)`。
- `AvailabilityCheck` 返回空串 = 可执行；返回非空串 = 不可执行，串内容即给 AI 的原因。
- 命名规则：禁止 `qt_` 前缀（保留给内置工具），禁止与已有工具/命令重名；
  违规返回 `false` 并 `qWarning`。
- 注册时机：`install()` 之前调用进 pending 队列（线程安全），install 时灌入；
  install 之后直接进工具注册表（GUI 线程同步，其他线程排队投递）。
  未设 `QT_MCP_PROBE=1` 时全部 no-op。
- 生命周期：`context` 语义同 `QObject::connect`——context 销毁时自动注销该命令，
  lambda 捕获 `this` 不会悬垂。也可手工 `QtMcp::unregisterCommand(name)`。

**可用性的三层表达**：① 调用时强制检查——`tools/call` 到达时先跑
`AvailabilityCheck`，非空原因 → `isError` + `"Command '<name>' is not available
now: <reason>"`，AI 必然收到原因；② `qt_app_commands` 状态工具——返回所有自定义命令的
`{name, description, available, reason}` 快照，AI 可在规划阶段先查，避免试错；
③ 在 description 里写明前置条件（约定，不强制）。

**线程约定**：handler 与可用性检查都运行在宿主 GUI 线程的事件循环里，可直接读写
控件状态。耗时任务不要在 handler 里同步执行（会冻结界面和 MCP）——启动后台任务后
立即返回，进度通过 `QtMcp::postMessage()` 上报，AI 用 `qt_host_messages` 轮询。

## 宿主消息推送与容量

宿主还可以主动向 agent 推送运行状态（可选，不调用则 `qt_host_messages` 恒为空）：

```cpp
QtMcp::postMessage("仿真完成，误差 0.3%", "info");  // 任意线程可调用；探针未启用时零开销 no-op
```

暂存区容量默认 500 条（超出丢最旧并在 `dropped` 计数），构建期可调：
CMake `-DQTMCP_HOST_LOG_CAPACITY=N`，qmake `QTMCP_HOST_LOG_CAPACITY=N`。
`qt_debug_message` 的 Qt 内部消息缓冲容量同理：
CMake `-DQTMCP_MESSAGE_LOG_CAPACITY=N`，qmake `QTMCP_MESSAGE_LOG_CAPACITY=N`（默认 500）。

## 环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| `QT_MCP_PROBE` | 未设置 | `1` 时启用；未设置时 `install()` 完全惰性 |
| `QT_MCP_PORT` | `9142` | 监听端口 |
| `QT_MCP_HOST` | `127.0.0.1` | 监听地址（改成非 localhost 前请知悉：该端口等价于进程的完全控制权） |

# QtMcpEmbedded

把 MCP (Model Context Protocol) server 直接嵌入 Qt Widgets 应用的 C++ 库——链接一次、加一行代码，运行中的程序就在本机端口提供完整的自动化调试与操作能力，供 Claude Code 等 AI agent 直连使用。类似 Playwright MCP，但面向桌面 Qt 程序。

- 单进程架构：**无 Python、无外部进程**，MCP server 运行在应用自己的 GUI 线程里
- 传输：MCP **Streamable HTTP**（单 `/mcp` 端点），默认绑定 `127.0.0.1:9142`
- Qt5（≥5.15 已验证）/ Qt6（同源码，待验证）双支持
- 以 [qt-mcp](https://github.com/0xCarbon/qt-mcp)（Python/PySide6）为设计蓝本，工具集与行为语义和其保持一致（详见文末"蓝本与致谢"）

## 快速开始

### 接入你的应用

以 `add_subdirectory` 方式接入（clone 或 git submodule 放进项目树均可）：

```cmake
add_subdirectory(third_party/QtMcpEmbedded)   # 作为子项目时不会构建本仓库的 examples
target_link_libraries(your_app PRIVATE QtMcpEmbedded)
```

qmake 项目则在 `.pro` 里加一行：

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

可选的宿主自我说明（会并入 MCP `initialize` 响应的 `instructions`，AI 连上即得应用地图）：

```cpp
QtMcp::InstallOptions opts;
opts.appName = "My Simulator";
opts.instructions = "主流程：先…再…；重要控件：objectName xxx 是…";
QtMcp::install(opts);
```

### 两阶段启动

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

### 自定义命令（registerCommand）

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

宿主还可以主动向 agent 推送运行状态（可选，不调用则 `qt_host_messages` 恒为空）：

```cpp
QtMcp::postMessage("仿真完成，误差 0.3%", "info");  // 任意线程可调用；探针未启用时零开销 no-op
```

暂存区容量默认 500 条（超出丢最旧并在 `dropped` 计数），构建期可调：
CMake `-DQTMCP_HOST_LOG_CAPACITY=N`，qmake `QTMCP_HOST_LOG_CAPACITY=N`。
`qt_debug_message` 的 Qt 内部消息缓冲容量同理，参数为
`-DQTMCP_MESSAGE_LOG_CAPACITY=N`（默认 500）。

### 环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| `QT_MCP_PROBE` | 未设置 | `1` 时启用；未设置时 `install()` 完全惰性 |
| `QT_MCP_PORT` | `9142` | 监听端口 |
| `QT_MCP_HOST` | `127.0.0.1` | 监听地址（改成非 localhost 前请知悉：该端口等价于进程的完全控制权） |

### 客户端接入（Claude Code 等）

```json
{ "mcpServers": { "my-app": { "type": "http", "url": "http://127.0.0.1:9142/mcp" } } }
```

注意：宿主程序退出时 MCP server 随之关闭，连接断开属于正常终止。

## 工具一览（21 个）

| 工具 | 说明 |
|---|---|
| `qt_snapshot` | widget 树结构化快照（含 ref、tooltip、勾选/隐藏/禁用标记；内联 list/tree/table 条目及点击坐标） |
| `qt_find_widget` | 按 objectName/类名/文本/模式查找控件（类名匹配忽略 C++ 命名空间，`CDockWidget` 可命中 `ads::CDockWidget`） |
| `qt_widget_details` | 控件或 tree 条目的完整属性（含控件挂载的 QAction 列表，可发现 Ribbon 组等容器上可触发的动作） |
| `qt_object_tree` / `qt_list_windows` | QObject 树 / 顶层窗口（每个窗口带 ref 与标题，可直接用于截图等后续操作） |
| `qt_active_popup` | 当前模态/弹出窗口（标题、文本、全部按钮的可点击 ref）——处理 messagebox、保存确认等阻塞弹窗的入口 |
| `qt_screenshot` | 窗口或控件截图（PNG/JPEG，基于真实渲染结果） |
| `qt_click` | 点击（widget 与 tree item 均可；双击、右键含上下文菜单、修饰键、坐标；`row`/`col` 直达任意 item view（QListView/QTableView/QTreeView/QTableWidget）的单元格，屏外自动滚入；`item_text` 按文本命中条目，树形自动展开） |
| `qt_file_dialog` | 处理当前活动的 QFileDialog：填路径并确认/取消（打开/保存/选目录均可）。探针 install() 时设置 `Qt::AA_DontUseNativeDialogs`，之后创建的文件对话框都是 Qt 控件形态、可被驱动 |
| `qt_drag` | 拖拽（源 widget → 目标 widget；路径上移动真实光标以兼容读取 `QCursor::pos()` 的拖拽实现，如 ADS dock 重排） |
| `qt_type_text` / `qt_key_press` | 文本输入 / 按键（含焦点管理） |
| `qt_set_property` | 写属性（含写后读回 `value`，可发现被校验逻辑拒绝的写入；支持 tree item 伪属性 expanded/checked/selected/text） |
| `qt_invoke_slot` | 调用槽/invokable 方法（≤4 参数；方法名可裸写或带签名，如 `toggleView` / `toggleView(bool)`） |
| `qt_get_text` | 提取文本（widget 或 tree item；隐藏控件也可读；对 item view 转储模型文本，行以换行、列以制表符分隔） |
| `qt_trigger_action` | 触发 QAction（菜单/工具栏；按文本匹配时递归搜索子菜单条目） |
| `qt_wait_for` | 等待条件（widget_visible / window_count_changed / property_equals；超时报告最后观测值） |
| `qt_batch` | 一次往返顺序执行多步，失败即停 |
| `qt_debug_message` | 读取 Qt 内部消息（qDebug/qWarning 等）环形缓冲 |
| `qt_host_messages` | 读取宿主通过 `QtMcp::postMessage()` 主动推送的消息（读后清空暂存区，不会重复送达；宿主不推送则恒为空） |
| `qt_app_commands` | 宿主自定义命令（`QtMcp::registerCommand` 注册）快照：每条 `{name, description, available, reason}`，可用性为实时评估 |

行为约定：操作类工具采用**异步事件投递**（不会在模态 `exec()` 上死锁）；对隐藏/禁用/被模态阻断的目标**快速失败**并给出原因（`force=true` 可绕过）；ref 全局单调编号，跨 snapshot 稳定，绝不静默重绑（包括宿主分配器复用已销毁控件地址的情形：旧 ref 一律明确报错，不会指向新控件）。

**不能承诺的行为边界**（agent 使用时必须知晓）：

- **事件投递即返回，不保证效果**。`qt_click`/`qt_type_text`/`qt_key_press` 把事件投进宿主事件队列就返回成功；目标是否真的响应（按钮被点中、文本被接受），要用 `qt_wait_for` / `qt_get_text` / 属性读回来断言。不要用固定 sleep 代替条件等待。
- **宿主 GUI 线程阻塞时一切免谈**。所有工具都运行在宿主事件循环里；宿主做长耗时同步操作（重计算、阻塞 IO）期间，请求会排队到其恢复。探针无法绕过，也没有超时兜底。
- **ref 只在控件存活期间有效**。对话框、动态页面销毁后旧 ref 报 "ref not found"，必须重新 `qt_find_widget`——哪怕新对话框看起来和前一个一模一样。
- **坐标点击（`position`）假设布局静止**。窗口缩放、面板展开/折叠、内容滚动都会使坐标失效；能按 ref/item/row/col 寻址就不要用坐标。纯自绘组件（如 Qtitan Grid 的行、QGraphicsScene 条目）没有子控件 ref，坐标（或专用场景工具）是唯一途径。
- **截图是离屏渲染（`QWidget::grab`）**，不依赖窗口在屏幕上可见；但对绕过 Qt 绘制链的控件（原生子窗口、OpenGL/DirectX 直绘、Qtitan Grid 的 GraphicControl）可能得到空白/底色——此时改截其父容器或顶层窗口。
- **`qt_wait_for` / `qt_batch` 的等待不是无副作用的**：等待期间宿主的事件循环照常运转（定时器、动画、网络回调都会执行），界面状态可能自行变化。
- **`qt_drag` 会移动物理鼠标光标**（兼容读 `QCursor::pos()` 的拖拽实现），执行期间不要动鼠标；Wayland 上不可用。
- **模态语义按 Qt 规则**：存在应用模态窗口时，对窗口外控件的操作被拒绝（`force=true` 可绕过，但事件仍会被 Qt 的模态过滤器丢弃——force 只对"隐藏/禁用"类守卫真正有效）。
- **只有 Qt 控件树内的界面可被感知**。探针的内省与操作建立在 QObject/QWidget 树和 Qt 事件队列上：经其他渠道创建的界面——直接调 Win32 API 的对话框（`GetOpenFileName`/`IFileDialog` 等）、嵌入的原生子窗口（HWND/CWnd）、QtWebEngine 的页面 DOM、OpenGL/DirectX 直绘内容——不会出现在快照里，也收不到合成事件。Qt 自带的文件/颜色/字体对话框默认在 Windows 上是**操作系统原生窗口**，本属此类；探针在 `install()` 时设置 `Qt::AA_DontUseNativeDialogs` 把它们静默切换为 Qt 控件实现（宿主无需改任何代码，`qt_file_dialog` 即可驱动），但该属性管不到绕过 Qt 对话框类、直接调用 OS API 的代码路径——那些需要宿主自行改造，否则对 agent 不可见。
- **单会话**：一个探针实例同一时刻只服务一个 MCP 会话。新的 `initialize` 会让旧会话立即失效（后续请求报 404 "Missing or invalid Mcp-Session-Id"）——不要并发连接同一端口；长时间任务期间如需查看状态，用宿主日志（`qt_host_messages`）而不是另开会话。

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

## 验证

```bash
# 终端 1：GUI 模式启动 demo（窗口会出现在桌面上）
QT_MCP_PROBE=1 ./build/examples/demo_app/Debug/demo_app.exe

# 终端 2：运行端到端验证（80+ 项断言，覆盖全部工具与阻塞场景）
cd client && uv sync && uv run python verify.py
```

`verify.py` 只依赖 MCP 接口、不依赖实现语言，可直接复用于验证任何实现了同一工具方言的探针。注意：脚本最后会让 demo 走"退出→保存确认→Discard"流程自行退出，属预期行为。

也支持无头（offscreen）模式验证，CI 可用：

```bash
QT_MCP_PROBE=1 QT_QPA_PLATFORM=offscreen ./build/examples/demo_app/Debug/demo_app.exe
```

> 离屏平台有两个 Qt 层级的坑，库内 `HeadlessCompat` 已做变通（仅在检测到 offscreen/minimal 时启用，桌面平台不受影响）：
> - **Windows 下 QMessageBox 崩溃**（Qt 5.15 bug）：`QMessageBox::showEvent` 无条件调用 `qt_getWindowsSystemMenu()`，而 offscreen/minimal 的 `platformNativeInterface()` 为 `nullptr`，静态函数 `QMessageBox::warning()` 等一弹即段错误。守卫拦截发往 QMessageBox 的 Show 事件并用公开 API 复现其行为（跳过仅装饰性的系统菜单项调整）。
> - **焦点死区**：offscreen 在窗口 show 时会激活它，但模态对话框关闭后不会重新激活父窗口，此后 `setFocus()` 静默失效、无 ref 的键盘操作无处投递。守卫在活动的瞬态窗口隐藏时重新激活其父窗口。

自定义命令（`registerCommand`/`qt_app_commands`/可用性拒绝路径）的专项验证：

```bash
uv run python verify_commands.py   # 同样要求 demo_app 已以 QT_MCP_PROBE=1 启动；不会退出 demo
```

## 仓库结构

```
src/
  QtMcp.h              # 公开头：QtMcp::install()
  core/                # ProbeServer 装配、RefRegistry（ref → QPointer，单调编号）
  transport/           # QTcpServer 上的极简 HTTP/1.1 + SSE
  protocol/            # JSON-RPC 2.0、MCP 会话、工具注册与分发
  tools/               # Introspector / Interactor / Screenshotter / MessageLog / HostLog
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

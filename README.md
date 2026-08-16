# QtMcpEmbedded

把 MCP (Model Context Protocol) server 直接嵌入 Qt Widgets 应用的 C++ 库——链接一次、加一行代码，运行中的程序就在本机端口提供完整的自动化调试与操作能力，供 Claude Code 等 AI agent 直连使用。类似 Playwright MCP，但面向桌面 Qt 程序。

- 单进程架构：**无 Python、无外部进程**，MCP server 运行在应用自己的 GUI 线程里
- 传输：MCP **Streamable HTTP**（单 `/mcp` 端点），默认绑定 `127.0.0.1:9142`
- Qt5（≥5.15 已验证）/ Qt6（同源码，待验证）双支持
- 以 [qt-mcp](https://github.com/0xCarbon/qt-mcp)（Python/PySide6）为设计蓝本，工具集与行为语义和其保持一致（详见文末"蓝本与致谢"）

## 快速开始

### 接入你的应用

```cmake
target_link_libraries(your_app PRIVATE QtMcpEmbedded)
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

## 工具一览（17 个）

| 工具 | 说明 |
|---|---|
| `qt_snapshot` | widget 树结构化快照（含 ref、tooltip、勾选/隐藏/禁用标记；内联 list/tree/table 条目及点击坐标） |
| `qt_find_widget` | 按 objectName/类名/文本/模式查找控件 |
| `qt_widget_details` | 控件或 tree 条目的完整属性 |
| `qt_object_tree` / `qt_list_windows` | QObject 树 / 顶层窗口 |
| `qt_active_popup` | 当前模态/弹出窗口（标题、文本、全部按钮的可点击 ref）——处理 messagebox、保存确认等阻塞弹窗的入口 |
| `qt_screenshot` | 窗口或控件截图（PNG/JPEG，基于真实渲染结果） |
| `qt_click` | 点击（widget 与 tree item 均可；双击、右键含上下文菜单、修饰键、坐标） |
| `qt_type_text` / `qt_key_press` | 文本输入 / 按键（含焦点管理） |
| `qt_set_property` | 写属性（含写后读回 `value`，可发现被校验逻辑拒绝的写入；支持 tree item 伪属性 expanded/checked/selected/text） |
| `qt_invoke_slot` | 调用槽/invokable 方法（≤4 参数） |
| `qt_get_text` | 提取文本（widget 或 tree item） |
| `qt_trigger_action` | 触发 QAction（菜单/工具栏） |
| `qt_wait_for` | 等待条件（widget_visible / window_count_changed / property_equals；超时报告最后观测值） |
| `qt_batch` | 一次往返顺序执行多步，失败即停 |
| `qt_messages` | 读取 Qt 内部消息（qWarning 等）环形缓冲 |

行为约定：操作类工具采用**异步事件投递**（不会在模态 `exec()` 上死锁）；对隐藏/禁用/被模态阻断的目标**快速失败**并给出原因（`force=true` 可绕过）；ref 全局单调编号，跨 snapshot 稳定，绝不静默重绑。

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

# 终端 2：运行端到端验证（72+ 项断言，覆盖全部工具与阻塞场景）
cd client && uv sync && uv run python verify.py
```

`verify.py` 只依赖 MCP 接口、不依赖实现语言，可直接复用于验证任何实现了同一工具方言的探针。注意：脚本最后会让 demo 走"退出→保存确认→Discard"流程自行退出，属预期行为。

## 仓库结构

```
src/
  QtMcp.h              # 公开头：QtMcp::install()
  core/                # ProbeServer 装配、RefRegistry（ref → QPointer，单调编号）
  transport/           # QTcpServer 上的极简 HTTP/1.1 + SSE
  protocol/            # JSON-RPC 2.0、MCP 会话、工具注册与分发
  tools/               # Introspector / Interactor / Screenshotter / MessageLog
examples/demo_app/     # 覆盖 20+ 控件与各类联动/阻塞场景的测试程序
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

# QtMcpEmbedded — Qt Widgets 应用内嵌 MCP 服务库 · 设计计划

> 目标：一个 C++ 库，已有 Qt5/Qt6 Widgets 程序通过 **一次链接 + 一行代码** 即可获得 MCP 服务能力，
> 程序启动后在指定端口提供 Streamable HTTP 传输的 MCP server，
> 具备 qt-mcp（PySide6）项目的全部自动化调试与自动化操作功能。

参考实现：`qt-mcp/`（本仓库内的开源项目，其 probe 端约 1500 行 Python，功能设计的蓝本）。

---

## 1. 目标与非目标

### 目标

- 单进程架构：MCP server 直接内嵌在目标 Qt 应用内，**无 Python、无外部进程**。
- 集成成本：CMake `target_link_libraries` + main() 里一行 `QtMcp::install()`。
- 传输：仅实现 MCP **Streamable HTTP**（规范 2025-03-26 起的唯一远程传输），
  单一 `/mcp` 端点：POST 携带 JSON-RPC 请求（同步返回 JSON），GET 打开 SSE 格式长连接
  （SSE 是 Streamable HTTP 内部的流式帧格式，不是已废弃的"HTTP+SSE 双端点传输"，后者不实现）。
  默认绑定 `127.0.0.1`。
- 功能对齐 qt-mcp：widget 树内省、属性读写、事件注入（点击/键盘/文本）、截图、
  Qt 消息捕获、线程/信号/布局检查、QGraphicsScene 内省、wait_for、batch。
- Qt5（≥5.12，重点 5.15）与 Qt6 双支持，同一份源码。
- 环境变量总开关，不启用时零开销。

### 非目标（本期不做）

- VTK/PyVista 3D 场景支持（qt-mcp 有此功能，C++ 侧 VTK 集成差异大，列为后续可选模块）。
- 远程访问 / TLS / 多客户端并发 / OAuth 认证（仅 localhost 单客户端调试场景）。
- 零源码修改的注入（桌面 C++ 无可移植的自动加载机制，一行显式调用是下限）。
- QML/QtQuick 内省（仅 Widgets + QGraphicsScene）。

---

## 2. 总体架构

```
┌──────────────────────────────────────────────────┐
│  目标 Qt Widgets 应用（Qt5 或 Qt6）               │
│                                                  │
│  QtMcp::install()  ──► ProbeServer (QObject)     │
│                          │                       │
│        ┌─────────────────┼───────────────────┐   │
│        ▼                 ▼                   ▼   │
│  ┌───────────┐   ┌──────────────┐   ┌────────┐ │
│  │ Transport │   │   Protocol   │   │ 工具层 │ │
│  │ 极简 HTTP │──►│ MCP/JSON-RPC │──►│（8 个  │ │
│  │ on QTcp   │◄──│  分发 + 会话  │◄──│ 模块） │ │
│  └───────────┘   └──────────────┘   └────────┘ │
│        全部运行在 GUI 线程事件循环内              │
└──────────────────────────────────────────────────┘
         ▲ 127.0.0.1:<port>  (默认 9142)
         │ Streamable HTTP
┌────────┴─────────┐
│  AI Agent        │  Claude Code 等 MCP 客户端，
│  (MCP client)    │  以 HTTP transport 直连
└──────────────────┘
```

与 qt-mcp 的关键差异：qt-mcp 是「进程内 probe 说私有 JSON-RPC + 外部 Python 进程翻译 MCP」
两段式；本方案把协议翻译层也放进进程内，消灭外部进程。

### 分层原则

- **Transport 层与 Protocol 层严格解耦**：之间只传「完整 JSON 文档进 / 完整 JSON 或 SSE 事件流出」。
  日后替换传输实现（Qt6 QHttpServer、反向代理等）时协议层与工具层零改动。
- **Protocol 层与工具层解耦**：工具以「名称 + JSON Schema + handler(QJsonObject→QJsonObject)」
  注册，协议层不知道任何 Qt 内省细节。

---

## 3. 集成形态（用户视角）

```cpp
#include <QtMcpEmbedded/QtMcp>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QtMcp::install();   // 唯一一行。读环境变量，未启用则立即返回，零开销
    ...
    return app.exec();
}
```

行为由环境变量控制（install() 无参，全部走环境变量，保证"一行"承诺）：

| 变量 | 默认 | 说明 |
|---|---|---|
| `QT_MCP_PROBE` | 未设置 | `1` 时启用；未设置时 `install()` 是 no-op |
| `QT_MCP_PORT` | `9142` | 监听端口 |
| `QT_MCP_HOST` | `127.0.0.1` | 监听地址（明确允许非 localhost，风险自负） |

打包形态：CMake 工程，产出静态库 + `QtMcpEmbeddedConfig.cmake`，
支持 `find_package(QtMcpEmbedded)` 与 `add_subdirectory` 两种接入。
同时提供 qmake `.pri` 文件（Qt5 时代项目仍大量用 qmake）。

---

## 4. 模块划分

```
src/
├── QtMcp.h                    // 唯一公开头文件：QtMcp::install()
├── core/
│   ├── ProbeServer.{h,cpp}    // 顶层 QObject：装配三层、持有注册表
│   └── RefRegistry.{h,cpp}    // ref 字符串 → QPointer<QObject>，防野指针
├── transport/
│   ├── HttpServer.{h,cpp}     // QTcpServer 上的极简 HTTP/1.1
│   ├── HttpRequest/Response   // 解析产物
│   └── SseStream.{h,cpp}      // 挂在 QTcpSocket 上的 SSE 写出器
├── protocol/
│   ├── McpSession.{h,cpp}     // initialize 握手、协议版本协商、会话 id
│   ├── McpDispatcher.{h,cpp}  // tools/list、tools/call 分发
│   ├── ToolRegistry.{h,cpp}   // 工具注册：name + schema + handler
│   └── JsonRpc.{h,cpp}        // JSON-RPC 2.0 帧解析/构造、错误码
└── tools/
    ├── Introspector.{h,cpp}   // snapshot / object_tree / list_windows / find_widget / widget_details / active_popup / menu_items
    ├── Interactor.{h,cpp}     // click / type_text / key_press / set_property / invoke_slot / wait_for / get_text / trigger_action / batch
    ├── Screenshotter.{h,cpp}  // screenshot（PNG → base64 → MCP image content）
    ├── MessageLog.{h,cpp}     // qt_debug_message（qInstallMessageHandler 环形缓冲）
    ├── ThreadInspector.{h,cpp}// thread_check
    ├── SignalInspector.{h,cpp}// signals
    ├── LayoutInspector.{h,cpp}// layout_check
    └── SceneInspector.{h,cpp} // scene_snapshot / scene_item_details
```

每个 tools/ 模块是 qt-mcp 同名 Python 模块的直译（`qt-mcp/src/qt_mcp/probe/*.py`），
API 对照天然存在，可逐个对照移植与测试。

---

## 5. MCP 工具清单（对齐 qt-mcp）

工具名沿用 qt-mcp 的 `qt_*` 命名，参数 schema 尽量与其 MCP server
（`qt-mcp/src/qt_mcp/server/mcp_server.py`）一致，方便已有 prompt/工作流复用。

| 工具 | 说明 | 移植来源（qt-mcp probe 方法） |
|---|---|---|
| `qt_snapshot` | 整棵 widget 树的结构化快照（含 ref） | `introspector.snapshot` |
| `qt_widget_details` | 指定 widget 的全部属性 | `widget_details` |
| `qt_object_tree` | QObject 父子树 | `object_tree` |
| `qt_list_windows` | 顶层窗口列表 | `list_windows` |
| `qt_find_widget` | 按 objectName/类名查找 | `find_widget` |
| `qt_active_popup` | 当前活动弹出窗口 | `active_popup` |
| `qt_menu_items` | 菜单/菜单栏条目 | `menu_items` |
| `qt_screenshot` | widget 或窗口截图（PNG） | `screenshotter.screenshot` |
| `qt_click` | 点击（左/右/中键、修饰键、坐标） | `interactor.click` |
| `qt_type` | 输入文本（逐键或剪贴板粘贴） | `type_text` |
| `qt_key_press` | 按键（Return/Esc/Ctrl+S…） | `key_press` |
| `qt_set_property` | 写 Qt 属性 | `set_property` |
| `qt_invoke_slot` | 调用槽/方法 | `invoke_slot` |
| `qt_wait_for` | 等待条件（控件可见/窗口数变化/属性等于） | `wait_for` |
| `qt_get_text` | 提取文本内容 | `get_text` |
| `qt_trigger_action` | 触发 QAction | `trigger_action` |
| `qt_batch` | 一次调用顺序执行多步 | `batch` |
| `qt_debug_message` | 读取并清空 Qt 内部消息缓冲 | `qt_messages` |
| `qt_thread_check` | 非 GUI 线程操作 widget 的检测 | `thread_check` |
| `qt_signals` | 信号连接检查 | `signals` |
| `qt_layout_check` | 布局问题检测 | `layout_check` |
| `qt_scene_snapshot` | QGraphicsScene 条目枚举 | `scene_snapshot` |
| `qt_scene_item_details` | 场景条目详情 | `scene_item_details` |

共 23 个工具。VTK 两个工具（`qt_vtk_*`）不在本期范围。

---

## 6. 线程模型（本设计最重要的决策）

**一切运行在 GUI 线程。**

- `QTcpServer` 由 `ProbeServer` 持有，`newConnection`/`readyRead` 都是事件循环内信号，
  请求解析、MCP 分发、工具执行全部发生在 GUI 线程——操作 widget 零封送、零锁。
- 单客户端串行负载下无并发需求；多个 TCP 连接被接受但请求是事件循环内自然串行的。
- `qt_wait_for` / `qt_batch` 内部像 qt-mcp 一样调用 `QCoreApplication::processEvents()`
  轮询——由于 server 本身也在事件循环里，processEvents 期间可以处理新请求（重入）。
  需在 `ProbeServer` 上加重入守卫：等待期间到达的新工具调用排队，等待结束后处理，
  防止等待中的请求交错操作 widget 状态。
- SSE 长连接（GET /mcp）：`QTcpSocket` 挂着，服务端有通知时直接 `write()`，天然异步。
  本期实际上没有服务端主动通知（notifications）需求，SSE 流主要为协议合规，
  保持空流 + 心跳注释行即可。

---

## 7. HTTP 传输层设计（自研，不引入第三方库）

在 `QTcpServer`/`QTcpSocket` 上实现 HTTP/1.1 的**最小合规子集**：

**请求解析（状态机，逐 socket 一个 buffer）：**
- 请求行 + 头部（大小写不敏感），头部总大小上限 64 KB，超限返 431；
- 仅支持 `Content-Length` 请求体，buffer 攒够长度才上交；body 上限 16 MB（截图参数不会大，但防御性设限）；
- `Transfer-Encoding: chunked` 请求体 → 直接返 501（合规 MCP 客户端不会发）；
- keep-alive 支持；不支持管线化（上一个响应未发完前不解析下一个请求）。

**路由：**
- `POST /mcp` → JSON-RPC 请求/通知，Content-Type `application/json`；
- `GET /mcp` → SSE 流响应（`Content-Type: text/event-stream`，无 Content-Length，持续写出）；
- `DELETE /mcp` → 会话终止（规范可选，实现为空操作返 200）；
- 其余 → 404。

**响应：** 由本方生成，`Content-Length` 精确计算，无分块难题。

**安全（MCP 规范对 HTTP 传输的强制项）：**
- 校验 `Origin` 头：仅允许 `null` / `localhost` / `127.0.0.1` 来源，防 DNS rebinding；
- 校验 `MCP-Protocol-Version` 头并做版本协商；
- 会话管理：`initialize` 响应分配 `Mcp-Session-Id`，后续请求校验。

**工作量估计：** 250~350 行 + 专项单测（含模糊输入用例）。

---

## 8. MCP 协议层设计

- **Pin 协议版本 `2025-06-18`**（Streamable HTTP 已稳定、各客户端广泛支持），
  握手时按规范协商降级；SSE 旧传输（2024-11-05）不实现。
- 实现的方法子集（server 视角）：
  - `initialize` / `notifications/initialized`
  - `ping`
  - `tools/list`（23 个工具的 JSON Schema）
  - `tools/call`（分发到 ToolRegistry，handler 返回 text content 或 image content）
  - 其余方法（resources/prompts/logging 等）→ 标准 JSON-RPC `-32601 Method not found`
- 错误映射：工具内部异常 → `tools/call` 结果 `isError: true` + 文本描述（MCP 约定），
  协议级错误 → JSON-RPC error response。
- 工具执行结果序列化：结构化数据走 text content（JSON 字符串），
  截图走 image content（`data: base64, mimeType: image/png`）。

---

## 9. JSON ↔ QVariant 类型转换

`qt_set_property` / `qt_invoke_slot` 需要把 JSON 值转为目标 `QMetaType`。
C++ 无鸭子类型，实现一张转换表：

- 一期覆盖：`int/uint/double/bool/QString/QStringList/QPoint/QPointF/QSize/QRect/`
  `QColor/QFont/QUrl`、任意注册 enum（按名或按值）。
- `QMetaProperty::write` 失败或类型不支持 → 明确报错（列出目标类型名），不静默吞掉。
- `invoke_slot`：通过 `QMetaMethod::parameterTypes()` 逐项转换后用
  `QGenericArgument` 调用；仅支持无返回值或简单返回值（先序列化为 JSON 返回）。

读路径（内省方向）用 `QVariant::toJsonValue()` 为主，特例（QRect 等）
手写转换，与 qt-mcp 的 `introspector.py` 序列化格式保持一致。

---

## 10. Qt5 / Qt6 兼容策略

- **全部使用 unscoped enum 写法**（`Qt::LeftButton`、`QEvent::MouseButtonPress`），
  Qt5/Qt6 通吃，无版本分支。这是从 qt-mcp 移植分析得出的关键结论：
  其全部功能 API 在 Qt 5.0+ 均存在。
- CMake 层：`find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)` 标准写法，
  用户链接哪个 Qt 就编哪个版本；同一份源码。
- 少量真实差异用 `#if QT_VERSION` 隔离（预计 ≤5 处，如 `QString::SkipEmptyParts`、
  `QEnterEvent`、高 DPI 属性默认值）。
- C++ 标准：C++17（Qt5 时代编译器普遍支持，不引入 C++20 依赖）。

---

## 11. 测试策略

| 层 | 手段 |
|---|---|
| transport | 纯单测：构造字节流喂给解析器（完整/分包/畸形/超限/keep-alive 复用），不碰 Qt |
| protocol | 单测：JSON-RPC 帧 + initialize/tools/list/tools/call 全流程，mock ToolRegistry |
| tools | 集成测试：offscreen platform（`QT_QPA_PLATFORM=offscreen`）起真实 QApplication，搭一个覆盖 QMainWindow/对话框/QTreeWidget/QGraphicsView 的 sample app，走真实 127.0.0.1 端口端到端调每个工具 |
| 兼容性 | CI 矩阵：Qt 5.15 + Qt 6.x × Linux/Windows（macOS 可选） |

测试基建参考 qt-mcp 的做法（它用 pytest offscreen 跑真实 app）；
本仓库测试用 Qt Test 或 GoogleTest（待定，倾向 GoogleTest，社区普及度高）。

另设一个 `examples/demo_app/`：一个按钮/输入框/菜单/GraphicsView 齐全的小程序，
既是手动验证工具也是集成测试宿主。

---

## 12. 里程碑

### M0 — 骨架与传输层（风险最先消灭）
- CMake 工程、CI、demo_app
- HttpServer + SseStream + 单测
- 验收：curl 手搓 POST /mcp 能得到 JSON-RPC pong

### M1 — 协议层
- JsonRpc / McpSession / ToolRegistry / McpDispatcher，2 个占位工具（ping、list_windows）
- 验收：**Claude Code 以 HTTP transport 直连成功**，`tools/list` 可见

### M2 — 内省类工具（只读，安全先行）
- RefRegistry、Introspector、Screenshotter、MessageLog、SceneInspector
- 验收：对 demo_app 完成 snapshot → 按 ref 截图 → widget_details 闭环

### M3 — 操作类工具
- Interactor 全部（click/type/key/set_property/invoke_slot/trigger_action/get_text）
  + wait_for + batch + 重入守卫
- 验收：agent 对 demo_app 完成「点击按钮 → 等待对话框 → 输入文本 → 断言属性」全流程

### M4 — 调试类工具 + 收尾
- ThreadInspector、SignalInspector、LayoutInspector
- qmake .pri、README（集成指南）、examples 完善、CI Qt5/Qt6 矩阵
- 验收：在一个真实的第三方 Qt5 Widgets 项目上完成「一行接入」验证

每个里程碑结束用真实 MCP 客户端（Claude Code）做冒烟验证，而非仅自测。

---

## 13. 风险与开放问题

| 风险 | 等级 | 缓解 |
|---|---|---|
| 自研 HTTP 解析的边角 bug | 中 | 窄子集 + 专项单测/模糊用例；接口隔离，可整体替换 |
| `processEvents()` 重入导致状态错乱 | 中 | 请求排队守卫；复用 qt-mcp 已验证的等待模型 |
| MCP 规范继续演进 | 低 | pin 2025-06-18 + 版本协商；协议层隔离 |
| JSON→QVariant 覆盖不全的类型 | 低 | 明确报错 + 按需扩展转换表 |
| 目标项目用 qmake 而非 CMake | 低 | 提供 .pri |
| Qt 5.12~5.14 的冷门 API 差异 | 低 | CI 覆盖 5.15，更老版本按需支持 |

**开放问题（动手前需确认）：**
1. 测试框架选型：GoogleTest 还是 Qt Test？（倾向 GoogleTest）
2. 最低 Qt 版本：5.15 即可，还是要覆盖 5.12？
3. 是否需要 `QT_MCP_PROBE` 之外的编译期开关（`QT_MCP_DISABLE` 宏让 release 版彻底剔除）？
4. demo/验证用的真实 Qt5 项目用哪个？

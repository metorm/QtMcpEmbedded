# 客户端接入与验证

[English](client.md) | **中文**

## 客户端接入（Claude Code 等）

```json
{ "mcpServers": { "my-app": { "type": "http", "url": "http://127.0.0.1:9142/mcp" } } }
```

会话注意事项：

- 宿主程序退出时 MCP server 随之关闭，连接断开属于正常终止。
- **单会话**：一个探针实例同一时刻只服务一个 MCP 会话。新的 `initialize` 会让旧
  会话立即失效（后续请求报 404 "Missing or invalid Mcp-Session-Id"）——不要并发
  连接同一端口；长时间任务期间如需查看状态，用宿主日志（`qt_host_messages`）而不是
  另开会话。（亦收录于 [pitfalls.zh-CN.md](pitfalls.zh-CN.md)。）

## 验证

除了交互式 agent，同一端点也可以用普通 Python 脚本驱动，实现确定性的
自动化与回归测试——见 [automation.zh-CN.md](automation.zh-CN.md)。

`client/` 目录是 uv 环境，先执行一次 `uv sync` 完成初始化。

```bash
# 终端 1：GUI 模式启动 demo（窗口会出现在桌面上）
QT_MCP_PROBE=1 ./build/examples/demo_app/Debug/demo_app.exe

# 终端 2：运行端到端验证（80+ 项断言，覆盖全部工具与阻塞场景）
cd client && uv sync && uv run python verify.py
```

`verify.py` 只依赖 MCP 接口、不依赖实现语言，可直接复用于验证任何实现了同一工具
方言的探针。注意：脚本最后会让 demo 走"退出→保存确认→Discard"流程自行退出，属预期行为。

也支持无头（offscreen）模式验证，CI 可用：

```bash
QT_MCP_PROBE=1 QT_QPA_PLATFORM=offscreen ./build/examples/demo_app/Debug/demo_app.exe
```

> 离屏平台有两个 Qt 层级的坑，库内 `HeadlessCompat` 已做变通（仅在检测到
> offscreen/minimal 时启用，桌面平台不受影响）。详见
> [pitfalls.zh-CN.md](pitfalls.zh-CN.md)。

自定义命令（`registerCommand`/`qt_app_commands`/可用性拒绝路径）的专项验证：

```bash
uv run python verify_commands.py   # 同样要求 demo_app 已以 QT_MCP_PROBE=1 启动；不会退出 demo
```

# 脚本化自动化与测试

[English](automation.md) | **中文**

除了交互式 AI agent，探针也可以用普通 Python 脚本驱动——这就把它变成了
Qt 应用的自动化/回归测试设施：同样的协议、同样的工具，但确定、可重复、
适合 CI。本仓库自带的验收套件（`client/verify.py`、
`client/verify_commands.py`）就是这种方式：只通过 MCP 通信、完全不 import
被测应用任何东西的 Python 脚本。

## 前置条件

- 应用以 `QT_MCP_PROBE=1` 运行（无头 CI 再加
  `QT_QPA_PLATFORM=offscreen`——见 [client.zh-CN.md](client.zh-CN.md)）。
- 装有[官方 MCP SDK](https://pypi.org/project/mcp/) 的 Python 环境：
  `pip install mcp`（本仓库 `client/` 目录就是现成的 uv 环境：`uv sync`）。

## 最小脚本

```python
import asyncio
import json

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

SERVER_URL = "http://127.0.0.1:9142/mcp"   # QT_MCP_HOST:QT_MCP_PORT + /mcp


async def call(session, name, args=None):
    """调用工具；协议级或工具级失败都抛异常。"""
    result = await session.call_tool(name, args or {})
    text = result.content[0].text if result.content else ""
    if result.is_error:
        raise RuntimeError(f"{name} failed: {text}")
    return json.loads(text) if text else {}


async def main():
    async with streamable_http_client(SERVER_URL) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            found = await call(session, "qt_find_widget",
                               {"object_name": "applyButton"})
            ref = found["widgets"][0]["ref"]

            await call(session, "qt_click", {"ref": ref})
            status = await call(session, "qt_find_widget",
                                {"object_name": "statusLabel"})
            waited = await call(session, "qt_wait_for", {
                "condition": "property_equals",
                "ref": status["widgets"][0]["ref"],
                "property_name": "text",
                "value": "Applied",
                "timeout_ms": 2000,
            })
            assert waited["ok"], f"button had no effect: {waited}"
            print("PASS")


asyncio.run(main())
```

关于返回格式，有几个值得知道的事实：

- 返回数据的工具把 **JSON 字符串装在 text content 块里**返回——
  `json.loads(result.content[0].text)`；截图则走 image content（base64）。
- 失败以 `result.is_error=True` 送达，原因在文本里（守卫拒绝、ref 失效、
  自定义命令不可用等）——务必检查。

## 推荐模式

以下模式全部取自 `client/verify.py`，需要完整范例（约 100 项断言）直接读它。

- **ref 现用现查，绝不缓存**。ref 随控件销毁而失效（且绝不静默重绑）。
  套件把 `qt_find_widget` 包成 `find_ref` 辅助函数，对话框/页面重建后一律
  重新解析。
- **断言效果，不要 sleep**。操作类工具是投递事件：每个动作都配
  `qt_wait_for` / `qt_get_text` / 属性读回，而不是 `asyncio.sleep`。完整的
  行为边界清单见 [pitfalls.zh-CN.md](pitfalls.zh-CN.md)。
- **成串的依赖步骤用 `qt_batch`**。一次往返执行多步，失败即停，省去大量
  往返开销。
- **宿主有自定义命令时，驱动语义而不是像素**。应用用
  `QtMcp::registerCommand` 注册的命令优先于长长的 UI 点击路径：先用
  `qt_app_commands` 查实时可用性与原因。脚本更短，也更不易碎。
- **长任务轮询 `qt_host_messages`**。行为良好的命令启动异步任务后立即返
  回、经宿主消息上报进度；脚本轮询直到终态消息出现（读后即清语义）。
- **截图作为测试产物**。`qt_screenshot` 的结果可落盘（本仓库的
  `client/shots/`），用于失败 run 的可视化排查。
- **同一时刻只跑一个脚本**。探针是单会话的：第二个 `initialize` 会让第一
  个会话失效。套件串行执行；跑套件时不要让交互式 agent 连着同一端口。

## 典型 CI 形态

```bash
# 1. 构建带探针的应用（探针默认 opt-in，release 里零开销）
# 2. 无头启动
QT_MCP_PROBE=1 QT_QPA_PLATFORM=offscreen ./your_app &
# 3. 跑套件，退出码即门禁
uv run python tests/automation.py
# 4. 失败时收集 client/shots/ 产物
```

本仓库的 demo 就是这个用法：
`QT_MCP_PROBE=1 QT_QPA_PLATFORM=offscreen ./build/examples/demo_app/Debug/demo_app.exe`，
然后 `uv run python verify.py`（97 项断言，结尾会自行驱动 demo 退出）和
`uv run python verify_commands.py`（自定义命令专项，不会退出 demo）。

## 注意事项

- 脚本进程与应用进程是两个独立进程，经 localhost HTTP 通信；只要重新
  `initialize` 会话（并重新解析全部 ref），脚本可以跨越应用重启。
- 套件之间共享一个存活的应用实例，只有在套件与状态无关时才安全；
  `verify.py` 的做法是结尾驱动 demo 走"退出→保存确认→Discard"流程，因此
  每次启动都是干净实例。套件间的状态泄漏（一个被勾上的选项、一个被切换
  的标签页）是后续运行不稳定的最常见原因——建议每个套件重启应用，或在
  套件开头显式重置状态。

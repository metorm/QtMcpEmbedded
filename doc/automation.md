# Scripted Automation & Testing

**English** | [中文](automation.zh-CN.md)

Besides interactive AI agents, the probe can be driven by plain Python scripts —
which turns it into an automation/regression-testing facility for your Qt
application: same protocol, same tools, but deterministic, repeatable, and
CI-friendly. The repository's own acceptance suites (`client/verify.py`,
`client/verify_commands.py`) are exactly this: Python scripts that only talk
MCP and never import anything from the application under test.

## Prerequisites

- The application is running with `QT_MCP_PROBE=1` (add
  `QT_QPA_PLATFORM=offscreen` for headless CI — see
  [client.md](client.md)).
- A Python environment with the [official MCP SDK](https://pypi.org/project/mcp/):
  `pip install mcp` (the `client/` directory of this repo is a ready-made uv
  environment: `uv sync`).

## Minimal Script

```python
import asyncio
import json

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

SERVER_URL = "http://127.0.0.1:9142/mcp"   # QT_MCP_HOST:QT_MCP_PORT + /mcp


async def call(session, name, args=None):
    """Call a tool; raise on protocol- or tool-level failure."""
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

Facts worth knowing about the wire format:

- Data-returning tools answer with a **JSON string inside a text content
  block** — `json.loads(result.content[0].text)`. Screenshots come back as
  image content (base64) instead.
- Failures arrive as `result.is_error=True` with the reason in the text (guard
  rejections, unknown refs, unavailable custom commands) — always check it.

## Recommended Patterns

All of these are taken from `client/verify.py`; read it for a full-size example
(~100 assertions).

- **Resolve refs fresh, never cache them.** Refs die with their widget (and are
  never silently rebound). The suite wraps `qt_find_widget` in a `find_ref`
  helper and re-resolves whenever a dialog or page is recreated.
- **Assert effects, don't sleep.** Actions are posted events: pair every action
  with `qt_wait_for` / `qt_get_text` / property read-back instead of
  `asyncio.sleep`. See [pitfalls.md](pitfalls.md) for the full list of
  behavioral boundaries.
- **Use `qt_batch` for chatter-free sequences.** Several dependent steps cost
  one round trip and stop at the first failure.
- **Drive semantics, not pixels, when the host offers custom commands.** If the
  application registers its own commands (`QtMcp::registerCommand`), prefer
  them over long UI click paths: check `qt_app_commands` for live availability
  and reasons. This makes scripts shorter and far less brittle.
- **Poll `qt_host_messages` for long jobs.** A well-behaved command starts
  async work and reports progress via host messages; the script polls until a
  terminal message appears (read-and-clear semantics).
- **Screenshots as test artifacts.** `qt_screenshot` results can be written to
  files (`client/shots/` in this repo) for visual debugging of failed runs.
- **One script at a time.** The probe serves a single MCP session; a second
  `initialize` invalidates the first. Run suites sequentially, and don't leave
  an interactive agent connected while a suite runs.

## Typical CI Shape

```bash
# 1. build the app with the probe (zero cost in release: probe is opt-in)
# 2. start it headless
QT_MCP_PROBE=1 QT_QPA_PLATFORM=offscreen ./your_app &
# 3. run the suite; exit code is the gate
uv run python tests/automation.py
# 4. collect client/shots/ artifacts on failure
```

The demo in this repo works exactly like this:
`QT_MCP_PROBE=1 QT_QPA_PLATFORM=offscreen ./build/examples/demo_app/Debug/demo_app.exe`,
then `uv run python verify.py` (97 assertions, exits the app itself at the end)
and `uv run python verify_commands.py` (custom-command suite, leaves the app
running).

## Notes

- The script process and the application are separate processes talking HTTP on
  localhost; the script survives application restarts as long as it
  re-initializes the session (and re-resolves all refs).
- A suite may safely leave the application running between invocations only if
  it is state-independent; `verify.py` instead ends by driving the demo through
  its own quit-and-discard flow, which is why it always starts from a clean
  instance. State leakage between suites (an option left checked, a tab left
  switched) is the most common cause of flaky follow-up runs — prefer
  restarting the application per suite, or reset state explicitly at suite
  start.

# Client Setup & Verification

**English** | [中文](client.zh-CN.md)

## Connecting a Client (Claude Code etc.)

```json
{ "mcpServers": { "my-app": { "type": "http", "url": "http://127.0.0.1:9142/mcp" } } }
```

Session notes:

- When the host program exits, the MCP server shuts down with it — a dropped connection
  is a normal termination.
- **Single session**: one probe instance serves exactly one MCP session at a time. A new
  `initialize` immediately invalidates the old session (subsequent requests get a 404
  "Missing or invalid Mcp-Session-Id") — do not connect to the same port concurrently;
  during long-running tasks, check status through the host log (`qt_host_messages`)
  instead of opening another session. (Also listed in
  [pitfalls.md](pitfalls.md).)

## Verification

Beyond interactive agents, the same endpoint can be driven by plain Python
scripts for deterministic automation and regression testing — see
[automation.md](automation.md).

The `client/` directory is a uv environment; run `uv sync` once to set it up.

```bash
# Terminal 1: start the demo in GUI mode (a window appears on the desktop)
QT_MCP_PROBE=1 ./build/examples/demo_app/Debug/demo_app.exe

# Terminal 2: run the end-to-end verification (80+ assertions covering every tool and blocking scenario)
cd client && uv sync && uv run python verify.py
```

`verify.py` depends only on the MCP interface, not on the implementation language, so
it can be reused directly to verify any probe implementing the same tool dialect. Note:
at the end of the script the demo goes through an "exit → save confirmation → Discard"
flow and quits by itself — that is expected behavior.

Headless (offscreen) verification is also supported and usable in CI:

```bash
QT_MCP_PROBE=1 QT_QPA_PLATFORM=offscreen ./build/examples/demo_app/Debug/demo_app.exe
```

> The offscreen platform has two Qt-level pitfalls, worked around by `HeadlessCompat`
> inside the library (enabled only when offscreen/minimal is detected; desktop platforms
> are unaffected). See [pitfalls.md](pitfalls.md) for details.

Dedicated verification for custom commands (`registerCommand` / `qt_app_commands` /
the availability-rejection path):

```bash
uv run python verify_commands.py   # likewise requires demo_app started with QT_MCP_PROBE=1; does not exit the demo
```

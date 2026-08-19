"""End-to-end verification of host-registered custom commands (registerCommand).

Usage:
    # demo_app must already be running with QT_MCP_PROBE=1
    uv run python verify_commands.py

Covers: custom commands visible in tools/list -> pending-queue command
(demo_echo, registered before install()) works -> availability rejection with
reason -> qt_app_commands snapshot (available=false + reason) -> enabling the
precondition -> successful invocation -> qt_app_commands flips to
available=true. Exits non-zero on any failed assertion. Does not exit the app.
"""

import asyncio
import json
import sys

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

SERVER_URL = "http://127.0.0.1:9142/mcp"

failures: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""), flush=True)
    if not ok:
        failures.append(name)


def text_of(result) -> str:
    return result.content[0].text if result.content else ""


def parse_json(result) -> dict:
    return json.loads(text_of(result))


async def call(session: ClientSession, name: str, args: dict | None = None):
    result = await session.call_tool(name, args or {})
    if result.is_error:
        raise RuntimeError(f"{name} failed: {text_of(result)}")
    return result


async def app_commands(session: ClientSession) -> dict:
    """qt_app_commands snapshot keyed by command name."""
    snap = parse_json(await call(session, "qt_app_commands"))
    return {c["name"]: c for c in snap.get("commands", [])}


async def main() -> int:
    async with streamable_http_client(SERVER_URL) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            # --- custom commands appear in tools/list alongside qt_* tools ---
            tools = await session.list_tools()
            names = {t.name for t in tools.tools}
            check("tools/list contains custom commands",
                  {"demo_status", "demo_apply_and_check", "demo_echo",
                   "qt_app_commands"} <= names,
                  f"{len(names)} tools")

            # --- pending-queue path: demo_echo was registered before install() ---
            echo = parse_json(await call(session, "demo_echo", {"text": "ping"}))
            check("demo_echo (pending-queue path) echoes argument",
                  echo.get("echo") == "ping", json.dumps(echo)[:120])

            # --- initial snapshot: demo_apply_and_check unavailable w/ reason ---
            cmds = await app_commands(session)
            check("qt_app_commands lists all custom commands",
                  {"demo_status", "demo_apply_and_check", "demo_echo"} <= set(cmds),
                  json.dumps(cmds, ensure_ascii=False)[:200])
            check("snapshot: demo_status available",
                  cmds.get("demo_status", {}).get("available") is True)
            apply_cmd = cmds.get("demo_apply_and_check", {})
            check("snapshot: demo_apply_and_check unavailable with reason",
                  apply_cmd.get("available") is False
                  and "unlockCheck" in apply_cmd.get("reason", ""),
                  json.dumps(apply_cmd, ensure_ascii=False)[:150])

            # --- call-time enforcement: isError + reason before handler runs ---
            denied = await session.call_tool("demo_apply_and_check", {})
            denied_text = text_of(denied)
            check("call denied while precondition unmet",
                  denied.is_error
                  and "not available now" in denied_text
                  and "unlockCheck" in denied_text,
                  denied_text[:200])

            # --- always-available command works ---
            status = parse_json(await call(session, "demo_status"))
            check("demo_status returns current state",
                  status.get("status") == "Ready" and status.get("name") == "",
                  json.dumps(status)[:120])

            # --- meet the precondition: check unlockCheck ---
            found = parse_json(await call(session, "qt_find_widget",
                                          {"object_name": "unlockCheck"}))
            unlock_ref = found["widgets"][0]["ref"]
            await call(session, "qt_set_property",
                       {"ref": unlock_ref, "property_name": "checked", "value": True})

            cmds = await app_commands(session)
            check("snapshot flips to available=true after precondition met",
                  cmds.get("demo_apply_and_check", {}).get("available") is True,
                  json.dumps(cmds.get("demo_apply_and_check"),
                             ensure_ascii=False)[:150])

            # --- invocation succeeds: applyButton triggered, status read back ---
            applied = parse_json(await call(session, "demo_apply_and_check"))
            check("demo_apply_and_check succeeds",
                  applied.get("status") == "Applied",
                  json.dumps(applied)[:120])

    print()
    if failures:
        print(f"{len(failures)} FAILED: {', '.join(failures)}")
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))

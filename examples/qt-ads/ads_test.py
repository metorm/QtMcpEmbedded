"""Deep probe test for Qt-Advanced-Docking-System apps with embedded QtMcp.

Usage:
    uv run python ads_test.py <ExeName> [--port 9142] [--exe-dir PATH] [--no-launch]

Launches build/x64/bin/<ExeName>.exe with QT_MCP_PROBE=1 (unless --no-launch),
runs a discovery-driven suite (snapshot / screenshots / dock introspection /
editable-text round trip / float-dock / batch / wait_for / messages), saving
screenshots to client/shots/. Exits non-zero on any failed assertion.
"""

import argparse
import asyncio
import base64
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

ADS_BIN = Path(os.environ.get(
    "ADS_BIN_DIR",
    Path(__file__).parent.parent.parent / "tmp" / "Qt-Advanced-Docking-System-master"
    / "build" / "x64" / "bin"))
SHOTS_DIR = Path(__file__).parent / "shots"

failures: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""), flush=True)
    if not ok:
        failures.append(name)


def info(msg: str) -> None:
    print(f"[info] {msg}", flush=True)


def text_of(result) -> str:
    return result.content[0].text if result.content else ""


def parse_json(result):
    return json.loads(text_of(result))


async def call(session: ClientSession, name: str, args: dict | None = None):
    result = await session.call_tool(name, args or {})
    if result.is_error:
        raise RuntimeError(f"{name} failed: {text_of(result)}")
    return result


async def call_soft(session: ClientSession, name: str, args: dict | None = None):
    """Like call() but returns (ok, payload-or-error-text) without raising."""
    result = await session.call_tool(name, args or {})
    if result.is_error:
        return False, text_of(result)
    try:
        return True, parse_json(result)
    except Exception:
        return True, text_of(result)


async def save_shot(result, filename: str) -> int:
    img = next(c for c in result.content if c.type == "image")
    data = base64.b64decode(img.data)
    SHOTS_DIR.mkdir(exist_ok=True)
    (SHOTS_DIR / filename).write_bytes(data)
    return len(data)


def port_open(port: int) -> bool:
    try:
        socket.create_connection(("127.0.0.1", port), timeout=2).close()
        return True
    except OSError:
        return False


async def find_all(session, **kw):
    kw.setdefault("visible_only", False)
    kw.setdefault("max_results", 100)
    ok, payload = await call_soft(session, "qt_find_widget", kw)
    if not ok:
        return []
    return payload.get("widgets", [])


async def run_suite(session: ClientSession, app: str) -> None:
    init = await session.initialize()
    check("initialize", init.server_info.name == "qt-mcp-embedded",
          f"protocol={init.protocol_version}")

    tools = await session.list_tools()
    check("tools/list >= 15", len(tools.tools) >= 15, f"{len(tools.tools)} tools")

    # --- windows & snapshot -------------------------------------------------
    windows = parse_json(await call(session, "qt_list_windows"))
    win_list = windows.get("windows", windows if isinstance(windows, list) else [])
    check("list_windows", len(win_list) >= 1, f"{len(win_list)} top-level windows")
    has_ref = all("ref" in w for w in win_list)
    check("list_windows entries carry ref+title", has_ref and all("title" in w for w in win_list),
          json.dumps(win_list[0]) if win_list else "")
    for w in win_list:
        info(f"  window: {w!r}")

    snap = parse_json(await call(session, "qt_snapshot"))
    snap_text = json.dumps(snap, ensure_ascii=False)
    check("snapshot non-empty", len(snap_text) > 500, f"{len(snap_text)} bytes")
    SHOTS_DIR.mkdir(exist_ok=True)
    (SHOTS_DIR / f"ads_{app}_snapshot.json").write_text(snap_text, encoding="utf-8")

    shot = await call(session, "qt_screenshot", {"full_window": True})
    size = await save_shot(shot, f"ads_{app}_01_main.png")
    check("screenshot main window", size > 1000, f"{size} bytes")

    # --- dock structure -----------------------------------------------------
    docks = await find_all(session, class_name="CDockWidget")
    check("find CDockWidget", len(docks) >= 1, f"{len(docks)} dock widgets")
    for d in docks[:8]:
        info(f"  dock: {d.get('text')!r} obj={d.get('objectName')} ref={d.get('ref')} "
             f"visible={d.get('visible')}")

    areas = await find_all(session, class_name="CDockAreaWidget")
    info(f"  dock areas: {len(areas)}")

    if docks:
        det = parse_json(await call(session, "qt_widget_details", {"ref": docks[0]["ref"]}))
        check("dock widget_details", bool(det.get("properties")) or bool(det.get("class")),
              f"keys={list(det)[:8]}")

    # --- editable controls: text extraction round trip ----------------------
    editables = []
    for cls in ("QLineEdit", "QTextEdit", "QPlainTextEdit"):
        editables += await find_all(session, class_name=cls)
    info(f"  editable controls: {len(editables)}")
    # prefer visible editables; hidden ones (inactive tabs) are legitimately
    # rejected by the operation guards
    editables.sort(key=lambda e: not e.get("visible", False))
    for e in editables:
        ok, payload = await call_soft(session, "qt_get_text", {"ref": e["ref"]})
        text = (payload.get("text") if ok and isinstance(payload, dict) else None)
        check(f"get_text {e.get('class') or cls}", ok and text is not None,
              repr((text or "")[:60]) if ok else str(payload)[:120])

    visible_editables = [e for e in editables if e.get("visible")]
    if visible_editables:
        target = visible_editables[0]
        marker = "MCP 注入测试 123"
        ok, payload = await call_soft(session, "qt_type_text",
                                      {"ref": target["ref"], "text": marker,
                                       "clear_first": True})
        check("type_text into editable", ok, str(payload)[:120])
        await asyncio.sleep(0.3)
        ok, payload = await call_soft(session, "qt_get_text", {"ref": target["ref"]})
        got = payload.get("text", "") if ok else ""
        check("text round trip verified", marker in got, repr(got[:80]))
        shot = await call(session, "qt_screenshot", {"ref": target["ref"]})
        await save_shot(shot, f"ads_{app}_02_editable.png")
    elif editables:
        info("  no visible editable — skipping type test (get_text already covered)")

    # --- dock tab switching via click ---------------------------------------
    visible_docks = [d for d in docks if d.get("visible")]
    if len(visible_docks) >= 2:
        ok, payload = await call_soft(session, "qt_click", {"ref": visible_docks[1]["ref"]})
        check("click dock widget", ok, str(payload)[:120])
        await asyncio.sleep(0.3)

    # --- float a dock (dynamic top-level window) ----------------------------
    # pick a visible dock that is not already floating (its parent chain must
    # not contain a CFloatingDockContainer)
    float_target = None
    for d in visible_docks:
        det = parse_json(await call(session, "qt_widget_details", {"ref": d["ref"]}))
        chain = json.dumps(det.get("parent_chain", []))
        if "Floating" not in chain:
            float_target = d
            break
    if float_target:
        before = len(parse_json(await call(session, "qt_list_windows"))
                     .get("windows", []))
        ok, payload = await call_soft(session, "qt_invoke_slot",
                                      {"ref": float_target["ref"],
                                       "method_name": "setFloating()"})
        if not ok:
            ok, payload = await call_soft(session, "qt_invoke_slot",
                                          {"ref": float_target["ref"],
                                           "method_name": "setFloating"})
        check("invoke setFloating", ok, str(payload)[:150])
        await asyncio.sleep(0.8)
        after = parse_json(await call(session, "qt_list_windows")).get("windows", [])
        check("floating created new window", len(after) > before,
              f"{before} -> {len(after)}")
        if len(after) > before:
            # screenshot the floating window itself (identified via ref/title),
            # not just the first visible top-level
            floaters = [w for w in after
                        if "Floating" in (w.get("class") or "")
                        or "Floating" in (w.get("title") or "")]
            target = floaters[0] if floaters else after[-1]
            shot = await call(session, "qt_screenshot", {"ref": target["ref"]})
            await save_shot(shot, f"ads_{app}_03_floating.png")
        # dock it back
        ok2, payload2 = await call_soft(session, "qt_invoke_slot",
                                        {"ref": float_target["ref"],
                                         "method_name": "toggleView(bool)",
                                         "args": [False]})
        if ok2:
            await asyncio.sleep(0.3)
            await call_soft(session, "qt_invoke_slot",
                            {"ref": float_target["ref"], "method_name": "toggleView(bool)",
                             "args": [True]})
        await asyncio.sleep(0.5)

    # --- batch / wait_for / messages ----------------------------------------
    ok, payload = await call_soft(session, "qt_batch", {"steps": [
        {"method": "qt_list_windows", "params": {}},
        {"method": "qt_find_widget", "params": {"class_name": "CDockWidget",
                                                "visible_only": False}},
    ]})
    check("qt_batch", ok, str(payload)[:150])

    main_ref = win_list[0].get("ref") if win_list else None
    if main_ref:
        ok, payload = await call_soft(session, "qt_wait_for", {
            "condition": "property_equals", "ref": main_ref,
            "property_name": "visible", "value": True, "timeout_ms": 2000})
        check("qt_wait_for", ok, str(payload)[:120])

    ok, payload = await call_soft(session, "qt_debug_message", {})
    check("qt_debug_message", ok, f"count={payload.get('count') if isinstance(payload, dict) else '?'}")

    shot = await call(session, "qt_screenshot", {"full_window": True})
    await save_shot(shot, f"ads_{app}_04_final.png")


async def amain() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", help="exe name without extension, e.g. SimpleExample")
    ap.add_argument("--port", type=int, default=9142)
    ap.add_argument("--exe-dir", default=str(ADS_BIN))
    ap.add_argument("--no-launch", action="store_true")
    args = ap.parse_args()

    proc = None
    if not args.no_launch:
        exe = Path(args.exe_dir) / f"{args.exe}.exe"
        env = dict(os.environ, QT_MCP_PROBE="1", QT_MCP_PORT=str(args.port))
        proc = subprocess.Popen([str(exe)], env=env)
        for _ in range(150):
            if port_open(args.port):
                break
            if proc.poll() is not None:
                print(f"[FAIL] {exe.name} exited early, code={proc.returncode}")
                return 1
            time.sleep(0.1)
        else:
            print(f"[FAIL] port {args.port} never opened")
            proc.kill()
            return 1
        info(f"launched {exe.name} pid={proc.pid} port={args.port}")

    url = f"http://127.0.0.1:{args.port}/mcp"
    try:
        async with streamable_http_client(url) as (read, write):
            async with ClientSession(read, write) as session:
                await run_suite(session, args.exe)
    finally:
        if proc is not None:
            proc.kill()
            proc.wait(timeout=5)

    print(f"\n=== {args.exe}: {len(failures)} failure(s) ===")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(amain()))

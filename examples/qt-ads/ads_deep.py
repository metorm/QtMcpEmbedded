"""Deep scenario tests for ADS apps with embedded QtMcp.

Usage:
    uv run python ads_deep.py <scenario> [--port N]

Scenarios:
    demo          AdvancedDockingSystemDemo: menu/toolbar actions, tab switch,
                  save/restore state, autohide sidebar, per-window screenshots
    deleteonclose DeleteOnCloseTest: dynamic dock creation via menu action,
                  editable QTextEdit text round trip, content recreation
    autohide      AutoHideExample: pin-to-autohide, sidebar tab overlay

Launches the exe itself. Screenshots go to client/shots/.
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


def parse_json(result):
    return json.loads(result.content[0].text if result.content else "null")


class Probe:
    def __init__(self, session: ClientSession, tag: str):
        self.s = session
        self.tag = tag
        self.shot_n = 0

    async def call(self, name: str, args: dict | None = None):
        result = await self.s.call_tool(name, args or {})
        if result.is_error:
            raise RuntimeError(f"{name} failed: {result.content[0].text}")
        return result

    async def soft(self, name: str, args: dict | None = None):
        result = await self.s.call_tool(name, args or {})
        if result.is_error:
            return False, result.content[0].text
        try:
            return True, parse_json(result)
        except Exception:
            return True, result.content[0].text

    async def j(self, name: str, args: dict | None = None):
        return parse_json(await self.call(name, args))

    async def shot(self, label: str, ref: str | None = None, full_window: bool = False):
        args = {"format": "png"}
        if ref:
            args["ref"] = ref
        if full_window:
            args["full_window"] = True
        result = await self.call("qt_screenshot", args)
        img = next(c for c in result.content if c.type == "image")
        data = base64.b64decode(img.data)
        SHOTS_DIR.mkdir(exist_ok=True)
        self.shot_n += 1
        path = SHOTS_DIR / f"deep_{self.tag}_{self.shot_n:02d}_{label}.png"
        path.write_bytes(data)
        info(f"  shot -> {path.name} ({len(data)} bytes)")
        return path

    async def find(self, **kw):
        kw.setdefault("visible_only", False)
        kw.setdefault("max_results", 100)
        ok, payload = await self.soft("qt_find_widget", kw)
        if not ok:
            return []
        return payload.get("widgets", [])

    async def windows(self):
        return (await self.j("qt_list_windows")).get("windows", [])


# --------------------------------------------------------------------- demo
async def scenario_demo(p: Probe) -> None:
    wins = await p.windows()
    for i, w in enumerate(wins):
        await p.shot(f"window{i}_{(w.get('title') or 'untitled').replace(' ', '_')}",
                     ref=w["ref"])
    # screenshots must target the main window explicitly — full_window would
    # grab the first visible top-level, which is the floating calendar here
    main_ref = next((w["ref"] for w in wins if "MainWindow" in (w.get("class") or "")),
                    wins[0]["ref"])

    # --- toolbar actions: Save State / Restore State ------------------------
    toolbars = await p.find(class_name="QToolBar")
    check("demo has toolbar", len(toolbars) >= 1, f"{len(toolbars)} toolbars")
    docks_before = await p.find(class_name="CDockWidget")
    visible_before = [d for d in docks_before if d.get("visible")]
    ok, payload = await p.soft("qt_trigger_action",
                               {"ref": toolbars[0]["ref"], "action_text": "Save State"})
    check("trigger Save State", ok, str(payload)[:150])

    # close a visible dock via its tab close button, then restore state
    target = next((d for d in visible_before if d.get("objectName") == "Label 2"),
                  visible_before[0] if visible_before else None)
    check("picked a dock to close", target is not None,
          f"target={target and target.get('objectName')}")
    if target:
        ok, payload = await p.soft("qt_invoke_slot",
                                   {"ref": target["ref"], "method_name": "toggleView(bool)",
                                    "args": [False]})
        check("close dock", ok, str(payload)[:120])
        await asyncio.sleep(0.5)
        ok, payload = await p.soft("qt_trigger_action",
                                   {"ref": toolbars[0]["ref"], "action_text": "Restore State"})
        check("trigger Restore State", ok, str(payload)[:150])
        await asyncio.sleep(1.0)
        det = await p.j("qt_widget_details", {"ref": target["ref"]})
        check("dock visible again after restore", det.get("visible") is True,
              f"visible={det.get('visible')}")
        await p.shot("restored", ref=main_ref)

    # --- menu bar: View menu toggle -----------------------------------------
    menubars = await p.find(class_name="QMenuBar")
    check("demo has menubar", len(menubars) >= 1)
    if menubars:
        ok, payload = await p.soft("qt_trigger_action",
                                   {"ref": menubars[0]["ref"], "action_text": "Label 4"})
        check("trigger View>Label 4 (hidden dock toggle)", ok, str(payload)[:200])
        await asyncio.sleep(0.5)
        d4 = await p.find(object_name="Label 4")
        if d4:
            det = await p.j("qt_widget_details", {"ref": d4[0]["ref"]})
            info(f"  Label 4 visible={det.get('visible')} after menu toggle")
            await p.shot("menu_toggled", ref=main_ref)

    # --- tab switch: click an inactive CDockWidgetTab -----------------------
    tabs = await p.find(class_name="CDockWidgetTab")
    visible_tabs = [t for t in tabs if t.get("visible")]
    info(f"  {len(visible_tabs)} visible dock tabs")
    clicked = False
    for t in visible_tabs:
        # CDockWidgetTab click switches the active tab of its area
        ok, payload = await p.soft("qt_click", {"ref": t["ref"]})
        if ok:
            clicked = True
            info(f"  clicked tab ref={t['ref']} text={t.get('text')!r}")
            break
    check("click dock tab", clicked)
    await asyncio.sleep(0.5)
    await p.shot("after_tab_click", ref=main_ref)

    # --- autohide: pin a dock to the side bar, then open its overlay --------
    pin_buttons = [b for b in await p.find(object_name="dockAreaAutoHideButton")
                   if b.get("visible")]
    if not pin_buttons:
        pin_buttons = [b for b in await p.find(class_name="CTitleBarButton")
                       if b.get("visible") and "pin" in (b.get("tooltip") or "").lower()]
    if pin_buttons:
        ok, payload = await p.soft("qt_click", {"ref": pin_buttons[0]["ref"]})
        check("click auto-hide pin button", ok, str(payload)[:120])
        await asyncio.sleep(0.8)
        side_tabs = await p.find(class_name="CAutoHideTab")
        check("auto-hide side tab created", len(side_tabs) >= 1,
              f"{len(side_tabs)} CAutoHideTab")
        await p.shot("autohide_sidebar", ref=main_ref)
        if side_tabs:
            ok, payload = await p.soft("qt_click", {"ref": side_tabs[0]["ref"]})
            check("click side tab to open overlay", ok, str(payload)[:120])
            await asyncio.sleep(1.0)
            await p.shot("autohide_overlay", ref=main_ref)
            # dismiss overlay with Esc
            await p.soft("qt_key_press", {"key": "Escape"})
    else:
        info("  no auto-hide pin button visible — skipping autohide flow")

    msgs = await p.j("qt_debug_message", {})
    info(f"  qt_debug_message count={msgs.get('count')}")


# ------------------------------------------------------------- deleteonclose
async def scenario_deleteonclose(p: Probe) -> None:
    menubars = await p.find(class_name="QMenuBar")
    check("menubar present", len(menubars) >= 1)
    mb = menubars[0]["ref"]

    # create two docks dynamically
    for i in range(2):
        ok, payload = await p.soft("qt_trigger_action", {"ref": mb, "action_text": "New"})
        check(f"trigger New #{i}", ok, str(payload)[:150])
        await asyncio.sleep(0.4)

    edits = await p.find(class_name="QTextEdit")
    check("QTextEdits created dynamically", len(edits) >= 2, f"{len(edits)}")
    visible_edits = [e for e in edits if e.get("visible")]
    if visible_edits:
        target_edit = visible_edits[0]
        t = await p.j("qt_get_text", {"ref": target_edit["ref"]})
        check("get_text of dynamic editor", "lorem ipsum" in (t.get("text") or ""),
              repr((t.get("text") or "")[:50]))
        marker = "动态创建的编辑器 42"
        ok, payload = await p.soft("qt_type_text",
                                   {"ref": target_edit["ref"], "text": marker,
                                    "clear_first": True})
        check("type into dynamic editor", ok, str(payload)[:120])
        t = await p.j("qt_get_text", {"ref": target_edit["ref"]})
        check("editor text round trip", marker in (t.get("text") or ""),
              repr((t.get("text") or "")[:60]))
        await p.shot("dynamic_editor", ref=target_edit["ref"])

    # toggle DeleteContentOnClose dock twice: content deleted & recreated
    ok, _ = await p.soft("qt_trigger_action",
                         {"ref": mb, "action_text": "Toggle [DeleteContentOnClose]"})
    check("toggle close", ok)
    await asyncio.sleep(0.5)
    ok, _ = await p.soft("qt_trigger_action",
                         {"ref": mb, "action_text": "Toggle [DeleteContentOnClose]"})
    check("toggle reopen", ok)
    await asyncio.sleep(0.5)
    edits2 = await p.find(class_name="QTextEdit")
    recreated = None
    for e in edits2:
        t = await p.j("qt_get_text", {"ref": e["ref"]})
        if "recreated" in (t.get("text") or ""):
            recreated = t.get("text")
    check("content recreated with new text", recreated is not None,
          repr((recreated or "")[:70]))
    await p.shot("final", full_window=True)


# ------------------------------------------------------------------ autohide
async def scenario_autohide(p: Probe) -> None:
    wins = await p.windows()
    await p.shot("initial", ref=wins[0]["ref"])

    # pin the visible Properties dock to autohide via its pin button
    pin_buttons = [b for b in await p.find(object_name="dockAreaAutoHideButton")
                   if b.get("visible")]
    check("pin button found", len(pin_buttons) >= 1, f"{len(pin_buttons)}")
    if not pin_buttons:
        return
    ok, payload = await p.soft("qt_click", {"ref": pin_buttons[0]["ref"]})
    check("click pin button", ok, str(payload)[:120])
    await asyncio.sleep(1.0)
    side_tabs = await p.find(class_name="CAutoHideTab")
    check("side tab created", len(side_tabs) >= 1, f"{len(side_tabs)}")
    await p.shot("sidebar", full_window=True)
    if side_tabs:
        ok, payload = await p.soft("qt_click", {"ref": side_tabs[0]["ref"]})
        check("open overlay", ok, str(payload)[:120])
        await asyncio.sleep(1.2)
        # overlay: dock becomes visible again in a sliding container
        det_list = await p.find(class_name="CAutoHideDockContainer")
        info(f"  autohide containers: {len(det_list)}")
        await p.shot("overlay", full_window=True)
        await p.soft("qt_key_press", {"key": "Escape"})
        await asyncio.sleep(0.8)
        await p.shot("after_esc", full_window=True)


SCENARIOS = {
    "demo": ("AdvancedDockingSystemDemo", scenario_demo),
    "deleteonclose": ("DeleteOnCloseTest", scenario_deleteonclose),
    "autohide": ("AutoHideExample", scenario_autohide),
}


def port_open(port: int) -> bool:
    try:
        socket.create_connection(("127.0.0.1", port), timeout=2).close()
        return True
    except OSError:
        return False


async def amain() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=SCENARIOS.keys())
    ap.add_argument("--port", type=int, default=9142)
    ap.add_argument("--exe-dir", default=str(ADS_BIN))
    args = ap.parse_args()

    exe_name, fn = SCENARIOS[args.scenario]
    exe = Path(args.exe_dir) / f"{exe_name}.exe"
    env = dict(os.environ, QT_MCP_PROBE="1", QT_MCP_PORT=str(args.port))
    proc = subprocess.Popen([str(exe)], env=env)
    try:
        for _ in range(150):
            if port_open(args.port):
                break
            if proc.poll() is not None:
                print(f"[FAIL] {exe.name} exited early, code={proc.returncode}")
                return 1
            time.sleep(0.1)
        else:
            print(f"[FAIL] port {args.port} never opened")
            return 1
        info(f"launched {exe.name} pid={proc.pid} port={args.port}")
        await asyncio.sleep(1.0)  # let the UI settle

        url = f"http://127.0.0.1:{args.port}/mcp"
        async with streamable_http_client(url) as (read, write):
            async with ClientSession(read, write) as session:
                await session.initialize()
                await fn(Probe(session, args.scenario))
    finally:
        proc.kill()
        proc.wait(timeout=5)

    print(f"\n=== {args.scenario}: {len(failures)} failure(s) ===")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(amain()))

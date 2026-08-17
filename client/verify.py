"""End-to-end verification of QtMcpEmbedded against the running demo_app.

Usage:
    # demo_app must already be running with QT_MCP_PROBE=1 (GUI mode)
    uv run python verify.py

Covers: initialize -> tools/list -> snapshot -> screenshots -> text input ->
click -> property set -> widget linkages (slider/spin/progress, dial/LCD,
combo/label, checkbox/group, radio/label, list/label, table sum) -> tab
switch -> modal dialog -> batch -> qt_debug_message.
Screenshots saved to client/shots/. Exits non-zero on any failed assertion.
"""

import asyncio
import base64
import json
import re
import socket
import sys
import time
from pathlib import Path

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

SERVER_URL = "http://127.0.0.1:9142/mcp"
SHOTS_DIR = Path(__file__).parent / "shots"

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


async def find_ref(session: ClientSession, object_name: str) -> str:
    result = parse_json(await call(session, "qt_find_widget", {"object_name": object_name}))
    widgets = result.get("widgets", [])
    if not widgets:
        raise RuntimeError(f"widget not found: {object_name}")
    return widgets[0]["ref"]


async def prop(session: ClientSession, ref: str, name: str):
    details = parse_json(await call(session, "qt_widget_details", {"ref": ref}))
    # objectName/visible/enabled/geometry are reported at the top level of
    # widget_details; everything else lives under "properties"
    value = details.get(name, details.get("properties", {}).get(name))
    if isinstance(value, dict):
        value = value.get("value")
    return value


async def wait_prop(session: ClientSession, ref: str, name: str, value, timeout_ms: int = 3000):
    return parse_json(await call(session, "qt_wait_for", {
        "condition": "property_equals", "ref": ref,
        "property_name": name, "value": value, "timeout_ms": timeout_ms}))


async def save_shot(result, filename: str) -> int:
    img = next(c for c in result.content if c.type == "image")
    data = base64.b64decode(img.data)
    SHOTS_DIR.mkdir(exist_ok=True)
    (SHOTS_DIR / filename).write_bytes(data)
    return len(data)


def exit_save_flow() -> None:
    """Unsaved-changes exit flow: Quit -> Cancel (app survives) -> Quit ->
    Discard (app exits). Runs last, over raw HTTP, because it kills the server.

    Note: Quit goes through closeEvent, so the save/discard/cancel prompt is a
    modal QMessageBox discovered via qt_active_popup.
    """
    import urllib.request

    base_headers = {"Content-Type": "application/json",
                    "Accept": "application/json, text/event-stream"}

    def post(payload: dict, session_id: str | None = None):
        headers = dict(base_headers)
        if session_id:
            headers["Mcp-Session-Id"] = session_id
            headers["MCP-Protocol-Version"] = "2025-06-18"
        req = urllib.request.Request(SERVER_URL, data=json.dumps(payload).encode(),
                                     headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=10) as resp:
            return dict(resp.headers), resp.read()

    resp_headers, _ = post({
        "jsonrpc": "2.0", "id": 1, "method": "initialize",
        "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                   "clientInfo": {"name": "verify-exit", "version": "0"}}})
    session_id = resp_headers.get("Mcp-Session-Id") or resp_headers.get("mcp-session-id")
    post({"jsonrpc": "2.0", "method": "notifications/initialized"}, session_id)

    counter = iter(range(2, 1000))

    def call_tool(name: str, args: dict) -> dict:
        _, body = post({"jsonrpc": "2.0", "id": next(counter), "method": "tools/call",
                        "params": {"name": name, "arguments": args}}, session_id)
        result = json.loads(body)["result"]
        text = result["content"][0]["text"] if result.get("content") else ""
        return json.loads(text)

    def wait_popup():
        for _ in range(30):
            popup = call_tool("qt_active_popup", {})
            if popup.get("found"):
                return popup
            time.sleep(0.1)
        return {}

    def port_open() -> bool:
        try:
            socket.create_connection(("127.0.0.1", 9142), timeout=2).close()
            return True
        except OSError:
            return False

    # ensure unsaved changes
    edit_ref = call_tool("qt_find_widget", {"object_name": "nameEdit"})["widgets"][0]["ref"]
    call_tool("qt_type_text", {"ref": edit_ref, "text": "dirty", "clear_first": True})

    menu_ref = call_tool("qt_find_widget",
                         {"object_name": "fileMenu", "visible_only": False}
                         )["widgets"][0]["ref"]

    # round 1: Quit -> Cancel -> app survives
    call_tool("qt_trigger_action", {"ref": menu_ref, "action_text": "Quit"})
    popup = wait_popup()
    check("exit prompts save/discard/cancel",
          popup.get("found") is True and popup.get("class") == "QMessageBox"
          and "Unsaved changes" in (popup.get("title") or ""),
          json.dumps(popup)[:200])
    cancel = [b for b in popup.get("buttons", []) if b.get("text") == "Cancel"]
    check("save dialog lists Cancel", len(cancel) == 1,
          json.dumps(popup.get("buttons"))[:200])
    call_tool("qt_click", {"ref": cancel[0]["ref"]})
    time.sleep(0.5)
    gone = call_tool("qt_active_popup", {})
    check("Cancel dismisses prompt, app alive",
          gone.get("found") is False and port_open())

    # round 2: Quit -> Discard -> app exits, server port closes
    call_tool("qt_trigger_action", {"ref": menu_ref, "action_text": "Quit"})
    popup = wait_popup()
    discard = [b for b in popup.get("buttons", []) if b.get("text") == "Discard"]
    check("save dialog lists Discard", len(discard) == 1,
          json.dumps(popup.get("buttons"))[:200])
    # The click kills the app, so its own HTTP response may get lost in a
    # reset connection — the real assertion is the port closing afterwards.
    # (The Cancel round above already proved prompt+button clicks work.)
    try:
        call_tool("qt_click", {"ref": discard[0]["ref"]})
    except Exception:
        pass
    time.sleep(0.8)
    check("app exited after Discard (port closed)", not port_open())


async def main() -> int:
    async with streamable_http_client(SERVER_URL) as (read, write):
        async with ClientSession(read, write) as session:
            init = await session.initialize()
            check("initialize", init.server_info.name == "qt-mcp-embedded",
                  f"server={init.server_info.name} {init.server_info.version}, "
                  f"protocol={init.protocol_version}")
            instructions = init.instructions or ""
            check("host instructions in initialize",
                  "QtMcp Demo" in instructions and "lockedButton" in instructions,
                  f"instructions length={len(instructions)}")
            check("instructions note server shutdown on app exit",
                  "shuts down with it" in instructions)
            check("instructions carry behavioral limits for agents",
                  "Behavioral limits" in instructions
                  and "never silently rebound" in instructions
                  and "event posted" in instructions)

            tools = await session.list_tools()
            names = sorted(t.name for t in tools.tools)
            check("tools/list", len(names) >= 15, f"{len(names)} tools")

            snap = parse_json(await call(session, "qt_snapshot"))
            check("qt_snapshot", len(snap) > 0, json.dumps(snap)[:120])

            n = await save_shot(await call(session, "qt_screenshot"), "01_initial.png")
            check("qt_screenshot initial", n > 1000, f"{n} bytes")

            name_ref = await find_ref(session, "nameEdit")
            apply_ref = await find_ref(session, "applyButton")
            status_ref = await find_ref(session, "statusLabel")

            # --- tooltips surface by default in all introspection channels ---
            fw = parse_json(await call(session, "qt_find_widget", {"object_name": "applyButton"}))
            tip = fw["widgets"][0].get("tooltip")
            check("tooltip in find_widget",
                  tip == "Apply the current settings (sets status to Applied)",
                  f"tip={tip!r}")
            snap_text = text_of(await call(session, "qt_snapshot"))
            check("tooltip in snapshot (incl. Chinese)",
                  "[tip:" in snap_text and "输入名字" in snap_text,
                  "snapshot contains [tip:] markers and Chinese tooltip")
            tip_prop = await prop(session, name_ref, "toolTip")
            check("tooltip via widget_details property",
                  tip_prop == "输入名字，会实时镜像到 Mirror 标签",
                  f"toolTip={tip_prop!r}")

            # --- regression: refs stay valid across snapshots (never rebound) ---
            await call(session, "qt_snapshot")
            details = parse_json(await call(session, "qt_widget_details", {"ref": apply_ref}))
            check("refs stable across snapshots",
                  details.get("objectName") == "applyButton",
                  f"objectName={details.get('objectName')!r}")

            # --- regression: snapshot folds Qt-internal widgets, shows item data ---
            check("snapshot folds qt_* internals",
                  "qt_scrollarea_viewport" not in snap_text)
            check("snapshot shows combo current/items",
                  "[current: Alpha]" in snap_text and "Alpha | Beta | Gamma" in snap_text)

            # --- toolbar action triggers the same linkage as the Apply button ---
            toolbar_ref = await find_ref(session, "mainToolBar")
            trig = parse_json(await call(session, "qt_trigger_action",
                                         {"ref": toolbar_ref, "action_text": "Apply"}))
            check("qt_trigger_action toolbar", trig.get("ok") is True, json.dumps(trig)[:100])
            waited = await wait_prop(session, status_ref, "text", "Applied")
            check("toolbar Apply -> status Applied", waited.get("ok") is True,
                  json.dumps(waited)[:100])

            # --- type into line edit; mirror label follows via textChanged ---
            typed = parse_json(await call(session, "qt_type_text",
                                          {"ref": name_ref, "text": "Kimi",
                                           "clear_first": True}))
            check("qt_type_text echoes resulting text", typed.get("text") == "Kimi",
                  f"echo={typed.get('text')!r}")
            text = parse_json(await call(session, "qt_get_text", {"ref": name_ref}))
            check("qt_type_text + qt_get_text", text.get("text") == "Kimi",
                  f"text={text.get('text')!r}")
            mirror_ref = await find_ref(session, "mirrorLabel")
            mirror = parse_json(await call(session, "qt_get_text", {"ref": mirror_ref}))
            check("linkage: textChanged -> mirrorLabel", mirror.get("text") == "Kimi",
                  f"mirror={mirror.get('text')!r}")

            # --- click Apply (posted events; status already Applied) ---
            await call(session, "qt_click", {"ref": apply_ref})
            status = parse_json(await call(session, "qt_get_text", {"ref": status_ref}))
            check("qt_click apply -> status", status.get("text") == "Applied",
                  f"status={status.get('text')!r}")

            # --- plain text edit: content + line count ---
            notes_ref = await find_ref(session, "notesEdit")
            notes = parse_json(await call(session, "qt_get_text", {"ref": notes_ref}))
            check("plain text edit content",
                  notes.get("text") == "line1\nline2" and notes.get("line_count") == 2,
                  f"text={notes.get('text')!r} lines={notes.get('line_count')}")

            # --- checkbox property ---
            agree_ref = await find_ref(session, "agreeCheckBox")
            await call(session, "qt_set_property",
                       {"ref": agree_ref, "property_name": "checked", "value": True})
            checked = await prop(session, agree_ref, "checked")
            check("qt_set_property checked", checked is True, f"checked={checked!r}")

            n = await save_shot(await call(session, "qt_screenshot"), "02_after_input.png")
            check("qt_screenshot after input", n > 1000, f"{n} bytes")

            # --- combo box linkage: selection drives modeLabel ---
            combo_ref = await find_ref(session, "modeCombo")
            await call(session, "qt_set_property",
                       {"ref": combo_ref, "property_name": "currentIndex", "value": 1})
            mode_ref = await find_ref(session, "modeLabel")
            waited = await wait_prop(session, mode_ref, "text", "Mode: Beta")
            check("linkage: combo -> modeLabel", waited.get("ok") is True,
                  json.dumps(waited)[:100])

            # --- slider/spin/progress three-way linkage ---
            spin_ref = await find_ref(session, "volumeSpin")
            await call(session, "qt_set_property",
                       {"ref": spin_ref, "property_name": "value", "value": 42})
            slider_ref = await find_ref(session, "volumeSlider")
            progress_ref = await find_ref(session, "volumeProgress")
            slider_val = await prop(session, slider_ref, "value")
            progress_val = await prop(session, progress_ref, "value")
            check("linkage: spin -> slider", slider_val == 42, f"slider={slider_val!r}")
            check("linkage: spin -> slider -> progress", progress_val == 42,
                  f"progress={progress_val!r}")

            # --- dial drives LCD ---
            dial_ref = await find_ref(session, "levelDial")
            await call(session, "qt_set_property",
                       {"ref": dial_ref, "property_name": "value", "value": 7})
            lcd_ref = await find_ref(session, "levelLcd")
            lcd_val = await prop(session, lcd_ref, "intValue")
            check("linkage: dial -> LCD", lcd_val == 7, f"lcd={lcd_val!r}")

            # --- checkbox enables/disables group; radios drive label ---
            adv_ref = await find_ref(session, "advancedCheck")
            group_ref = await find_ref(session, "advancedGroup")
            await call(session, "qt_set_property",
                       {"ref": adv_ref, "property_name": "checked", "value": False})
            enabled = await prop(session, group_ref, "enabled")
            check("linkage: advancedCheck off -> group disabled", enabled is False,
                  f"enabled={enabled!r}")
            await call(session, "qt_set_property",
                       {"ref": adv_ref, "property_name": "checked", "value": True})
            enabled = await prop(session, group_ref, "enabled")
            check("linkage: advancedCheck on -> group enabled", enabled is True,
                  f"enabled={enabled!r}")

            radio2_ref = await find_ref(session, "optionRadio2")
            await call(session, "qt_click", {"ref": radio2_ref})
            await asyncio.sleep(0.4)  # posted click is delivered by the app event loop
            option_ref = await find_ref(session, "optionLabel")
            option = parse_json(await call(session, "qt_get_text", {"ref": option_ref}))
            check("linkage: radio click -> optionLabel", option.get("text") == "Option: 2",
                  f"option={option.get('text')!r}")

            # --- disabled-widget guard (multi-layer defense) ---
            locked_ref = await find_ref(session, "lockedButton")
            blocked = await session.call_tool("qt_click", {"ref": locked_ref})
            check("guard: click disabled -> explicit error",
                  blocked.is_error and "disabled" in text_of(blocked),
                  text_of(blocked)[:120])
            await call(session, "qt_click", {"ref": locked_ref, "force": True})
            locked_label_ref = await find_ref(session, "lockedLabel")
            label = parse_json(await call(session, "qt_get_text", {"ref": locked_label_ref}))
            check("forced click on disabled had no effect",
                  label.get("text") == "Not run", f"label={label.get('text')!r}")
            unlock_ref = await find_ref(session, "unlockCheck")
            await call(session, "qt_set_property",
                       {"ref": unlock_ref, "property_name": "checked", "value": True})
            enabled = await prop(session, locked_ref, "enabled")
            check("linkage: unlockCheck -> button enabled", enabled is True,
                  f"enabled={enabled!r}")
            await call(session, "qt_click", {"ref": locked_ref})
            waited = await wait_prop(session, locked_label_ref, "text", "Clicked")
            check("unlocked button click works", waited.get("ok") is True,
                  json.dumps(waited)[:100])
            click_res = parse_json(await call(session, "qt_click", {"ref": locked_ref}))
            check("click result carries enabled/visible snapshot",
                  click_res.get("enabled") is True and click_res.get("visible") is True,
                  json.dumps(click_res)[:120])

            # --- regression: center-click toggles a wide checkbox (style hit rect) ---
            await call(session, "qt_click", {"ref": agree_ref})
            checked = await prop(session, agree_ref, "checked")
            check("center click toggles checkbox", checked is False, f"checked={checked!r}")
            await call(session, "qt_click", {"ref": agree_ref})
            checked = await prop(session, agree_ref, "checked")
            check("center click toggles checkbox back", checked is True,
                  f"checked={checked!r}")

            # --- regression: invalid key names are rejected ---
            bad_key = await session.call_tool("qt_key_press",
                                              {"key": "NotAKey", "ref": name_ref})
            check("invalid key name -> explicit error",
                  bad_key.is_error and "Unknown key" in text_of(bad_key),
                  text_of(bad_key)[:150])

            # --- regression: click establishes keyboard focus ---
            await call(session, "qt_click", {"ref": name_ref})
            await call(session, "qt_key_press", {"key": "End"})
            await call(session, "qt_key_press", {"key": "1"})
            await asyncio.sleep(0.3)
            text = parse_json(await call(session, "qt_get_text", {"ref": name_ref}))
            check("click sets keyboard focus", text.get("text") == "Kimi1",
                  f"text={text.get('text')!r}")

            # --- regression: dynamic property is reported as dynamic, not failed ---
            dyn = parse_json(await call(session, "qt_set_property",
                                        {"ref": name_ref, "property_name": "myDynamicProp",
                                         "value": 5}))
            check("dynamic property reported as dynamic",
                  dyn.get("ok") is True and dyn.get("dynamic") is True,
                  json.dumps(dyn)[:120])

            # --- modal warning box discovered via qt_active_popup ---
            warn_ref = await find_ref(session, "warnButton")
            await call(session, "qt_click", {"ref": warn_ref})
            popup = {}
            for _ in range(20):  # posted click needs an event-loop turn
                popup = parse_json(await call(session, "qt_active_popup"))
                if popup.get("found"):
                    break
                await asyncio.sleep(0.1)
            check("active_popup finds warning box",
                  popup.get("found") is True and popup.get("class") == "QMessageBox",
                  json.dumps(popup)[:200])
            buttons = popup.get("buttons", [])
            ok_buttons = [b for b in buttons if b.get("text") == "OK"]
            check("active_popup lists clickable button refs", len(ok_buttons) == 1,
                  json.dumps(buttons)[:200])
            await call(session, "qt_click", {"ref": ok_buttons[0]["ref"]})
            await asyncio.sleep(0.3)
            gone = parse_json(await call(session, "qt_active_popup"))
            check("warning dismissed via button ref", gone.get("found") is False)

            # --- rejected tab switch: readback shows the actual outcome ---
            tabs_ref = await find_ref(session, "mainTabs")
            await call(session, "qt_set_property",
                       {"ref": name_ref, "property_name": "text", "value": ""})
            rejected = parse_json(await call(session, "qt_set_property",
                                             {"ref": tabs_ref, "property_name": "currentIndex",
                                              "value": 1}))
            check("rejected tab switch visible in readback", rejected.get("value") == 0,
                  json.dumps(rejected)[:150])
            await call(session, "qt_set_property",
                       {"ref": name_ref, "property_name": "text", "value": "Kimi1"})
            accepted = parse_json(await call(session, "qt_set_property",
                                             {"ref": tabs_ref, "property_name": "currentIndex",
                                              "value": 1}))
            check("accepted tab switch readback", accepted.get("value") == 1,
                  json.dumps(accepted)[:150])
            await asyncio.sleep(0.2)
            n = await save_shot(await call(session, "qt_screenshot"), "04_views_tab.png")
            check("qt_screenshot views tab", n > 1000, f"{n} bytes")

            # --- hidden-widget guard: nameEdit lives on the now-hidden Basic tab ---
            hidden_click = await session.call_tool("qt_click", {"ref": name_ref})
            check("guard: click hidden (other tab) -> explicit error",
                  hidden_click.is_error and "hidden" in text_of(hidden_click),
                  text_of(hidden_click)[:120])

            # --- list widget linkage ---
            list_ref = await find_ref(session, "itemList")
            await call(session, "qt_set_property",
                       {"ref": list_ref, "property_name": "currentRow", "value": 2})
            detail_ref = await find_ref(session, "listDetailLabel")
            waited = await wait_prop(session, detail_ref, "text", "Selected: Cherry")
            check("linkage: list selection -> detail label", waited.get("ok") is True,
                  json.dumps(waited)[:100])

            # --- regression: click a list item via snapshot-provided coordinates ---
            views_tree = parse_json(await call(session, "qt_snapshot")).get("tree", "")
            m = re.search(r'QListWidgetItem "Banana" \[click: (\d+),(\d+)\]', views_tree)
            check("snapshot lists items with click points", m is not None)
            if m:
                await call(session, "qt_click",
                           {"ref": list_ref,
                            "position": [int(m.group(1)), int(m.group(2))]})
                waited = await wait_prop(session, detail_ref, "text", "Selected: Banana")
                check("click list item via snapshot coordinates (viewport redirect)",
                      waited.get("ok") is True, json.dumps(waited)[:100])

            # --- regression: click a model-view item by item_text ---
            await call(session, "qt_click",
                       {"ref": list_ref, "item_text": "Apple"})
            waited = await wait_prop(session, detail_ref, "text", "Selected: Apple")
            check("click list item via item_text (generic model view)",
                  waited.get("ok") is True, json.dumps(waited)[:100])

            # --- regression: get_text dumps model-view contents ---
            lv = parse_json(await call(session, "qt_get_text", {"ref": list_ref}))
            check("get_text on item view dumps model rows",
                  all(x in lv.get("text", "") for x in ("Apple", "Banana", "Cherry")),
                  repr(lv.get("text", ""))[:80])

            # --- regression: out-of-bounds click position is rejected ---
            oob = await session.call_tool("qt_click",
                                          {"ref": list_ref, "position": [99999, 99999]})
            check("out-of-bounds click position -> explicit error",
                  oob.is_error and "outside" in text_of(oob), text_of(oob)[:120])

            # --- tree widget present ---
            tree = parse_json(await call(session, "qt_find_widget",
                                         {"object_name": "itemTree"}))
            check("tree widget found", tree.get("count", 0) >= 1, json.dumps(tree)[:100])

            # --- tree item operations: i-refs are first-class targets ---
            m_banana = re.search(r'QTreeWidgetItem "Banana" \[ref=(i\d+)\]', views_tree)
            m_fruits = re.search(r'QTreeWidgetItem "Fruits" \[ref=(i\d+)\]', views_tree)
            m_apple = re.search(r'QTreeWidgetItem "Apple" \[ref=(i\d+)\]', views_tree)
            m_carrot = re.search(r'QTreeWidgetItem "Carrot" \[ref=(i\d+)\]', views_tree)
            m_veg = re.search(r'QTreeWidgetItem "Vegetables" \[ref=(i\d+)\]', views_tree)
            ok_refs = all([m_banana, m_fruits, m_apple, m_carrot, m_veg])
            check("tree item refs in snapshot", ok_refs)
            if ok_refs:
                banana_i = m_banana.group(1)
                fruits_i = m_fruits.group(1)
                apple_i = m_apple.group(1)
                carrot_i = m_carrot.group(1)
                veg_i = m_veg.group(1)
                tree_detail_ref = await find_ref(session, "treeDetailLabel")

                await call(session, "qt_click", {"ref": banana_i})
                waited = await wait_prop(session, tree_detail_ref, "text", "Tree: Banana")
                check("click tree item selects it", waited.get("ok") is True,
                      json.dumps(waited)[:100])

                await call(session, "qt_click", {"ref": banana_i, "double_click": True})
                waited = await wait_prop(session, tree_detail_ref, "text", "Double: Banana")
                check("double click tree item", waited.get("ok") is True,
                      json.dumps(waited)[:100])

                # Carrot sits under the collapsed "Vegetables" node
                rc = await session.call_tool("qt_click", {"ref": carrot_i, "button": "right"})
                check("click item under collapsed ancestor -> not-visible error",
                      rc.is_error and "not visible" in text_of(rc), text_of(rc)[:150])

                await call(session, "qt_set_property",
                           {"ref": veg_i, "property_name": "expanded", "value": True})
                waited = parse_json(await call(session, "qt_wait_for", {
                    "condition": "property_equals", "ref": veg_i,
                    "property_name": "expanded", "value": True, "timeout_ms": 2000}))
                check("expand tree item + wait_for on i-ref", waited.get("ok") is True,
                      json.dumps(waited)[:100])

                await call(session, "qt_click", {"ref": carrot_i, "button": "right"})
                waited = await wait_prop(session, tree_detail_ref, "text", "Context: Carrot")
                check("right click tree item -> context signal", waited.get("ok") is True,
                      json.dumps(waited)[:100])

                await call(session, "qt_set_property",
                           {"ref": apple_i, "property_name": "checked", "value": True})
                item_details = parse_json(await call(session, "qt_widget_details",
                                                     {"ref": apple_i}))
                check("set tree item checked + item details",
                      item_details.get("checked") is True
                      and item_details.get("checkable") is True,
                      json.dumps(item_details)[:150])
                tree_snap = parse_json(await call(session, "qt_snapshot")).get("tree", "")
                check("snapshot shows tree item checked marker",
                      'QTreeWidgetItem "Apple"' in tree_snap and "[checked]" in tree_snap)

                await call(session, "qt_set_property",
                           {"ref": fruits_i, "property_name": "expanded", "value": False})
                fruits_details = parse_json(await call(session, "qt_widget_details",
                                                       {"ref": fruits_i}))
                check("collapse tree item", fruits_details.get("expanded") is False,
                      json.dumps(fruits_details)[:120])
                await call(session, "qt_set_property",
                           {"ref": fruits_i, "property_name": "expanded", "value": True})

                item_text = parse_json(await call(session, "qt_get_text", {"ref": banana_i}))
                check("get_text on tree item", item_text.get("text") == "Banana",
                      json.dumps(item_text)[:120])

            # --- table sum label ---
            sum_ref = await find_ref(session, "sumLabel")
            sum_text = parse_json(await call(session, "qt_get_text", {"ref": sum_ref}))
            check("table sum label", sum_text.get("text") == "Sum: 45",
                  f"sum={sum_text.get('text')!r}")

            # --- table cell editing: focus -> Ctrl+Home -> F2 -> type -> Return ---
            table_ref = await find_ref(session, "itemTable")
            await call(session, "qt_invoke_slot",
                       {"ref": table_ref, "method_name": "setFocus"})
            await call(session, "qt_key_press", {"key": "Ctrl+Home"})
            await call(session, "qt_key_press", {"key": "F2"})
            await asyncio.sleep(0.3)  # posted keys delivered; delegate editor opens
            n = await save_shot(await call(session, "qt_screenshot"), "05_table_editing.png")
            check("qt_screenshot table editing", n > 1000, f"{n} bytes")
            # the delegate editor is Qt-internal QExpandingLineEdit, not a plain
            # QLineEdit — match by substring; it is the only visible line edit
            # on this tab (nameEdit lives on the hidden Basic tab)
            editors = parse_json(await call(session, "qt_find_widget",
                                            {"pattern": "LineEdit"}))
            check("cell editor opened", editors.get("count", 0) >= 1,
                  json.dumps(editors)[:150])
            if editors.get("count", 0) >= 1:
                editor_ref = editors["widgets"][0]["ref"]
                await call(session, "qt_type_text",
                           {"ref": editor_ref, "text": "10", "clear_first": True})
                await call(session, "qt_key_press", {"key": "Return"})
                waited = await wait_prop(session, sum_ref, "text", "Sum: 54")
                check("linkage: cell edit -> sum recomputed", waited.get("ok") is True,
                      json.dumps(waited)[:100])

            # --- non-text cell editors: combo / spin delegates ---
            delegate_table_ref = await find_ref(session, "delegateTable")
            delegate_label_ref = await find_ref(session, "delegateLabel")

            # combo editor: double-click cell -> QComboBox opens -> set -> Return
            await call(session, "qt_click", {"ref": delegate_table_ref,
                                             "double_click": True, "row": 0, "col": 0})
            await asyncio.sleep(0.3)
            combos = parse_json(await call(session, "qt_find_widget",
                                           {"class_name": "QComboBox"}))
            visible_combos = [w for w in combos.get("widgets", []) if w.get("visible")]
            # the Basic-tab combo is on a hidden tab; the cell editor combo is visible
            check("combo cell editor opened", len(visible_combos) >= 1,
                  json.dumps(combos)[:150])
            if visible_combos:
                cmb = visible_combos[0]
                cur = parse_json(await call(session, "qt_get_text", {"ref": cmb["ref"]}))
                check("combo editor shows cell value", cur.get("text") == "Red",
                      repr(cur.get("text")))
                await call(session, "qt_set_property",
                           {"ref": cmb["ref"], "property_name": "currentText",
                            "value": "Blue"})
                await call(session, "qt_key_press", {"key": "Return"})
                waited = await wait_prop(session, delegate_label_ref, "text",
                                         "delegates: Blue/5 Green/10")
                check("linkage: combo cell edit committed", waited.get("ok") is True,
                      json.dumps(waited)[:100])

            # spin editor: double-click cell -> QSpinBox opens -> type -> Return
            await call(session, "qt_click", {"ref": delegate_table_ref,
                                             "double_click": True, "row": 0, "col": 1})
            await asyncio.sleep(0.3)
            spins = parse_json(await call(session, "qt_find_widget",
                                          {"class_name": "QSpinBox"}))
            visible_spins = [w for w in spins.get("widgets", []) if w.get("visible")]
            check("spin cell editor opened", len(visible_spins) >= 1,
                  json.dumps(spins)[:150])
            if visible_spins:
                sp = visible_spins[0]
                # type into the spin's internal line edit
                sp_edits = parse_json(await call(session, "qt_find_widget",
                                                 {"class_name": "QLineEdit",
                                                  "root_ref": sp["ref"],
                                                  "visible_only": False}))
                sp_edit = sp_edits["widgets"][0]
                await call(session, "qt_type_text",
                           {"ref": sp_edit["ref"], "text": "42", "clear_first": True})
                await call(session, "qt_key_press", {"key": "Return"})
                waited = await wait_prop(session, delegate_label_ref, "text",
                                         "delegates: Blue/42 Green/10")
                check("linkage: spin cell edit committed", waited.get("ok") is True,
                      json.dumps(waited)[:100])
            n = await save_shot(await call(session, "qt_screenshot"),
                                "06_delegate_table.png")
            check("qt_screenshot delegate table", n > 1000, f"{n} bytes")

            # --- back to Basic tab for the dialog flow ---
            await call(session, "qt_set_property",
                       {"ref": tabs_ref, "property_name": "currentIndex", "value": 0})
            await asyncio.sleep(0.2)

            dlg_btn_ref = await find_ref(session, "dialogButton")
            await call(session, "qt_click", {"ref": dlg_btn_ref})
            waited = parse_json(await call(session, "qt_wait_for",
                                           {"condition": "widget_visible",
                                            "object_name": "SampleDialog",
                                            "timeout_ms": 3000}))
            check("qt_wait_for dialog", waited.get("ok") is True, json.dumps(waited)[:100])

            # --- modal-block guard: main-window widgets refuse clicks while dialog is up ---
            modal_click = await session.call_tool("qt_click", {"ref": apply_ref})
            check("guard: click blocked by modal dialog -> explicit error",
                  modal_click.is_error and "modal" in text_of(modal_click),
                  text_of(modal_click)[:120])

            # --- regression: no duplicate results while a modal dialog is open ---
            dlg_found = parse_json(await call(session, "qt_find_widget",
                                              {"object_name": "dialogEdit"}))
            check("find_widget no duplicates under modal", dlg_found.get("count") == 1,
                  json.dumps(dlg_found)[:120])

            n = await save_shot(await call(session, "qt_screenshot"), "03_dialog.png")
            check("qt_screenshot dialog", n > 500, f"{n} bytes")

            dlg_edit_ref = await find_ref(session, "dialogEdit")
            await call(session, "qt_type_text", {"ref": dlg_edit_ref, "text": "hello dialog"})
            dlg_ok_ref = await find_ref(session, "okButton")
            await call(session, "qt_click", {"ref": dlg_ok_ref})
            # posted events are delivered by the app's own event loop, which runs
            # independently of MCP requests — just give it a moment, then verify
            # the modal dialog is gone (no wait_for needed for the close itself)
            await asyncio.sleep(0.5)
            gone = parse_json(await call(session, "qt_find_widget",
                                         {"object_name": "SampleDialog"}))
            check("dialog closed via posted click", gone.get("count") == 0,
                  json.dumps(gone)[:100])

            # --- regression: refs must not bind to recycled addresses ---
            # Each dialog open creates a brand-new dialogEdit, often at the same
            # heap address as the destroyed one. The registry must not hand out
            # a stale ref for it (that produced "ref not found" on type/click).
            for i in range(3):
                await call(session, "qt_click", {"ref": dlg_btn_ref})
                waited = parse_json(await call(session, "qt_wait_for",
                                               {"condition": "widget_visible",
                                                "object_name": "SampleDialog",
                                                "timeout_ms": 3000}))
                if waited.get("ok") is not True:
                    check(f"dialog recycle round {i}: opens", False, json.dumps(waited)[:100])
                    break
                edit_ref = await find_ref(session, "dialogEdit")
                ok = True
                try:
                    await call(session, "qt_type_text",
                               {"ref": edit_ref, "text": f"recycle {i}"})
                    ok_ref = await find_ref(session, "okButton")
                    await call(session, "qt_click", {"ref": ok_ref})
                except RuntimeError as exc:
                    ok = False
                    check(f"dialog recycle round {i}: refs stay valid", False, str(exc)[:120])
                await asyncio.sleep(0.4)
                gone = parse_json(await call(session, "qt_find_widget",
                                             {"object_name": "SampleDialog"}))
                if ok:
                    check(f"dialog recycle round {i}: refs stay valid",
                          gone.get("count") == 0, json.dumps(gone)[:100])
                if gone.get("count") != 0:
                    break

            # --- qt_file_dialog: non-native file & directory dialogs ---
            async def await_popup():
                for _ in range(30):
                    pop = parse_json(await call(session, "qt_active_popup", {}))
                    if pop.get("found"):
                        return pop
                    await asyncio.sleep(0.1)
                return {}

            probe_file = Path(__file__).resolve().as_posix()
            file_btn_ref = await find_ref(session, "fileButton")
            await call(session, "qt_click", {"ref": file_btn_ref})
            pop = await await_popup()
            check("file dialog popup appeared (non-native)",
                  pop.get("found") is True and pop.get("class") == "QFileDialog",
                  json.dumps(pop, ensure_ascii=False)[:150])
            fd = parse_json(await call(session, "qt_file_dialog", {"path": probe_file}))
            check("qt_file_dialog open-file accepted", fd.get("ok") is True,
                  json.dumps(fd)[:120])
            waited = await wait_prop(session, status_ref, "text", f"file: {probe_file}")
            check("file dialog result reached the app", waited.get("ok") is True,
                  json.dumps(waited)[:120])

            probe_dir = Path(__file__).resolve().parent.as_posix()
            dir_btn_ref = await find_ref(session, "dirButton")
            await call(session, "qt_click", {"ref": dir_btn_ref})
            await await_popup()
            fd = parse_json(await call(session, "qt_file_dialog", {"path": probe_dir}))
            check("qt_file_dialog directory accepted", fd.get("ok") is True,
                  json.dumps(fd)[:120])
            waited = await wait_prop(session, status_ref, "text", f"dir: {probe_dir}")
            check("directory dialog result reached the app", waited.get("ok") is True,
                  json.dumps(waited)[:120])

            # --- batch: read-back ---
            batch = parse_json(await call(session, "qt_batch", {"steps": [
                {"method": "get_text", "params": {"ref": name_ref}},
                {"method": "get_text", "params": {"ref": status_ref}},
            ]}))
            results = batch.get("results", [])
            check("qt_batch", len(results) == 2 and all(r.get("ok") for r in results),
                  json.dumps(results)[:150])

            # --- regression: a failed batch reports is_error at the top level ---
            bad_batch = await session.call_tool("qt_batch", {"steps": [
                {"method": "get_text", "params": {"ref": "w999999"}}]})
            check("batch failure sets is_error", bad_batch.is_error,
                  text_of(bad_batch)[:150])

            # --- regression: wait_for timeout reports the last seen value ---
            timeout_res = await session.call_tool("qt_wait_for", {
                "condition": "property_equals", "ref": status_ref,
                "property_name": "text", "value": "NeverHappens", "timeout_ms": 500})
            check("wait_for timeout reports last seen value",
                  timeout_res.is_error and "last seen as" in text_of(timeout_res),
                  text_of(timeout_res)[:150])

            # --- qt_debug_message ---
            msgs = parse_json(await call(session, "qt_debug_message", {"level": "debug"}))
            check("qt_debug_message", "messages" in msgs, f"count={msgs.get('count')}")

            # --- qt_host_messages: host-posted channel, read-and-clear ---
            host_msgs = parse_json(await call(session, "qt_host_messages"))
            texts = [m.get("message", "") for m in host_msgs.get("messages", [])]
            check("qt_host_messages delivers posted messages",
                  any("demo_app started" in t for t in texts)
                  and any("applyButton clicked" in t for t in texts),
                  json.dumps(texts)[:200])
            again = parse_json(await call(session, "qt_host_messages"))
            check("qt_host_messages cleared after read",
                  again.get("count") == 0, json.dumps(again)[:100])

    exit_save_flow()

    print()
    if failures:
        print(f"{len(failures)} FAILED: {', '.join(failures)}")
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))

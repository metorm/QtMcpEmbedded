# Tool Reference

**English** | [中文](usage.zh-CN.md)

For readers writing instructions for an AI agent or calling the tools by hand. These
semantics are also reflected in the `instructions` field of the `initialize` response.

## Tool Overview (21 tools)

| Tool | Description |
|---|---|
| `qt_snapshot` | structured snapshot of the widget tree (with refs, tooltips, checked/hidden/disabled markers; inlines list/tree/table entries and their click coordinates) |
| `qt_find_widget` | find widgets by objectName/class name/text/pattern (class-name matching ignores C++ namespaces — `CDockWidget` matches `ads::CDockWidget`) |
| `qt_widget_details` | full properties of a widget or tree entry (including the QAction list attached to a widget — discover triggerable actions on containers such as Ribbon groups) |
| `qt_object_tree` / `qt_list_windows` | QObject tree / top-level windows (each window carries a ref and title, ready for follow-up operations like screenshots) |
| `qt_active_popup` | the current modal/popup window (title, text, clickable refs of all buttons) — the entry point for handling blocking popups like message boxes and save confirmations |
| `qt_screenshot` | screenshot of a window or widget (PNG/JPEG, based on real rendering results) |
| `qt_click` | click (widgets and tree items alike; double-click, right-click with context menu, modifier keys, coordinates; `row`/`col` address any cell of an item view (QListView/QTableView/QTreeView/QTableWidget) directly, auto-scrolling off-screen cells into view; `item_text` hits entries by text, auto-expanding trees) |
| `qt_file_dialog` | drive the currently active QFileDialog: fill in a path and confirm/cancel (open/save/select-directory all supported). The probe sets `Qt::AA_DontUseNativeDialogs` at install() time, so file dialogs created afterwards are Qt-widget based and drivable |
| `qt_drag` | drag & drop (source widget → target widget; moves the real cursor along the path to stay compatible with drag implementations that read `QCursor::pos()`, such as ADS dock rearrangement) |
| `qt_type_text` / `qt_key_press` | text input / key press (with focus management) |
| `qt_set_property` | write a property (with write-then-read-back of `value`, so writes rejected by validation logic are detectable; supports tree-item pseudo-properties expanded/checked/selected/text) |
| `qt_invoke_slot` | invoke a slot/invokable method (≤4 arguments; the method name may be bare or carry a signature, e.g. `toggleView` / `toggleView(bool)`) |
| `qt_get_text` | extract text (widget or tree item; hidden widgets are readable too; for item views, dumps model text with rows separated by newlines and columns by tabs) |
| `qt_trigger_action` | trigger a QAction (menu/toolbar; when matching by text, submenu entries are searched recursively) |
| `qt_wait_for` | wait for a condition (widget_visible / window_count_changed / property_equals; on timeout, reports the last observed value) |
| `qt_batch` | execute multiple steps sequentially in one round trip; stops at the first failure |
| `qt_debug_message` | read the ring buffer of Qt internal messages (qDebug/qWarning etc.) |
| `qt_host_messages` | read messages the host pushed via `QtMcp::postMessage()` (read-and-clear — never delivered twice; always empty if the host never pushes) |
| `qt_app_commands` | snapshot of host custom commands (registered via `QtMcp::registerCommand`): `{name, description, available, reason}` per entry, with availability evaluated in real time |

Behavioral conventions: action-style tools use **asynchronous event posting** (no
deadlock on modal `exec()`); they **fail fast** with a reason for hidden/disabled/
modally-blocked targets (`force=true` bypasses); refs are numbered globally and
monotonically, stable across snapshots, and never silently re-bound (including the case
where the host allocator reuses the address of a destroyed widget: the old ref always
fails with an explicit error and never points at the new widget).

For the behavioral boundaries that cannot be promised, see
[pitfalls.md](pitfalls.md).

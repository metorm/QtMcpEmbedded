# Pitfalls & Known Limits

**English** | [中文](pitfalls.zh-CN.md)

## Behavioral Boundaries That Cannot Be Promised

Agents using the tools must be aware of these:

- **Event posting returns immediately; effects are not guaranteed.** `qt_click` /
  `qt_type_text` / `qt_key_press` report success as soon as the event is posted into the
  host's event queue; whether the target actually responded (button activated, text
  accepted) must be asserted via `qt_wait_for` / `qt_get_text` / property read-back.
  Do not use fixed sleeps instead of conditional waits.
- **Nothing works while the host GUI thread is blocked.** All tools run inside the
  host's event loop; while the host performs long synchronous operations (heavy
  computation, blocking IO), requests queue up until it resumes. The probe cannot bypass
  this, and there is no timeout fallback.
- **Refs are valid only while the widget is alive.** After dialogs or dynamic pages are
  destroyed, old refs fail with "ref not found" and you must re-run `qt_find_widget` —
  even if the new dialog looks exactly like the previous one.
- **Coordinate clicks (`position`) assume a static layout.** Window resizing, panel
  expand/collapse, and content scrolling all invalidate coordinates; prefer
  ref/item/row/col addressing whenever possible. Pure self-drawn components (e.g. rows
  of a Qtitan Grid, QGraphicsScene items) have no child-widget refs, so coordinates (or
  dedicated scene tools) are the only way.
- **Screenshots are off-screen renders (`QWidget::grab`)** — they do not depend on the
  window being visible on screen; but for widgets that bypass Qt's paint chain (native
  child windows, OpenGL/DirectX direct rendering, Qtitan Grid's GraphicControl) you may
  get a blank/background image — in that case, screenshot the parent container or the
  top-level window instead.
- **Waiting in `qt_wait_for` / `qt_batch` is not side-effect free**: while waiting, the
  host's event loop keeps running (timers, animations, network callbacks all execute),
  and the UI state may change on its own.
- **`qt_drag` moves the physical mouse cursor** (for compatibility with drag
  implementations that read `QCursor::pos()`); do not touch the mouse while it runs.
  Not available on Wayland.
- **Modal semantics follow Qt rules**: while an application-modal window exists,
  operations on widgets outside it are rejected (`force=true` bypasses, but the events
  are still dropped by Qt's modal filter — force is only truly effective against
  "hidden/disabled" style guards).
- **Only UI inside the Qt widget tree can be perceived.** The probe's introspection and
  operation are built on the QObject/QWidget tree and the Qt event queue: UI created
  through other channels — dialogs via direct Win32 API calls
  (`GetOpenFileName`/`IFileDialog` etc.), embedded native child windows (HWND/CWnd),
  the page DOM of QtWebEngine, OpenGL/DirectX direct-rendered content — does not appear
  in snapshots and receives no synthetic events. Qt's own file/color/font dialogs are
  **OS-native windows** by default on Windows and fall into this category; the probe
  sets `Qt::AA_DontUseNativeDialogs` at `install()` time to silently switch them to Qt
  widget implementations (no host code changes needed, and `qt_file_dialog` can drive
  them), but that attribute cannot reach code paths that bypass Qt's dialog classes and
  call OS APIs directly — those need host-side changes, otherwise they are invisible to
  the agent.
- **Single session**: one probe instance serves only one MCP session at a time. See
  [client.md](client.md#connecting-a-client-claude-code-etc) for details and
  recommended practice.

## Offscreen-Platform Qt-Level Pitfalls

The offscreen platform has two Qt-level pitfalls, worked around by `HeadlessCompat`
inside the library (enabled only when offscreen/minimal is detected; desktop platforms
are unaffected):

- **QMessageBox crash on Windows** (Qt 5.15 bug): `QMessageBox::showEvent`
  unconditionally calls `qt_getWindowsSystemMenu()`, while `platformNativeInterface()`
  is `nullptr` under offscreen/minimal, so static functions like
  `QMessageBox::warning()` segfault as soon as they pop up. The guard intercepts Show
  events targeted at QMessageBox and reproduces the behavior with public APIs (skipping
  the purely decorative system-menu-item adjustment).
- **Focus dead zone**: offscreen activates a window when it is shown, but does not
  reactivate the parent window after a modal dialog closes; afterwards `setFocus()`
  silently fails and keyboard operations without a ref have nowhere to go. The guard
  reactivates the parent window when the active transient window hides.

## Custom-Command Cautions

- **Naming rules**: the `qt_` prefix is forbidden (reserved for built-in tools), and
  names must not collide with an existing tool or command; violations return `false`
  and emit a `qWarning`. Pick a distinct, application-specific prefix instead.
- **Clients cache tools/list**: most MCP clients fetch the tool list only once, so
  commands registered after the client connected may not be visible to it. Prefer the
  two-phase startup (`autoStart=false` + `startServer()` after registration) so the
  first connection already sees the complete tool list; registrations made after the
  server started still appear in the next `tools/list` fetch.
- **Handlers run on the GUI thread**: both the handler and the availability check run
  inside the host GUI thread's event loop, so do not execute long tasks synchronously
  (they freeze both the UI and MCP). Start a background task and return immediately;
  report progress via `QtMcp::postMessage()` and let the agent poll `qt_host_messages`.
- **Cross-thread registration returns optimistically**: calling `registerCommand()`
  from a non-GUI thread after install() queues the registration to the GUI thread, and
  the return value is optimistic — name collisions only surface as `qWarning`s on the
  server side, and the registration is silently dropped if the probe is torn down before
  the queued call runs or if the context object died in the meantime. For reliable
  feedback, register from the GUI thread.

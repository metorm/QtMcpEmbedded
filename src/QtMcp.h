#ifndef QTMCP_H
#define QTMCP_H

#include <QJsonObject>
#include <QString>

#include <functional>

class QObject;

namespace QtMcp {

/// Optional host-application description, surfaced to MCP clients in the
/// `instructions` field of the initialize response. Use it to tell the AI
/// agent what this application is and how to navigate it effectively.
struct InstallOptions {
    /// Human-readable application name, e.g. "SRM Simulator".
    QString appName;
    /// Free-form guidance for the agent, e.g. main workflows, important
    /// widgets/objectNames, known pitfalls. Appended to the built-in
    /// baseline instructions; empty means baseline only.
    QString instructions;
    /// true (default): install() also starts listening on the configured
    /// port, preserving the historical one-call behavior. false: install()
    /// only assembles the probe (custom commands may be registered from this
    /// point on) and the port stays closed until startServer() is called —
    /// use this two-phase mode when commands are registered after install(),
    /// so connecting clients always see the complete tool list.
    bool autoStart = true;
};

/// Installs the embedded MCP (Model Context Protocol) server into the running
/// QApplication. The server listens on QT_MCP_HOST (default 127.0.0.1),
/// QT_MCP_PORT (default 9142) and exposes a Streamable HTTP endpoint at /mcp.
///
/// The call is a no-op unless the environment variable QT_MCP_PROBE=1 is set,
/// so it is safe to leave in production binaries. It is also idempotent:
/// calling it more than once returns the already-running probe.
///
/// Must be called after QApplication construction, on the GUI thread.
/// Returns true if the probe is active after the call.
bool install();
bool install(const InstallOptions &options);

/// Starts listening on the configured host/port. Only meaningful after
/// install() with autoStart=false; with autoStart=true the server is already
/// listening and this is a no-op. Idempotent. Returns true if the server is
/// listening after the call.
///
/// Must be called on the GUI thread (the thread that owns the QApplication).
bool startServer();

/// Result of a host-registered custom command. `data` is serialized to a
/// compact JSON string and returned to the agent as text content; isError
/// maps to MCP's isError on the tool result.
struct CommandResult {
    QJsonObject data;
    bool isError = false;

    static CommandResult ok(const QJsonObject &d);
    /// Convenience: { "error": message } with isError=true.
    static CommandResult error(const QString &message);
};

/// Handler of a host-registered custom command. Executed on the GUI thread
/// inside the host's event loop — keep it fast; for long-running work, start
/// the job asynchronously, return immediately, and report progress via
/// postMessage() (the agent polls qt_host_messages).
using CommandHandler = std::function<CommandResult(const QJsonObject &arguments)>;

/// Availability check of a custom command, evaluated on every invocation and
/// by the qt_app_commands status tool. Return an empty string when the
/// command can execute right now; return a non-empty string otherwise — the
/// string is the reason, forwarded verbatim to the agent.
using AvailabilityCheck = std::function<QString()>;

/// Registers a host-defined MCP command. Custom commands live alongside the
/// built-in qt_* tools in tools/list; the name must not be empty, must not
/// use the reserved "qt_" prefix, and must not collide with an already
/// registered tool or command (returns false + qWarning on violation).
///
/// Timing: called before install(), the registration is staged in a pending
/// queue (thread-safe) and flushed when install() assembles the probe; called
/// after install(), it goes straight into the live tool registry (from the
/// GUI thread synchronously, from any other thread via queued delivery).
/// Registrations after the server started listening appear in the next
/// tools/list fetch — note that many MCP clients fetch tools/list only once.
///
/// Cross-thread caveat: from a non-GUI thread after install(), registration
/// is queued to the GUI thread and the return value is optimistic — name
/// collisions only surface as qWarnings on the server side, and the
/// registration is silently dropped if the probe is torn down before the
/// queued call runs or if the context object died in the meantime.
///
/// `context` (optional) has QObject::connect semantics: the command is
/// automatically unregistered when the context object is destroyed, so
/// handlers capturing `this` cannot dangle.
///
/// No-op returning false when QT_MCP_PROBE is not enabled.
bool registerCommand(const QString &name,
                     const QString &description,
                     const QJsonObject &inputSchema,
                     CommandHandler handler,
                     AvailabilityCheck available = {},
                     QObject *context = nullptr);

/// Removes a command previously added with registerCommand(). Returns false
/// if no such command exists (or the probe is not enabled). From a non-GUI
/// thread after install(), the removal is queued to the GUI thread and the
/// return value is optimistic, mirroring registerCommand().
bool unregisterCommand(const QString &name);

/// Post a message to the MCP host log staging area. An AI agent receives
/// these messages via the `qt_host_messages` tool (read-and-clear), giving the
/// application a push-style channel to report runtime status without the
/// agent polling log widgets.
///
/// Thread-safe; may be called from any thread. No-op with zero overhead when
/// the probe is not installed (QT_MCP_PROBE unset) — safe to leave in
/// production code. Messages posted before install() are dropped.
/// `level` is free-form, conventionally "debug"/"info"/"warning"/"critical".
void postMessage(const QString &message, const QString &level = QStringLiteral("info"));

} // namespace QtMcp

#endif // QTMCP_H

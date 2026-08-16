#ifndef QTMCP_H
#define QTMCP_H

#include <QString>

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

} // namespace QtMcp

#endif // QTMCP_H

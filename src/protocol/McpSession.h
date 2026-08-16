#ifndef QTMCP_MCPSESSION_H
#define QTMCP_MCPSESSION_H

#include <QString>

namespace QtMcp {

/// Holds the single active MCP session: session id and negotiated protocol
/// version. The embedded server targets one local client, so a new
/// `initialize` replaces the previous session.
class McpSession
{
public:
    static QString pinnedProtocolVersion() { return QStringLiteral("2025-06-18"); }

    bool isActive() const { return !m_sessionId.isEmpty(); }
    QString sessionId() const { return m_sessionId; }
    QString protocolVersion() const { return m_protocolVersion; }

    /// Negotiate the protocol version and start a new session.
    /// Returns the new session id.
    QString startNewSession(const QString &requestedVersion);

    void endSession();

private:
    QString m_sessionId;
    QString m_protocolVersion;
};

} // namespace QtMcp

#endif // QTMCP_MCPSESSION_H

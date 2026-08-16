#ifndef QTMCP_MCPDISPATCHER_H
#define QTMCP_MCPDISPATCHER_H

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QtMcp {

class McpSession;
class ToolRegistry;

/// HTTP-relevant context extracted from request headers.
struct McpRequestContext {
    QString sessionId;       // Mcp-Session-Id header
    QString protocolVersion; // MCP-Protocol-Version header
};

/// What the transport layer should write back.
struct McpHttpResponse {
    int statusCode = 200;
    QHash<QString, QString> headers;
    QByteArray body;
    QString contentType = QStringLiteral("application/json");
};

/// Translates JSON-RPC payloads into MCP semantics: initialize handshake,
/// session validation, ping, tools/list, tools/call.
class McpDispatcher
{
public:
    McpDispatcher(McpSession &session, ToolRegistry &tools);

    McpHttpResponse handlePost(const QByteArray &body, const McpRequestContext &context);

    /// Extra content appended to the baseline `instructions` in the
    /// initialize response. Either part may be empty.
    void setHostDescription(const QString &appName, const QString &instructions);

private:
    McpSession &m_session;
    ToolRegistry &m_tools;
    QString m_hostDescription; // prebuilt suffix, empty when not set
};

} // namespace QtMcp

#endif // QTMCP_MCPDISPATCHER_H

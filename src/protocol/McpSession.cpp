#include "McpSession.h"

#include <QUuid>

namespace QtMcp {

QString McpSession::startNewSession(const QString &requestedVersion)
{
    // Version negotiation: we pin 2025-06-18. Whatever the client requested,
    // we answer with the pinned version and let the client decide whether to
    // continue (per MCP version negotiation rules).
    Q_UNUSED(requestedVersion);
    m_protocolVersion = pinnedProtocolVersion();

    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sessionId.remove(QLatin1Char('-'));
    return m_sessionId;
}

void McpSession::endSession()
{
    m_sessionId.clear();
    m_protocolVersion.clear();
}

} // namespace QtMcp

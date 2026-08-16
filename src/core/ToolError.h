#ifndef QTMCP_TOOLERROR_H
#define QTMCP_TOOLERROR_H

#include <QString>

namespace QtMcp {

/// Exception thrown by tool handlers on user-facing errors (bad ref, unknown
/// property, timeout, ...). Mapped to an MCP tool result with isError=true.
class ToolError
{
public:
    explicit ToolError(const QString &message) : m_message(message) {}

    QString message() const { return m_message; }

private:
    QString m_message;
};

} // namespace QtMcp

#endif // QTMCP_TOOLERROR_H

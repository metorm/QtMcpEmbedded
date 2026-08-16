#ifndef QTMCP_MESSAGELOG_H
#define QTMCP_MESSAGELOG_H

#include <QJsonObject>
#include <QMutex>
#include <QString>

#include <deque>

namespace QtMcp {

class ToolRegistry;

/// Captures Qt debug/warning messages via qInstallMessageHandler into a ring
/// buffer (max 500 entries). qt_messages reads at/above a severity level and
/// clears the buffer, matching qt-mcp behavior.
class MessageLog
{
public:
    static MessageLog &instance();

    /// Install the Qt message handler (idempotent). Chains to the previously
    /// installed handler so messages still reach stderr.
    void install();

    void registerTools(ToolRegistry &registry);

    /// Return {messages: [...], count: N} for entries at or above `level`
    /// ("debug"/"info"/"warning"/"critical"), then clear the whole buffer.
    QJsonObject takeMessages(const QString &level);

private:
    MessageLog() = default;

    struct Entry {
        QString level;
        QString message;
        double timestamp = 0.0;
    };

    static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &message);
    void append(const QString &level, const QString &message);

    static const int MAX_ENTRIES = 500;

    std::deque<Entry> m_buffer;
    QMutex m_mutex;
    QtMessageHandler m_previous = nullptr;
    bool m_installed = false;
};

} // namespace QtMcp

#endif // QTMCP_MESSAGELOG_H

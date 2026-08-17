#include "HostLog.h"

#include <QDateTime>
#include <QJsonArray>

#include "../protocol/ToolRegistry.h"

namespace QtMcp {

HostLog &HostLog::instance()
{
    static HostLog log;
    return log;
}

void HostLog::setEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_enabled = enabled;
}

bool HostLog::isEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_enabled;
}

void HostLog::post(const QString &level, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_enabled)
        return;
    if (int(m_buffer.size()) >= MAX_ENTRIES) {
        m_buffer.pop_front();
        ++m_dropped;
    }
    m_buffer.push_back(Entry{level, message,
                             QDateTime::currentMSecsSinceEpoch() / 1000.0});
}

void HostLog::registerTools(ToolRegistry &registry)
{
    const QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{}},
    };

    registry.registerTool(
        QStringLiteral("qt_host_messages"),
        QStringLiteral("Return messages the host application posted via QtMcp::postMessage() "
                       "since the last call, then clear the staging buffer (no duplicates on "
                       "repeat reads). Always returns empty if the host never posts. Use this "
                       "instead of polling log widgets when the host supports it."),
        schema, [](const QJsonObject &) {
            return ToolResult::fromData(HostLog::instance().takeMessages());
        });
}

QJsonObject HostLog::takeMessages()
{
    QJsonArray messages;
    int dropped = 0;
    {
        QMutexLocker locker(&m_mutex);
        for (const Entry &entry : m_buffer) {
            messages.append(QJsonObject{
                {QStringLiteral("level"), entry.level},
                {QStringLiteral("message"), entry.message},
                {QStringLiteral("timestamp"), entry.timestamp},
            });
        }
        m_buffer.clear();
        dropped = m_dropped;
        m_dropped = 0;
    }

    return QJsonObject{
        {QStringLiteral("messages"), messages},
        {QStringLiteral("count"), messages.size()},
        {QStringLiteral("dropped"), dropped},
    };
}

} // namespace QtMcp

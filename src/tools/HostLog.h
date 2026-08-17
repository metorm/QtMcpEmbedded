#ifndef QTMCP_HOSTLOG_H
#define QTMCP_HOSTLOG_H

#include <QJsonObject>
#include <QMutex>
#include <QString>

#include <deque>

// Build-time knob (CMake -DQTMCP_HOST_LOG_CAPACITY=N / qmake
// QTMCP_HOST_LOG_CAPACITY=N): capacity of the postMessage staging buffer.
#ifndef QTMCP_HOST_LOG_CAPACITY
#define QTMCP_HOST_LOG_CAPACITY 500
#endif

namespace QtMcp {

class ToolRegistry;

/// Staging area for messages the host application posts proactively via
/// QtMcp::postMessage(). qt_host_messages drains the buffer (read-and-clear),
/// giving the application a push-style channel to the AI agent without the
/// agent having to poll log widgets.
///
/// The buffer stays disabled until the probe is installed; post() is then a
/// no-op with zero overhead, and qt_host_messages always returns empty if the
/// host never posts.
class HostLog
{
public:
    static HostLog &instance();

    /// Enable/disable staging. Called by ProbeServer; disabled by default so
    /// post() is a no-op in production builds (probe not installed).
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /// Append a message. Thread-safe (mutex only, safe from any thread).
    /// Ring buffer: when full, the oldest entry is dropped and counted.
    void post(const QString &level, const QString &message);

    void registerTools(ToolRegistry &registry);

    /// Return {messages: [...], count: N, dropped: M} for every staged entry,
    /// then clear the buffer and reset the dropped counter.
    QJsonObject takeMessages();

private:
    HostLog() = default;

    struct Entry {
        QString level;
        QString message;
        double timestamp = 0.0;
    };

    static const int MAX_ENTRIES = QTMCP_HOST_LOG_CAPACITY;

    std::deque<Entry> m_buffer;
    mutable QMutex m_mutex;
    int m_dropped = 0;
    bool m_enabled = false;
};

} // namespace QtMcp

#endif // QTMCP_HOSTLOG_H

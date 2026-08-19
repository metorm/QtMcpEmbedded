#ifndef QTMCP_PROBESERVER_H
#define QTMCP_PROBESERVER_H

#include <QObject>
#include <QPointer>

#include "../core/RefRegistry.h"
#include "../protocol/McpDispatcher.h"
#include "../protocol/McpSession.h"
#include "../protocol/ToolRegistry.h"
#include "../tools/Introspector.h"
#include "../tools/Interactor.h"
#include "../tools/Screenshotter.h"
#include "../transport/HttpServer.h"

class QHostAddress;

namespace QtMcp {

/// Top-level coordinator: owns the HTTP transport, the MCP protocol layer and
/// all tools. Lives as a child of QApplication, entirely on the GUI thread.
class ProbeServer : public QObject
{
    Q_OBJECT
public:
    explicit ProbeServer(QObject *parent = nullptr);

    bool start(const QHostAddress &address, quint16 port);

    /// Host-application self-description, surfaced in the MCP initialize
    /// response's `instructions` field. Call before start().
    void setHostDescription(const QString &appName, const QString &instructions);

    /// Registers a host-defined custom command. `handler` is invoked only
    /// after `availability` (if given) reports the command as executable;
    /// otherwise the call fails with isError and the reason text. Refuses
    /// (false + qWarning) when the name collides with an existing tool.
    /// Call on the GUI thread.
    bool registerHostCommand(const QString &name, const QString &description,
                             const QJsonObject &inputSchema,
                             ToolRegistry::Handler handler,
                             std::function<QString()> availability = {});
    bool unregisterHostCommand(const QString &name);

private:
    void onHttpRequest(const QtMcp::HttpRequest &request, QtMcp::HttpConnection *connection);
    void registerTools();

    struct HostCommand {
        QString description;
        std::function<QString()> availability;
    };

    RefRegistry m_registry;
    ToolRegistry m_tools;
    McpSession m_session;
    McpDispatcher m_dispatcher;
    HttpServer *m_http = nullptr;
    Introspector m_introspector;
    Interactor m_interactor;
    Screenshotter m_screenshotter;
    QHash<QString, HostCommand> m_hostCommands;
    QStringList m_hostOrder; // stable listing order for qt_app_commands
};

} // namespace QtMcp

#endif // QTMCP_PROBESERVER_H

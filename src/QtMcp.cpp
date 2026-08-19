#include "QtMcp.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QHostAddress>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

#include "core/ProbeServer.h"
#include "tools/HostLog.h"

namespace QtMcp {

namespace {

const int DEFAULT_PORT = 9142;
const char PROBE_OBJECT_NAME[] = "qt_mcp_probe";

bool probeEnabled()
{
    return qEnvironmentVariable("QT_MCP_PROBE") == QLatin1String("1");
}

/// Registration staged before install() assembles the probe.
struct PendingCommand {
    QString name;
    QString description;
    QJsonObject schema;
    CommandHandler handler;
    AvailabilityCheck available;
};

QMutex g_mutex;
QList<PendingCommand> g_pending;
ProbeServer *g_server = nullptr; // owned by QApplication; cleared on destroyed
QHostAddress g_host;
quint16 g_port = 0;
bool g_listening = false;
// Auto-unregister connections established for registered commands, keyed by
// command name. Tracked so a stale context dying later cannot unregister a
// newer command that reused the name.
QHash<QString, QMetaObject::Connection> g_contextConnections;

/// Adapt the public CommandResult-based handler to the internal
/// ToolRegistry::Handler (ToolResult) signature.
ToolRegistry::Handler wrapHandler(CommandHandler handler)
{
    return [handler = std::move(handler)](const QJsonObject &args) -> ToolResult {
        const CommandResult result = handler(args);
        ToolResult toolResult = ToolResult::fromData(result.data);
        toolResult.isError = result.isError;
        return toolResult;
    };
}

/// g_mutex must be held. Registers into the live probe; returns false (with a
/// qWarning from ProbeServer) on name collision.
bool registerIntoServer(ProbeServer *server, const QString &name,
                        const QString &description, const QJsonObject &schema,
                        const CommandHandler &handler,
                        const AvailabilityCheck &available)
{
    return server->registerHostCommand(name, description, schema,
                                       wrapHandler(handler), available);
}

/// g_mutex must be held. Arms the context-destroyed auto-unregister for a
/// command that was *successfully* registered. Replaces any connection left
/// over from a previous registration under the same name, so a stale context
/// dying later cannot unregister the new command.
void armContextAutoUnregister(const QString &name, QObject *context)
{
    if (!context)
        return;
    const auto old = g_contextConnections.find(name);
    if (old != g_contextConnections.end()) {
        QObject::disconnect(old.value());
        g_contextConnections.erase(old);
    }
    g_contextConnections.insert(name, QObject::connect(context, &QObject::destroyed,
                                [name]() { unregisterCommand(name); }));
}

/// g_mutex must be held. Drops the auto-unregister connection of a command
/// that no longer exists. Safe to call from the connection's own emission
/// (disconnecting the currently-running connection is allowed).
void disarmContextAutoUnregister(const QString &name)
{
    const auto it = g_contextConnections.find(name);
    if (it != g_contextConnections.end()) {
        QObject::disconnect(it.value());
        g_contextConnections.erase(it);
    }
}

} // namespace

CommandResult CommandResult::ok(const QJsonObject &d)
{
    CommandResult r;
    r.data = d;
    return r;
}

CommandResult CommandResult::error(const QString &message)
{
    CommandResult r;
    r.data = QJsonObject{{QStringLiteral("error"), message}};
    r.isError = true;
    return r;
}

void postMessage(const QString &message, const QString &level)
{
    HostLog::instance().post(level, message);
}

bool registerCommand(const QString &name, const QString &description,
                     const QJsonObject &inputSchema, CommandHandler handler,
                     AvailabilityCheck available, QObject *context)
{
    if (!probeEnabled())
        return false;

    if (name.isEmpty() || name.startsWith(QLatin1String("qt_"))) {
        qWarning("QtMcp: refusing to register command '%s': name must be non-empty "
                 "and must not use the reserved 'qt_' prefix", qPrintable(name));
        return false;
    }
    if (!handler) {
        qWarning("QtMcp: refusing to register command '%s' without a handler",
                 qPrintable(name));
        return false;
    }

    // Auto-unregister when the context object dies, so handlers capturing
    // `this` cannot dangle. The connection is only armed AFTER a successful
    // registration — arming it earlier would let a dying context unregister
    // someone else's legitimate command that reused the name.

    QMutexLocker locker(&g_mutex);
    if (g_server) {
        if (QThread::currentThread() == g_server->thread()) {
            if (!registerIntoServer(g_server, name, description, inputSchema,
                                    handler, available))
                return false;
            armContextAutoUnregister(name, context);
            return true;
        }
        // Off-thread: deliver to the GUI thread. The result of a queued
        // registration cannot be reported back; collisions surface as
        // qWarnings on the server side. The context is forwarded (guarded by
        // QPointer) so the auto-unregister connection is armed there, after
        // the registration actually succeeds; if the context died before
        // delivery, registration is dropped — its handler would dangle anyway.
        QMetaObject::invokeMethod(g_server,
                                  [name, description, inputSchema,
                                   handler = std::move(handler),
                                   available = std::move(available),
                                   contextGuard = QPointer<QObject>(context),
                                   hadContext = (context != nullptr)]() {
                                      if (hadContext && !contextGuard)
                                          return;
                                      registerCommand(name, description, inputSchema,
                                                      handler, available,
                                                      contextGuard.data());
                                  },
                                  Qt::QueuedConnection);
        return true;
    }

    for (const PendingCommand &pending : g_pending) {
        if (pending.name == name) {
            qWarning("QtMcp: refusing to register command '%s': already registered",
                     qPrintable(name));
            return false;
        }
    }
    g_pending.append(PendingCommand{name, description, inputSchema,
                                    std::move(handler), std::move(available)});
    armContextAutoUnregister(name, context);
    return true;
}

bool unregisterCommand(const QString &name)
{
    if (!probeEnabled())
        return false;

    QMutexLocker locker(&g_mutex);
    if (g_server) {
        if (QThread::currentThread() == g_server->thread()) {
            if (!g_server->unregisterHostCommand(name))
                return false;
            disarmContextAutoUnregister(name);
            return true;
        }
        QMetaObject::invokeMethod(g_server,
                                  [name]() { unregisterCommand(name); },
                                  Qt::QueuedConnection);
        disarmContextAutoUnregister(name);
        return true;
    }
    for (int i = 0; i < g_pending.size(); ++i) {
        if (g_pending[i].name == name) {
            g_pending.removeAt(i);
            disarmContextAutoUnregister(name);
            return true;
        }
    }
    return false;
}

bool install()
{
    return install(InstallOptions{});
}

bool install(const InstallOptions &options)
{
    if (!probeEnabled())
        return false;

    QCoreApplication *app = QCoreApplication::instance();
    if (!app) {
        qWarning("QtMcp: install() called without a QApplication instance");
        return false;
    }

    // File dialogs become Qt widgets instead of OS-native dialogs, so agents
    // can introspect and drive them (qt_file_dialog). Read at dialog-creation
    // time, so setting it here covers every dialog opened afterwards.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    // Idempotent: return the existing probe if already installed.
    const QObjectList children = app->children();
    for (QObject *child : children) {
        if (child->objectName() == QLatin1String(PROBE_OBJECT_NAME))
            return true;
    }

    const QString host = qEnvironmentVariable("QT_MCP_HOST", QStringLiteral("127.0.0.1"));
    bool portOk = false;
    const int portEnv = qEnvironmentVariableIntValue("QT_MCP_PORT", &portOk);
    const quint16 port = portOk ? quint16(portEnv) : quint16(DEFAULT_PORT);

    auto *server = new ProbeServer(app);
    server->setHostDescription(options.appName, options.instructions);

    QList<PendingCommand> staged;
    {
        QMutexLocker locker(&g_mutex);
        g_server = server;
        g_host = QHostAddress(host);
        g_port = port;
        // Flush registrations staged before install().
        staged = std::move(g_pending);
        for (const PendingCommand &pending : staged)
            registerIntoServer(server, pending.name, pending.description,
                               pending.schema, pending.handler, pending.available);
    }

    QObject::connect(server, &QObject::destroyed, []() {
        QMutexLocker locker(&g_mutex);
        g_server = nullptr;
        g_listening = false;
    });

    if (options.autoStart) {
        if (startServer())
            return true;
        // Historical behavior: a failed listen removes the probe entirely, so
        // a later install() can retry once the port conflict is resolved. The
        // destroyed connection above clears g_server/g_listening. Re-stage the
        // flushed commands so the retried install() picks them up again.
        {
            QMutexLocker locker(&g_mutex);
            g_pending = staged + g_pending;
        }
        delete server;
        return false;
    }
    return true;
}

bool startServer()
{
    if (!probeEnabled())
        return false;

    QMutexLocker locker(&g_mutex);
    if (!g_server) {
        qWarning("QtMcp: startServer() called before install()");
        return false;
    }
    if (g_listening)
        return true;

    if (!g_server->start(g_host, g_port)) {
        qWarning("QtMcp: failed to listen on %s:%d",
                 qPrintable(g_host.toString()), int(g_port));
        return false;
    }
    g_listening = true;
    qInfo("QtMcp: MCP server listening on http://%s:%d/mcp",
          qPrintable(g_host.toString()), int(g_port));
    return true;
}

} // namespace QtMcp

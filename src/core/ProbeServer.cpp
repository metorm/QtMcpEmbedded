#include "ProbeServer.h"

#include <QDebug>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include "ToolError.h"
#include "HeadlessCompat.h"
#include "../tools/HostLog.h"
#include "../tools/MessageLog.h"
#include "../transport/SseStream.h"

namespace QtMcp {

namespace {

const char PROBE_OBJECT_NAME[] = "qt_mcp_probe";

/// MCP requires validating the Origin header (DNS rebinding protection):
/// only "null" and localhost origins are accepted.
bool isAllowedOrigin(const QString &origin)
{
    if (origin.isEmpty() || origin == QLatin1String("null"))
        return true;

    QString host = origin;
    const int schemeEnd = host.indexOf(QStringLiteral("://"));
    if (schemeEnd >= 0)
        host = host.mid(schemeEnd + 3);
    const int slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        host = host.left(slash);
    const int colon = host.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        host = host.left(colon);

    return host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
           || host == QLatin1String("127.0.0.1")
           || host == QLatin1String("::1")
           || host == QLatin1String("[::1]");
}

QHash<QString, QString> jsonHeaders()
{
    QHash<QString, QString> headers;
    headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    return headers;
}

QJsonObject stepOk(const QJsonObject &result)
{
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
}

QJsonObject stepError(const QString &error)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

} // namespace

ProbeServer::ProbeServer(QObject *parent)
    : QObject(parent),
      m_dispatcher(m_session, m_tools),
      m_http(new HttpServer(this)),
      m_introspector(m_registry),
      m_interactor(m_registry),
      m_screenshotter(m_registry)
{
    setObjectName(QString::fromLatin1(PROBE_OBJECT_NAME));

    MessageLog::instance().install();
    HostLog::instance().setEnabled(true);
    HeadlessCompat::installIfNeeded(this);
    registerTools();

    connect(m_http, &HttpServer::requestReceived, this, &ProbeServer::onHttpRequest);
}

bool ProbeServer::start(const QHostAddress &address, quint16 port)
{
    return m_http->listen(address, port);
}

void ProbeServer::setHostDescription(const QString &appName, const QString &instructions)
{
    m_dispatcher.setHostDescription(appName, instructions);
}

bool ProbeServer::registerHostCommand(const QString &name, const QString &description,
                                      const QJsonObject &inputSchema,
                                      ToolRegistry::Handler handler,
                                      std::function<QString()> availability)
{
    if (m_tools.hasTool(name)) {
        qWarning("QtMcp: refusing to register command '%s': name already in use",
                 qPrintable(name));
        return false;
    }

    // Enforce availability on every invocation: a non-empty reason fails the
    // call with isError before the host handler runs.
    ToolRegistry::Handler guarded =
        [name, handler = std::move(handler), availability](const QJsonObject &args) -> ToolResult {
            if (availability) {
                const QString reason = availability();
                if (!reason.isEmpty()) {
                    ToolResult denied = ToolResult::fromData(QJsonObject{
                        {QStringLiteral("error"),
                         QStringLiteral("Command '%1' is not available now: %2")
                             .arg(name, reason)},
                    });
                    denied.isError = true;
                    return denied;
                }
            }
            return handler(args);
        };

    m_tools.registerTool(name, description, inputSchema, std::move(guarded));
    HostCommand command;
    command.description = description;
    command.availability = std::move(availability);
    m_hostCommands.insert(name, command);
    m_hostOrder.append(name);
    return true;
}

bool ProbeServer::unregisterHostCommand(const QString &name)
{
    if (!m_hostCommands.contains(name))
        return false;
    m_hostCommands.remove(name);
    m_hostOrder.removeAll(name);
    m_tools.unregister(name);
    return true;
}

void ProbeServer::onHttpRequest(const QtMcp::HttpRequest &request,
                                QtMcp::HttpConnection *connection)
{
    // Requests are dispatched immediately, even while an earlier request is
    // still on the stack inside processEvents() (qt_wait_for / qt_batch) or
    // the app itself sits in a nested event loop (modal QDialog::exec()).
    // Queueing here would deadlock modal dialogs: the request that closes the
    // dialog could never run. Reentrancy is safe because everything executes
    // on the GUI thread, sequentially consistent — the same model qt-mcp's
    // Python probe uses.
    if (!connection)
        return;

    if (!isAllowedOrigin(request.header(QStringLiteral("origin")))) {
        connection->respond(403, jsonHeaders(), "{\"error\":\"Forbidden origin\"}");
        return;
    }

    if (request.path != QLatin1String("/mcp")) {
        connection->respond(404, jsonHeaders(), "{\"error\":\"Not found\"}");
        return;
    }

    if (request.method == QLatin1String("POST")) {
        McpRequestContext context;
        context.sessionId = request.header(QStringLiteral("mcp-session-id"));
        context.protocolVersion = request.header(QStringLiteral("mcp-protocol-version"));
        const McpHttpResponse response = m_dispatcher.handlePost(request.body, context);
        QHash<QString, QString> headers = response.headers;
        headers.insert(QStringLiteral("Content-Type"), response.contentType);
        connection->respond(response.statusCode, headers, response.body);
        return;
    }

    if (request.method == QLatin1String("GET")) {
        // SSE stream: kept open for protocol compliance; heartbeats only.
        QHash<QString, QString> headers;
        headers.insert(QStringLiteral("Content-Type"), QStringLiteral("text/event-stream"));
        headers.insert(QStringLiteral("Cache-Control"), QStringLiteral("no-cache"));
        connection->startStream(200, headers);
        new SseStream(connection->socket()); // parented to the socket
        return;
    }

    if (request.method == QLatin1String("DELETE")) {
        // Session termination: optional per spec, implemented as a no-op.
        connection->respond(200, jsonHeaders(), "{}");
        return;
    }

    connection->respond(405, jsonHeaders(), "{\"error\":\"Method not allowed\"}");
}

void ProbeServer::registerTools()
{
    m_introspector.registerTools(m_tools);
    m_interactor.registerTools(m_tools);
    m_screenshotter.registerTools(m_tools);
    MessageLog::instance().registerTools(m_tools);
    HostLog::instance().registerTools(m_tools);

    // qt_batch dispatches through the registry; step method names accept both
    // the qt_* tool names and the qt-mcp probe method names (click, type_text,
    // ...). "wait" is a special step that just pumps events.
    const QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("steps"),
              QJsonObject{
                  {QStringLiteral("type"), QStringLiteral("array")},
                  {QStringLiteral("description"),
                   QStringLiteral("Each step: {method, params?, wait_ms?}. method is a tool "
                                  "name ('qt_click' or 'click'); 'wait' with params {ms} pumps "
                                  "events. Execution stops at the first error.")},
                  {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
              }},
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("steps")}},
    };

    m_tools.registerTool(
        QStringLiteral("qt_batch"),
        QStringLiteral("Execute multiple Qt operations sequentially in one round trip. "
                       "Stops on the first error and returns partial results."),
        schema, [this](const QJsonObject &args) -> ToolResult {
            const QJsonArray steps = args.value(QStringLiteral("steps")).toArray();
            QJsonArray results;
            int completed = 0;
            QJsonValue failedAt(QJsonValue::Null);

            for (int i = 0; i < steps.size(); ++i) {
                const QJsonObject step = steps.at(i).toObject();
                const QString method = step.value(QStringLiteral("method")).toString();
                const QJsonObject params = step.value(QStringLiteral("params")).toObject();
                const int waitMs = step.value(QStringLiteral("wait_ms")).toInt(0);

                if (method.isEmpty()) {
                    results.append(stepError(QStringLiteral("Missing 'method' in step")));
                    failedAt = i;
                    break;
                }
                if (method == QLatin1String("batch") || method == QLatin1String("qt_batch")) {
                    results.append(stepError(QStringLiteral("Nested batch not allowed")));
                    failedAt = i;
                    break;
                }

                try {
                    if (method == QLatin1String("wait")) {
                        const int ms = params.value(QStringLiteral("ms")).toInt(0);
                        Interactor::processEventsFor(ms);
                        results.append(stepOk(QJsonObject{{QStringLiteral("waited_ms"), ms}}));
                    } else {
                        const QString toolName = method.startsWith(QLatin1String("qt_"))
                                                     ? method
                                                     : QStringLiteral("qt_") + method;
                        ToolResult stepResult = m_tools.call(toolName, params);
                        QJsonObject data = stepResult.data;
                        if (stepResult.isImage) {
                            // Don't inline base64 screenshots in batch output.
                            data.remove(QStringLiteral("image"));
                        }
                        results.append(stepOk(data));
                    }
                } catch (const ToolError &err) {
                    results.append(stepError(err.message()));
                    failedAt = i;
                    break;
                } catch (...) {
                    results.append(stepError(QStringLiteral("Internal error")));
                    failedAt = i;
                    break;
                }

                completed = i + 1;
                if (waitMs > 0)
                    Interactor::processEventsFor(waitMs);
            }

            ToolResult batchResult = ToolResult::fromData(QJsonObject{
                {QStringLiteral("results"), results},
                {QStringLiteral("completed"), completed},
                {QStringLiteral("failed_at"), failedAt},
            });
            // A stopped-early batch is an overall failure even though per-step
            // details are in the payload — agent frameworks key on isError.
            batchResult.isError = !failedAt.isNull();
            return batchResult;
        });

    // Snapshot of host-registered custom commands with live availability, so
    // agents can ask "what can I run right now, and why not" before planning
    // instead of probing by trial and error.
    m_tools.registerTool(
        QStringLiteral("qt_app_commands"),
        QStringLiteral("List the host application's custom commands (registered via "
                       "QtMcp::registerCommand) with live availability. Each entry: "
                       "{name, description, available, reason}; 'reason' explains why "
                       "a command cannot run right now. Custom commands also enforce "
                       "availability at call time."),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}},
        [this](const QJsonObject &) -> ToolResult {
            QJsonArray commands;
            for (const QString &name : m_hostOrder) {
                const HostCommand &command = m_hostCommands[name];
                QString reason;
                if (command.availability)
                    reason = command.availability();
                commands.append(QJsonObject{
                    {QStringLiteral("name"), name},
                    {QStringLiteral("description"), command.description},
                    {QStringLiteral("available"), reason.isEmpty()},
                    {QStringLiteral("reason"), reason},
                });
            }
            return ToolResult::fromData(QJsonObject{
                {QStringLiteral("commands"), commands},
                {QStringLiteral("count"), int(commands.size())},
            });
        });
}

} // namespace QtMcp

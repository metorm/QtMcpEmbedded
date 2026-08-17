#include "McpDispatcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

#include "../core/ToolError.h"
#include "JsonRpc.h"
#include "McpSession.h"
#include "ToolRegistry.h"

namespace QtMcp {

namespace {

const char SERVER_NAME[] = "qt-mcp-embedded";
const char SERVER_VERSION[] = "0.1.0";

const char BASELINE_INSTRUCTIONS[] =
    "Embedded Qt Widgets inspection and automation probe. "
    "Use qt_snapshot/qt_find_widget to discover widgets, then "
    "qt_click/qt_type_text/qt_get_text etc. with the returned refs. "
    "If the host application exits (e.g. after you trigger a quit/close action or "
    "confirm an exit prompt), this server shuts down with it: the connection drops "
    "and further calls fail — that is normal termination, not an error. "
    "Behavioral limits you MUST respect: "
    "(1) Action tools (qt_click/qt_type_text/qt_key_press/qt_trigger_action) post "
    "events asynchronously and return immediately — success means 'event posted', "
    "NOT 'effect happened'. Confirm outcomes with qt_wait_for/qt_get_text/property "
    "reads; never rely on fixed sleeps. "
    "(2) Everything runs on the host GUI thread; while the host is busy in a long "
    "synchronous operation, requests queue — there is no timeout and no way around it. "
    "(3) Refs are valid only while their widget is alive. After a dialog/panel is "
    "destroyed, its old refs fail with 'ref not found' — call qt_find_widget again. "
    "Refs are never silently rebound to a different widget. "
    "(4) Coordinate clicks (position) assume a static layout: resizing, expand/"
    "collapse and scrolling invalidate them. Prefer ref/item/row/col addressing. "
    "Painted-only elements (e.g. owner-drawn grid rows) have no refs — coordinates "
    "are the only way to hit them. "
    "(5) Screenshots are off-screen renders; widgets painting outside Qt's pipeline "
    "(native child windows, OpenGL/direct rendering) may come out blank — capture a "
    "parent container or the top-level window instead. "
    "(6) While qt_wait_for/qt_batch waits, the host event loop keeps running: "
    "timers, animations and async callbacks may change the UI underneath you. "
    "(7) qt_drag moves the physical mouse cursor — do not touch the mouse during a "
    "drag; it is unavailable on Wayland. "
    "(8) While an application-modal window is up, operations on widgets outside it "
    "are refused; force=true bypasses the guard for hidden/disabled widgets only — "
    "Qt still discards events blocked by modality.";

QByteArray toBytes(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

McpHttpResponse jsonResponse(int statusCode, const QJsonObject &payload)
{
    McpHttpResponse resp;
    resp.statusCode = statusCode;
    resp.body = toBytes(payload);
    return resp;
}

QJsonObject toolResultToJson(const ToolResult &result)
{
    QJsonArray content;
    if (result.isImage) {
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image")},
            {QStringLiteral("data"), QString::fromLatin1(result.imageBase64)},
            {QStringLiteral("mimeType"), result.mimeType},
        });
        if (!result.imageText.isEmpty()) {
            content.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"), result.imageText},
            });
        }
    } else {
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"),
             QString::fromUtf8(QJsonDocument(result.data).toJson(QJsonDocument::Compact))},
        });
    }
    return QJsonObject{
        {QStringLiteral("content"), content},
        {QStringLiteral("isError"), result.isError},
    };
}

QJsonObject toolErrorToJson(const QString &message)
{
    return QJsonObject{
        {QStringLiteral("content"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("Error: %1").arg(message)},
         }}},
        {QStringLiteral("isError"), true},
    };
}

} // namespace

McpDispatcher::McpDispatcher(McpSession &session, ToolRegistry &tools)
    : m_session(session), m_tools(tools)
{
}

void McpDispatcher::setHostDescription(const QString &appName, const QString &instructions)
{
    QStringList parts;
    if (!appName.isEmpty())
        parts << QStringLiteral("Application: %1").arg(appName);
    if (!instructions.isEmpty())
        parts << instructions;
    if (!parts.isEmpty())
        m_hostDescription = QStringLiteral("\n\n") + parts.join(QStringLiteral("\n"));
}

McpHttpResponse McpDispatcher::handlePost(const QByteArray &body, const McpRequestContext &context)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return jsonResponse(400, JsonRpc::makeError(QJsonValue(QJsonValue::Null),
                                                    JsonRpc::ParseError,
                                                    QStringLiteral("Parse error: %1")
                                                        .arg(parseError.errorString())));
    }

    const JsonRpc::Request req = JsonRpc::parseRequest(doc);
    if (!req.valid) {
        return jsonResponse(400, JsonRpc::makeError(req.id, req.errorCode, req.errorMessage));
    }

    // ---- initialize: allowed without a session, creates one ----
    if (req.method == QLatin1String("initialize")) {
        if (req.isNotification)
            return jsonResponse(400, JsonRpc::makeError(QJsonValue(QJsonValue::Null),
                                                        JsonRpc::InvalidRequest,
                                                        QStringLiteral("initialize requires an id")));

        const QString requestedVersion =
            req.params.value(QStringLiteral("protocolVersion")).toString();
        const QString sessionId = m_session.startNewSession(requestedVersion);

        const QJsonObject result{
            {QStringLiteral("protocolVersion"), m_session.protocolVersion()},
            {QStringLiteral("capabilities"),
             QJsonObject{
                 {QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), false}}},
             }},
            {QStringLiteral("serverInfo"),
             QJsonObject{
                 {QStringLiteral("name"), QString::fromLatin1(SERVER_NAME)},
                 {QStringLiteral("version"), QString::fromLatin1(SERVER_VERSION)},
             }},
            {QStringLiteral("instructions"),
             QString::fromLatin1(BASELINE_INSTRUCTIONS) + m_hostDescription},
        };

        McpHttpResponse resp = jsonResponse(200, JsonRpc::makeResponse(req.id, result));
        resp.headers.insert(QStringLiteral("Mcp-Session-Id"), sessionId);
        return resp;
    }

    // ---- all other methods require a valid session ----
    if (!m_session.isActive() || context.sessionId != m_session.sessionId()) {
        return jsonResponse(404, JsonRpc::makeError(req.id, JsonRpc::InvalidRequest,
                                                    QStringLiteral("Missing or invalid Mcp-Session-Id")));
    }

    if (!context.protocolVersion.isEmpty()
        && context.protocolVersion != m_session.protocolVersion()) {
        return jsonResponse(400, JsonRpc::makeError(req.id, JsonRpc::InvalidRequest,
                                                    QStringLiteral("Unsupported MCP-Protocol-Version: %1")
                                                        .arg(context.protocolVersion)));
    }

    // ---- notifications: never answered ----
    if (req.isNotification) {
        McpHttpResponse resp;
        resp.statusCode = 202;
        resp.body.clear();
        return resp;
    }

    // ---- requests ----
    if (req.method == QLatin1String("ping"))
        return jsonResponse(200, JsonRpc::makeResponse(req.id, QJsonObject{}));

    if (req.method == QLatin1String("tools/list")) {
        const QJsonObject result{{QStringLiteral("tools"), m_tools.toolList()}};
        return jsonResponse(200, JsonRpc::makeResponse(req.id, result));
    }

    if (req.method == QLatin1String("tools/call")) {
        const QString name = req.params.value(QStringLiteral("name")).toString();
        const QJsonObject arguments =
            req.params.value(QStringLiteral("arguments")).toObject();
        if (name.isEmpty()) {
            return jsonResponse(200, JsonRpc::makeError(req.id, JsonRpc::InvalidParams,
                                                        QStringLiteral("tools/call requires \"name\"")));
        }
        try {
            const ToolResult result = m_tools.call(name, arguments);
            return jsonResponse(200, JsonRpc::makeResponse(req.id, toolResultToJson(result)));
        } catch (const ToolError &err) {
            return jsonResponse(200, JsonRpc::makeResponse(req.id, toolErrorToJson(err.message())));
        } catch (...) {
            return jsonResponse(200, JsonRpc::makeResponse(req.id, toolErrorToJson(
                                         QStringLiteral("Internal error while running %1").arg(name))));
        }
    }

    return jsonResponse(200, JsonRpc::makeError(req.id, JsonRpc::MethodNotFound,
                                                QStringLiteral("Method not found: %1").arg(req.method)));
}

} // namespace QtMcp

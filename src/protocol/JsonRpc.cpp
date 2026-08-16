#include "JsonRpc.h"

namespace QtMcp {
namespace JsonRpc {

Request parseRequest(const QJsonDocument &doc)
{
    Request req;
    if (!doc.isObject()) {
        req.errorCode = InvalidRequest;
        req.errorMessage = QStringLiteral("Request must be a JSON object");
        return req;
    }

    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0")) {
        req.errorCode = InvalidRequest;
        req.errorMessage = QStringLiteral("Missing or invalid \"jsonrpc\" (expected \"2.0\")");
        return req;
    }

    const QJsonValue method = obj.value(QStringLiteral("method"));
    if (!method.isString() || method.toString().isEmpty()) {
        req.errorCode = InvalidRequest;
        req.errorMessage = QStringLiteral("Missing or invalid \"method\"");
        return req;
    }
    req.method = method.toString();

    if (obj.contains(QStringLiteral("id")))
        req.id = obj.value(QStringLiteral("id"));
    else
        req.isNotification = true;

    const QJsonValue params = obj.value(QStringLiteral("params"));
    if (!params.isUndefined() && !params.isNull()) {
        if (!params.isObject()) {
            req.errorCode = InvalidRequest;
            req.errorMessage = QStringLiteral("\"params\" must be an object");
            return req;
        }
        req.params = params.toObject();
    }

    req.valid = true;
    return req;
}

QJsonObject makeResponse(const QJsonValue &id, const QJsonValue &result)
{
    return QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    };
}

QJsonObject makeError(const QJsonValue &id, int code, const QString &message)
{
    return QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
        {QStringLiteral("error"),
         QJsonObject{
             {QStringLiteral("code"), code},
             {QStringLiteral("message"), message},
         }},
    };
}

} // namespace JsonRpc
} // namespace QtMcp

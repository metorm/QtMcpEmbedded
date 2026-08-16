#ifndef QTMCP_JSONRPC_H
#define QTMCP_JSONRPC_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace QtMcp {
namespace JsonRpc {

const int ParseError = -32700;
const int InvalidRequest = -32600;
const int MethodNotFound = -32601;
const int InvalidParams = -32602;
const int InternalError = -32603;

struct Request {
    bool valid = false;
    bool isNotification = false;
    QString method;
    QJsonObject params;
    QJsonValue id;
    int errorCode = 0;
    QString errorMessage;
};

/// Parse a JSON document into a JSON-RPC 2.0 request. On failure, valid is
/// false and errorCode/errorMessage describe the problem.
Request parseRequest(const QJsonDocument &doc);

QJsonObject makeResponse(const QJsonValue &id, const QJsonValue &result);
QJsonObject makeError(const QJsonValue &id, int code, const QString &message);

} // namespace JsonRpc
} // namespace QtMcp

#endif // QTMCP_JSONRPC_H

#include "HttpServer.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace QtMcp {

namespace {
const int MAX_HEADER_SIZE = 64 * 1024;
const qint64 MAX_BODY_SIZE = 16 * 1024 * 1024;
} // namespace

QByteArray httpStatusText(int statusCode)
{
    switch (statusCode) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    default: return "Unknown";
    }
}

// ---------------------------------------------------------------- HttpConnection

HttpConnection::HttpConnection(QTcpSocket *socket, HttpServer *server)
    : QObject(socket), m_socket(socket), m_server(server)
{
}

void HttpConnection::respond(int statusCode, const QHash<QString, QString> &headers,
                             const QByteArray &body)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    HttpServer::ConnectionState &state = m_server->m_states[m_socket];

    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + httpStatusText(statusCode) + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        out += it.key().toLatin1() + ": " + it.value().toUtf8() + "\r\n";
    }
    if (state.closeAfterResponse)
        out += "Connection: close\r\n";
    else
        out += "Connection: keep-alive\r\n";
    out += "\r\n";
    out += body;

    m_socket->write(out);

    state.responsePending = false;
    if (state.closeAfterResponse)
        m_socket->disconnectFromHost();
    else
        m_server->tryParse(m_socket);
}

void HttpConnection::startStream(int statusCode, const QHash<QString, QString> &headers)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    HttpServer::ConnectionState &state = m_server->m_states[m_socket];

    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + httpStatusText(statusCode) + "\r\n";
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        out += it.key().toLatin1() + ": " + it.value().toUtf8() + "\r\n";
    }
    out += "Connection: keep-alive\r\n";
    out += "\r\n";
    m_socket->write(out);

    state.responsePending = false;
    state.streaming = true;
}

// ---------------------------------------------------------------- HttpServer

HttpServer::HttpServer(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

bool HttpServer::listen(const QHostAddress &address, quint16 port)
{
    return m_server->listen(address, port);
}

quint16 HttpServer::serverPort() const
{
    return m_server->serverPort();
}

void HttpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_states.insert(socket, ConnectionState());
        m_connections.insert(socket, new HttpConnection(socket, this));
        connect(socket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &HttpServer::onDisconnected);
    }
}

void HttpServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_states.contains(socket))
        return;
    m_states[socket].buffer.append(socket->readAll());
    tryParse(socket);
}

void HttpServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    m_states.remove(socket);
    m_connections.remove(socket);
    socket->deleteLater(); // also deletes the HttpConnection (child of socket)
}

void HttpServer::fail(QTcpSocket *socket, int statusCode, const QByteArray &message)
{
    HttpServer::ConnectionState &state = m_states[socket];
    state.closeAfterResponse = true;
    state.buffer.clear();
    HttpConnection *conn = m_connections.value(socket);
    if (conn) {
        QHash<QString, QString> headers;
        headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
        conn->respond(statusCode, headers,
                      QByteArray("{\"error\":\"") + message + QByteArray("\"}"));
    }
}

void HttpServer::tryParse(QTcpSocket *socket)
{
    ConnectionState &state = m_states[socket];
    if (state.responsePending || state.streaming)
        return;

    const int headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (state.buffer.size() > MAX_HEADER_SIZE)
            fail(socket, 431, "Request headers too large");
        return;
    }
    if (headerEnd + 4 > MAX_HEADER_SIZE) {
        fail(socket, 431, "Request headers too large");
        return;
    }

    const QList<QByteArray> lines = state.buffer.left(headerEnd).split('\n');
    if (lines.isEmpty()) {
        fail(socket, 400, "Malformed request");
        return;
    }

    // Request line: METHOD SP request-target SP HTTP-version
    const QList<QByteArray> requestLine = lines[0].trimmed().split(' ');
    if (requestLine.size() != 3) {
        fail(socket, 400, "Malformed request line");
        return;
    }

    HttpRequest request;
    request.method = QString::fromLatin1(requestLine[0]).toUpper();
    const QString target = QString::fromUtf8(requestLine[1]);
    request.path = target.section(QLatin1Char('?'), 0, 0);
    const QByteArray version = requestLine[2].trimmed();

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        if (line.isEmpty())
            continue;
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = QString::fromLatin1(line.left(colon).trimmed()).toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1).trimmed());
        request.headers.insert(key, value);
    }

    const QString transferEncoding = request.header(QStringLiteral("transfer-encoding"));
    if (!transferEncoding.isEmpty()
        && transferEncoding.compare(QStringLiteral("identity"), Qt::CaseInsensitive) != 0) {
        fail(socket, 501, "Chunked request bodies are not supported");
        return;
    }

    qint64 contentLength = 0;
    const QString contentLengthStr = request.header(QStringLiteral("content-length"));
    if (!contentLengthStr.isEmpty()) {
        bool ok = false;
        contentLength = contentLengthStr.toLongLong(&ok);
        if (!ok || contentLength < 0) {
            fail(socket, 400, "Invalid Content-Length");
            return;
        }
        if (contentLength > MAX_BODY_SIZE) {
            fail(socket, 413, "Request body too large");
            return;
        }
    }

    if (qint64(state.buffer.size()) < qint64(headerEnd) + 4 + contentLength)
        return; // body not fully arrived yet — wait for more data

    request.body = state.buffer.mid(headerEnd + 4, int(contentLength));
    state.buffer.remove(0, headerEnd + 4 + int(contentLength));

    const QString connectionHeader = request.header(QStringLiteral("connection")).toLower();
    if (connectionHeader == QStringLiteral("close"))
        state.closeAfterResponse = true;
    else if (version == "HTTP/1.0" && connectionHeader != QStringLiteral("keep-alive"))
        state.closeAfterResponse = true;
    else
        state.closeAfterResponse = false;

    state.responsePending = true;
    emit requestReceived(request, m_connections.value(socket));
}

} // namespace QtMcp

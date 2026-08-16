#ifndef QTMCP_HTTPSERVER_H
#define QTMCP_HTTPSERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QHostAddress;
class QTcpServer;
class QTcpSocket;

namespace QtMcp {

class HttpServer;

/// A fully parsed HTTP/1.1 request. Header keys are stored lower-cased.
struct HttpRequest {
    QString method;
    QString path;
    QHash<QString, QString> headers;
    QByteArray body;

    QString header(const QString &name) const { return headers.value(name.toLower()); }
};

/// Handle to one client connection, used to write the response.
/// Owned by HttpServer; invalid once the socket disconnects.
class HttpConnection : public QObject
{
    Q_OBJECT
public:
    /// Write a complete response (Content-Length is computed). Honors
    /// keep-alive: the connection stays open unless the client asked to close.
    void respond(int statusCode, const QHash<QString, QString> &headers, const QByteArray &body);

    /// Write response headers without Content-Length and switch the connection
    /// to streaming mode (used for SSE). Further bytes are written by SseStream.
    void startStream(int statusCode, const QHash<QString, QString> &headers);

    QTcpSocket *socket() const { return m_socket; }

private:
    friend class HttpServer;
    HttpConnection(QTcpSocket *socket, HttpServer *server);

    QTcpSocket *m_socket;
    HttpServer *m_server;
};

/// Minimal HTTP/1.1 server on top of QTcpServer.
///
/// Supported subset (MCP Streamable HTTP requirements only):
///  - request line + headers (case-insensitive), header block capped at 64 KB (431)
///  - Content-Length request bodies only, capped at 16 MB (413);
///    chunked request bodies are rejected (501)
///  - keep-alive, no pipelining (one in-flight request per connection)
class HttpServer : public QObject
{
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);

    bool listen(const QHostAddress &address, quint16 port);
    quint16 serverPort() const;

signals:
    void requestReceived(const QtMcp::HttpRequest &request, QtMcp::HttpConnection *connection);

private:
    friend class HttpConnection;

    struct ConnectionState {
        QByteArray buffer;
        bool responsePending = false;
        bool closeAfterResponse = false;
        bool streaming = false;
    };

    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void tryParse(QTcpSocket *socket);
    void fail(QTcpSocket *socket, int statusCode, const QByteArray &message);

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, ConnectionState> m_states;
    QHash<QTcpSocket *, HttpConnection *> m_connections;
};

QByteArray httpStatusText(int statusCode);

} // namespace QtMcp

#endif // QTMCP_HTTPSERVER_H

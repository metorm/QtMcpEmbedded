#ifndef QTMCP_SSESTREAM_H
#define QTMCP_SSESTREAM_H

#include <QObject>

class QTimer;
class QTcpSocket;

namespace QtMcp {

/// Server-Sent Events writer bound to one streaming HTTP connection.
/// Currently only emits heartbeat comment lines; sendEvent() is available for
/// future server-to-client notifications. Destroyed with the socket.
class SseStream : public QObject
{
    Q_OBJECT
public:
    explicit SseStream(QTcpSocket *socket);

    void sendEvent(const QString &event, const QByteArray &data);

private:
    void writeRaw(const QByteArray &bytes);

    QTcpSocket *m_socket;
    QTimer *m_heartbeatTimer;
};

} // namespace QtMcp

#endif // QTMCP_SSESTREAM_H

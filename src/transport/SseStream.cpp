#include "SseStream.h"

#include <QTcpSocket>
#include <QTimer>

namespace QtMcp {

namespace {
const int HEARTBEAT_INTERVAL_MS = 15000;
}

SseStream::SseStream(QTcpSocket *socket)
    : QObject(socket), m_socket(socket), m_heartbeatTimer(new QTimer(this))
{
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() { writeRaw(": heartbeat\n\n"); });
    m_heartbeatTimer->start(HEARTBEAT_INTERVAL_MS);
}

void SseStream::sendEvent(const QString &event, const QByteArray &data)
{
    QByteArray out;
    if (!event.isEmpty())
        out += "event: " + event.toUtf8() + '\n';
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines)
        out += "data: " + line + '\n';
    out += '\n';
    writeRaw(out);
}

void SseStream::writeRaw(const QByteArray &bytes)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(bytes);
}

} // namespace QtMcp

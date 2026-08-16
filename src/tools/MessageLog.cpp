#include "MessageLog.h"

#include <QDateTime>
#include <QJsonArray>

#include "../protocol/ToolRegistry.h"

namespace QtMcp {

namespace {

int severityOf(const QString &level)
{
    if (level == QLatin1String("debug")) return 0;
    if (level == QLatin1String("info")) return 1;
    if (level == QLatin1String("warning")) return 2;
    if (level == QLatin1String("critical")) return 3;
    return 1;
}

} // namespace

MessageLog &MessageLog::instance()
{
    static MessageLog log;
    return log;
}

void MessageLog::install()
{
    if (m_installed)
        return;
    m_previous = qInstallMessageHandler(&MessageLog::messageHandler);
    m_installed = true;
}

void MessageLog::registerTools(ToolRegistry &registry)
{
    const QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("level"),
              QJsonObject{
                  {QStringLiteral("type"), QStringLiteral("string")},
                  {QStringLiteral("enum"),
                   QJsonArray{QStringLiteral("debug"), QStringLiteral("info"),
                              QStringLiteral("warning"), QStringLiteral("critical")}},
                  {QStringLiteral("default"), QStringLiteral("info")},
                  {QStringLiteral("description"),
                   QStringLiteral("Minimum severity; each level includes more severe levels.")},
              }},
         }},
    };

    registry.registerTool(
        QStringLiteral("qt_messages"),
        QStringLiteral("Return Qt internal messages (qDebug/qWarning/...) captured since the "
                       "last call, at or above the given level. The buffer is cleared on read."),
        schema, [](const QJsonObject &args) {
            const QString level = args.value(QStringLiteral("level")).toString(QStringLiteral("info"));
            return ToolResult::fromData(MessageLog::instance().takeMessages(level));
        });
}

QJsonObject MessageLog::takeMessages(const QString &level)
{
    const int minSeverity = severityOf(level);

    QJsonArray messages;
    {
        QMutexLocker locker(&m_mutex);
        for (const Entry &entry : m_buffer) {
            if (severityOf(entry.level) < minSeverity)
                continue;
            messages.append(QJsonObject{
                {QStringLiteral("level"), entry.level},
                {QStringLiteral("message"), entry.message},
                {QStringLiteral("timestamp"), entry.timestamp},
            });
        }
        m_buffer.clear();
    }

    return QJsonObject{
        {QStringLiteral("messages"), messages},
        {QStringLiteral("count"), messages.size()},
    };
}

void MessageLog::messageHandler(QtMsgType type, const QMessageLogContext &context,
                                const QString &message)
{
    MessageLog &self = instance();

    QString level;
    switch (type) {
    case QtDebugMsg: level = QStringLiteral("debug"); break;
    case QtInfoMsg: level = QStringLiteral("info"); break;
    case QtWarningMsg: level = QStringLiteral("warning"); break;
    case QtCriticalMsg: level = QStringLiteral("critical"); break;
    case QtFatalMsg: level = QStringLiteral("critical"); break;
    }
    self.append(level, message);

    if (self.m_previous)
        self.m_previous(type, context, message);
    else {
        // No previous handler: keep messages visible on stderr ourselves.
        fprintf(stderr, "%s\n", qPrintable(message));
        if (type == QtFatalMsg)
            abort();
    }
}

void MessageLog::append(const QString &level, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    if (int(m_buffer.size()) >= MAX_ENTRIES)
        m_buffer.pop_front();
    m_buffer.push_back(Entry{level, message,
                             QDateTime::currentMSecsSinceEpoch() / 1000.0});
}

} // namespace QtMcp

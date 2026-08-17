#include "QtMcp.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>

#include "core/ProbeServer.h"
#include "tools/HostLog.h"

namespace QtMcp {

namespace {
const int DEFAULT_PORT = 9142;
const char PROBE_OBJECT_NAME[] = "qt_mcp_probe";
}

void postMessage(const QString &message, const QString &level)
{
    HostLog::instance().post(level, message);
}

bool install()
{
    return install(InstallOptions{});
}

bool install(const InstallOptions &options)
{
    if (qEnvironmentVariable("QT_MCP_PROBE") != QLatin1String("1"))
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
    if (!server->start(QHostAddress(host), port)) {
        qWarning("QtMcp: failed to listen on %s:%d", qPrintable(host), int(port));
        delete server;
        return false;
    }

    qInfo("QtMcp: MCP server listening on http://%s:%d/mcp", qPrintable(host), int(port));
    return true;
}

} // namespace QtMcp

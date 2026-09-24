#include "QtMcpPlugin.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

#include "QtMcp.h"

namespace QtMcp {

QtMcpPlugin::QtMcpPlugin(QObject *parent)
    : QGenericPlugin(parent)
{
}

QObject *QtMcpPlugin::create(const QString &key, const QString &specification)
{
    if (key.compare(QLatin1String("qtmcp"), Qt::CaseInsensitive) != 0) {
        return nullptr;
    }

    // 1. Check for independent config file (QT_MCP_CONFIG env var, or qtmcp.ini next to exe / in cwd)
    bool configFound = false;
    QString configPath = qEnvironmentVariable("QT_MCP_CONFIG");
    if (configPath.isEmpty() || !QFileInfo::exists(configPath)) {
        const QString appDirIni = QCoreApplication::applicationDirPath() + QLatin1String("/qtmcp.ini");
        if (QFileInfo::exists(appDirIni)) {
            configPath = appDirIni;
        } else if (QFileInfo::exists(QStringLiteral("qtmcp.ini"))) {
            configPath = QStringLiteral("qtmcp.ini");
        }
    }

    if (!configPath.isEmpty() && QFileInfo::exists(configPath)) {
        configFound = true;
        QSettings settings(configPath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("QtMcp"));

        const QVariant enabledVar = settings.value(QStringLiteral("enabled"));
        if (enabledVar.isValid() && !enabledVar.toBool()) {
            qInfo("QtMcp: disabled by configuration file '%s'.", qPrintable(configPath));
            return nullptr;
        }

        const QString iniHost = settings.value(QStringLiteral("host")).toString();
        if (!iniHost.isEmpty() && qEnvironmentVariableIsEmpty("QT_MCP_HOST")) {
            qputenv("QT_MCP_HOST", iniHost.toLatin1());
        }

        const QString iniPort = settings.value(QStringLiteral("port")).toString();
        if (!iniPort.isEmpty() && qEnvironmentVariableIsEmpty("QT_MCP_PORT")) {
            qputenv("QT_MCP_PORT", iniPort.toLatin1());
        }

        const QString iniAppName = settings.value(QStringLiteral("appName")).toString();
        if (!iniAppName.isEmpty() && qEnvironmentVariableIsEmpty("QT_MCP_APP_NAME")) {
            qputenv("QT_MCP_APP_NAME", iniAppName.toUtf8());
        }

        const QString iniInstructions = settings.value(QStringLiteral("instructions")).toString();
        if (!iniInstructions.isEmpty() && qEnvironmentVariableIsEmpty("QT_MCP_INSTRUCTIONS")) {
            qputenv("QT_MCP_INSTRUCTIONS", iniInstructions.toUtf8());
        }

        settings.endGroup();
        qInfo("QtMcp: loaded configuration from '%s'.", qPrintable(configPath));
    }

    // 2. Parse options from specification (highest priority), e.g.
    // QT_QPA_GENERIC_PLUGINS="qtmcp:port=9145,host=127.0.0.1"
    if (!specification.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const auto parts = specification.split(QLatin1Char(','), Qt::SkipEmptyParts);
#else
        const auto parts = specification.split(QLatin1Char(','), QString::SkipEmptyParts);
#endif
        for (const QString &part : parts) {
            const int eq = part.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString optKey = part.left(eq).trimmed();
            const QString optVal = part.mid(eq + 1).trimmed();

            if (optKey.compare(QLatin1String("port"), Qt::CaseInsensitive) == 0) {
                qputenv("QT_MCP_PORT", optVal.toLatin1());
            } else if (optKey.compare(QLatin1String("host"), Qt::CaseInsensitive) == 0) {
                qputenv("QT_MCP_HOST", optVal.toLatin1());
            } else if (optKey.compare(QLatin1String("appName"), Qt::CaseInsensitive) == 0 ||
                       optKey.compare(QLatin1String("name"), Qt::CaseInsensitive) == 0) {
                qputenv("QT_MCP_APP_NAME", optVal.toUtf8());
            } else if (optKey.compare(QLatin1String("instructions"), Qt::CaseInsensitive) == 0) {
                qputenv("QT_MCP_INSTRUCTIONS", optVal.toUtf8());
            }
        }
    }

    // 3. Activation check:
    // To prevent interfering with every Qt application when QT_QPA_GENERIC_PLUGINS=qtmcp
    // is set globally, stay completely dormant unless:
    //   a) An independent config file (qtmcp.ini) is present in the app dir or cwd (and not enabled=false).
    //   b) OR QT_MCP_PROBE=1 is explicitly set in environment.
    //   c) OR explicit arguments were passed via specification (e.g. -plugin qtmcp:port=...).
    const bool probeEnvExplicit = (qEnvironmentVariable("QT_MCP_PROBE") == QLatin1String("1"));
    const bool specExplicit = !specification.isEmpty();

    if (!configFound && !probeEnvExplicit && !specExplicit) {
        // Zero-overhead dormancy: no config file and no explicit request.
        return nullptr;
    }

    qputenv("QT_MCP_PROBE", "1");

    if (!install()) {
        qWarning("QtMcp: plugin failed to install probe.");
        return nullptr;
    }

    qInfo("QtMcp: generic plugin loaded and probe active.");
    return new QObject(QCoreApplication::instance());
}

} // namespace QtMcp

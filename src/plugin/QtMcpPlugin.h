#ifndef QTMCP_PLUGIN_H
#define QTMCP_PLUGIN_H

#include <QtGui/qgenericplugin.h>

namespace QtMcp {

class QtMcpPlugin : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "qtmcp.json")

public:
    explicit QtMcpPlugin(QObject *parent = nullptr);
    ~QtMcpPlugin() override = default;

    QObject *create(const QString &key, const QString &specification) override;
};

} // namespace QtMcp

#endif // QTMCP_PLUGIN_H

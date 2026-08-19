#include "ToolRegistry.h"

#include "../core/ToolError.h"

namespace QtMcp {

void ToolRegistry::registerTool(const QString &name, const QString &description,
                                const QJsonObject &inputSchema, Handler handler)
{
    Tool tool;
    tool.description = description;
    tool.schema = inputSchema;
    tool.handler = std::move(handler);
    if (!m_tools.contains(name))
        m_order.append(name);
    m_tools.insert(name, tool);
}

bool ToolRegistry::unregister(const QString &name)
{
    if (!m_tools.contains(name))
        return false;
    m_tools.remove(name);
    m_order.removeAll(name);
    return true;
}

QJsonArray ToolRegistry::toolList() const
{
    QJsonArray list;
    for (const QString &name : m_order) {
        const Tool &tool = m_tools[name];
        list.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("description"), tool.description},
            {QStringLiteral("inputSchema"), tool.schema},
        });
    }
    return list;
}

ToolResult ToolRegistry::call(const QString &name, const QJsonObject &arguments) const
{
    const auto it = m_tools.constFind(name);
    if (it == m_tools.constEnd())
        throw ToolError(QStringLiteral("Unknown tool: %1").arg(name));
    return it->handler(arguments);
}

} // namespace QtMcp

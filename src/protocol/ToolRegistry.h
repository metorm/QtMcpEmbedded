#ifndef QTMCP_TOOLREGISTRY_H
#define QTMCP_TOOLREGISTRY_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace QtMcp {

/// Result of a tool invocation. By default `data` is serialized to a compact
/// JSON string and wrapped into a text content item. Image results (screenshots)
/// become an image content item plus an optional caption text item.
struct ToolResult {
    QJsonObject data;
    bool isImage = false;
    QByteArray imageBase64;
    QString mimeType;
    QString imageText;
    /// Maps to MCP's isError on the tool result. Use for partial/overall
    /// failures that still carry useful payload (e.g. a stopped batch).
    bool isError = false;

    static ToolResult fromData(const QJsonObject &d)
    {
        ToolResult r;
        r.data = d;
        return r;
    }
};

/// Registry of MCP tools: name + description + JSON Schema + handler.
/// Handlers throw ToolError for user-facing failures.
class ToolRegistry
{
public:
    using Handler = std::function<ToolResult(const QJsonObject &arguments)>;

    void registerTool(const QString &name, const QString &description,
                      const QJsonObject &inputSchema, Handler handler);

    bool hasTool(const QString &name) const { return m_tools.contains(name); }

    /// Removes a tool. Returns false if no tool with that name exists.
    bool unregister(const QString &name);

    /// Array of {name, description, inputSchema} for tools/list.
    QJsonArray toolList() const;

    /// Invoke a tool. Throws ToolError on unknown tool or handler failure.
    ToolResult call(const QString &name, const QJsonObject &arguments) const;

private:
    struct Tool {
        QString description;
        QJsonObject schema;
        Handler handler;
    };

    QHash<QString, Tool> m_tools;
    QStringList m_order; // stable listing order
};

} // namespace QtMcp

#endif // QTMCP_TOOLREGISTRY_H

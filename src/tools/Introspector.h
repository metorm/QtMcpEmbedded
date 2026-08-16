#ifndef QTMCP_INTROSPECTOR_H
#define QTMCP_INTROSPECTOR_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

class QWidget;

namespace QtMcp {

class RefRegistry;
class ToolRegistry;

/// Widget tree traversal, property reading and snapshot generation.
/// A direct port of qt-mcp's introspector.py.
class Introspector
{
public:
    explicit Introspector(RefRegistry &registry);

    void registerTools(ToolRegistry &registry);

    QJsonObject snapshot(int maxDepth, const QString &rootRef, bool skipHidden);
    QJsonObject widgetDetails(const QString &ref);
    QJsonObject findWidget(const QString &pattern, const QString &className,
                           const QString &objectName, const QString &text,
                           const QString &rootRef, bool visibleOnly, int maxResults);
    QJsonObject listWindows(bool skipHidden);
    QJsonObject objectTree(const QString &rootRef, int maxDepth);
    QJsonObject activePopup();

private:
    int walkWidget(QWidget *widget, int depth, int maxDepth, QStringList &lines, bool skipHidden);
    int walkObject(QObject *obj, int depth, int maxDepth, QStringList &lines);

    RefRegistry &m_registry;
};

} // namespace QtMcp

#endif // QTMCP_INTROSPECTOR_H

#ifndef QTMCP_INTERACTOR_H
#define QTMCP_INTERACTOR_H

#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QStringList>

class QWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace QtMcp {

class RefRegistry;
class ToolRegistry;

/// Synthesizes Qt input events and mutates widget state.
/// A direct port of qt-mcp's interactor.py.
class Interactor
{
public:
    explicit Interactor(RefRegistry &registry);

    void registerTools(ToolRegistry &registry);

    QJsonObject click(const QString &ref, const QString &button,
                      const QStringList &modifiers, const QPoint &position, bool hasPosition,
                      bool force, bool doubleClick);
    QJsonObject typeText(const QString &ref, const QString &text, bool clearFirst,
                         bool useClipboard, bool force);
    QJsonObject keyPress(const QString &key, const QString &ref, bool force);
    QJsonObject setProperty(const QString &ref, const QString &propertyName,
                            const QJsonValue &value);
    QJsonObject invokeSlot(const QString &ref, const QString &methodName,
                           const QJsonArray &args);
    QJsonObject waitFor(const QString &condition, int timeoutMs, const QString &objectName,
                        const QString &ref, const QString &propertyName, const QJsonValue &value);
    QJsonObject getText(const QString &ref);
    QJsonObject triggerAction(const QString &ref, const QString &actionText,
                              bool hasActionIndex, int actionIndex);

    /// Process Qt events for the given number of milliseconds (GUI thread).
    static void processEventsFor(int ms);

private:
    QWidget *resolveWidget(const QString &ref);
    /// Fail fast when a widget cannot meaningfully receive input: hidden,
    /// disabled, or blocked by a modal window. `force` skips all checks.
    void ensureInteractable(QWidget *widget, const QString &ref, bool force);
    /// Click a QTreeWidgetItem (refs "i...") inside its tree's viewport.
    QJsonObject clickTreeItem(const QString &ref, QTreeWidget *tree, QTreeWidgetItem *item,
                              const QString &button, const QStringList &modifiers,
                              bool force, bool doubleClick);
    /// Pseudo-properties on QTreeWidgetItem refs: expanded/checked/selected/text.
    QJsonObject setTreeItemProperty(const QString &ref, QTreeWidgetItem *item,
                                    const QString &propertyName, const QJsonValue &value);

    RefRegistry &m_registry;
};

} // namespace QtMcp

#endif // QTMCP_INTERACTOR_H

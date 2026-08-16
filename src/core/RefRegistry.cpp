#include "RefRegistry.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "ToolError.h"

namespace QtMcp {

void RefRegistry::sweep()
{
    ++m_generation;

    for (auto it = m_refs.begin(); it != m_refs.end();) {
        if (it.value().isNull())
            it = m_refs.erase(it);
        else
            ++it;
    }
    for (auto it = m_reverse.begin(); it != m_reverse.end();) {
        if (!it.key() || !m_refs.contains(it.value()))
            it = m_reverse.erase(it);
        else
            ++it;
    }
    for (auto it = m_itemRefs.begin(); it != m_itemRefs.end();) {
        if (it->tree.isNull())
            it = m_itemRefs.erase(it);
        else
            ++it;
    }
}

QString RefRegistry::registerObject(QObject *obj, const QString &prefix)
{
    if (!obj)
        return QString();

    const auto it = m_reverse.constFind(obj);
    if (it != m_reverse.constEnd())
        return it.value();

    const QString ref = prefix + QString::number(++m_counter);
    m_refs.insert(ref, QPointer<QObject>(obj));
    m_reverse.insert(obj, ref);
    return ref;
}

QString RefRegistry::registerTreeItem(QTreeWidgetItem *item, QTreeWidget *tree)
{
    if (!item || !tree)
        return QString();

    TreeItemEntry entry;
    entry.tree = tree;
    QTreeWidgetItem *cur = item;
    while (cur) {
        QTreeWidgetItem *parent = cur->parent();
        if (parent)
            entry.path.prepend(parent->indexOfChild(cur));
        else
            entry.path.prepend(tree->indexOfTopLevelItem(cur));
        cur = parent;
    }

    const QString ref = QStringLiteral("i") + QString::number(++m_counter);
    m_itemRefs.insert(ref, entry);
    return ref;
}

QObject *RefRegistry::resolve(const QString &ref) const
{
    const auto it = m_refs.constFind(ref);
    if (it == m_refs.constEnd())
        return nullptr;
    return it.value().data(); // QPointer: nullptr if the object was deleted
}

QObject *RefRegistry::resolveOrThrow(const QString &ref) const
{
    QObject *obj = resolve(ref);
    if (!obj) {
        throw ToolError(QStringLiteral("Ref %1 not found — unknown ref, or the widget was "
                                       "destroyed. Use qt_find_widget or qt_snapshot to look "
                                       "up current refs.").arg(ref));
    }
    return obj;
}

QTreeWidgetItem *RefRegistry::resolveTreeItem(const QString &ref) const
{
    const auto it = m_itemRefs.constFind(ref);
    if (it == m_itemRefs.constEnd() || !it->tree)
        return nullptr;

    QTreeWidgetItem *item = nullptr;
    const QList<int> &path = it->path;
    for (int i = 0; i < path.size(); ++i) {
        if (i == 0) {
            if (path[i] < 0 || path[i] >= it->tree->topLevelItemCount())
                return nullptr;
            item = it->tree->topLevelItem(path[i]);
        } else {
            if (!item || path[i] < 0 || path[i] >= item->childCount())
                return nullptr;
            item = item->child(path[i]);
        }
    }
    return item;
}

bool RefRegistry::resolveTreeItemRef(const QString &ref, QTreeWidget **tree,
                                     QTreeWidgetItem **item) const
{
    const auto it = m_itemRefs.constFind(ref);
    if (it == m_itemRefs.constEnd() || it->tree.isNull())
        return false;
    QTreeWidgetItem *resolved = resolveTreeItem(ref);
    if (!resolved)
        return false;
    if (tree)
        *tree = it->tree.data();
    if (item)
        *item = resolved;
    return true;
}

} // namespace QtMcp

#ifndef QTMCP_REFREGISTRY_H
#define QTMCP_REFREGISTRY_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;

namespace QtMcp {

/// Ephemeral mapping from string refs ("w1", "w2", "i1", ...) to live objects.
/// QObjects are held through QPointer so a deleted widget resolves to nullptr
/// instead of a dangling pointer. Refs are NEVER reused: the counter is
/// monotonic, so a ref obtained from any earlier snapshot either still points
/// to the same live widget or fails resolve() outright — it can never be
/// silently rebound to a different widget. Full-tree snapshots call sweep()
/// to purge dead entries; the generation counter is informational only.
class RefRegistry
{
public:
    RefRegistry() = default;

    int generation() const { return m_generation; }
    int size() const { return m_refs.size(); }

    /// Purge entries whose objects were deleted; bump the generation counter.
    /// Live refs stay valid and stable across sweeps.
    void sweep();

    QString registerObject(QObject *obj, const QString &prefix = QStringLiteral("w"));
    QString registerTreeItem(QTreeWidgetItem *item, QTreeWidget *tree);

    QObject *resolve(const QString &ref) const;
    QObject *resolveOrThrow(const QString &ref) const;
    QTreeWidgetItem *resolveTreeItem(const QString &ref) const;

    /// Resolve an item ref ("i...") to its tree and item. Returns false when
    /// the ref is not an item ref or the item no longer exists.
    bool resolveTreeItemRef(const QString &ref, QTreeWidget **tree,
                            QTreeWidgetItem **item) const;

private:
    struct TreeItemEntry {
        QPointer<QTreeWidget> tree;
        QList<int> path;
    };

    int m_counter = 0;
    int m_generation = 0;
    QHash<QString, QPointer<QObject>> m_refs;
    QHash<QObject *, QString> m_reverse;
    QHash<QString, TreeItemEntry> m_itemRefs;
};

} // namespace QtMcp

#endif // QTMCP_REFREGISTRY_H

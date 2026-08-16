#include "Introspector.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLayout>
#include <QListWidget>
#include <QMetaProperty>
#include <QRect>
#include <QSet>
#include <QSizePolicy>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QWidget>

#include <functional>

#include "../core/RefRegistry.h"
#include "../core/ToolError.h"
#include "../protocol/ToolRegistry.h"

namespace QtMcp {

namespace {

const int MAX_PROPERTY_LENGTH = 500;

QString truncate(const QString &value, int limit = MAX_PROPERTY_LENGTH)
{
    if (value.size() > limit) {
        return value.left(limit)
               + QStringLiteral(" [...%1 more chars]").arg(value.size() - limit);
    }
    return value;
}

/// Invoke a no-arg method returning QString (e.g. "text()") via the meta-object
/// system. Returns false when the method does not exist or has another shape.
bool invokeStringMethod(QObject *obj, const char *signature, QString *out)
{
    const int index = obj->metaObject()->indexOfMethod(signature);
    if (index < 0)
        return false;
    const QMetaMethod method = obj->metaObject()->method(index);
    if (method.parameterCount() != 0 || method.returnType() != QMetaType::QString)
        return false;
    return method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(QString, *out));
}

bool invokeBoolMethod(QObject *obj, const char *signature, bool *out)
{
    const int index = obj->metaObject()->indexOfMethod(signature);
    if (index < 0)
        return false;
    const QMetaMethod method = obj->metaObject()->method(index);
    if (method.parameterCount() != 0 || method.returnType() != QMetaType::Bool)
        return false;
    return method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(bool, *out));
}

/// Counterpart of Python's `_safe_text`: read text() when available, falling
/// back to the "text" property (QLabel/QLineEdit/QAbstractButton expose text
/// as a property, not as an invokable method).
QString safeText(QWidget *widget)
{
    QString text;
    if (invokeStringMethod(widget, "text()", &text) && !text.isEmpty())
        return text;
    const QMetaObject *meta = widget->metaObject();
    const int index = meta->indexOfProperty("text");
    if (index >= 0 && meta->property(index).userType() == QMetaType::QString)
        return widget->property("text").toString();
    return QString();
}

QJsonObject geometryJson(const QRect &g)
{
    return QJsonObject{
        {QStringLiteral("x"), g.x()},
        {QStringLiteral("y"), g.y()},
        {QStringLiteral("width"), g.width()},
        {QStringLiteral("height"), g.height()},
    };
}

int variantTypeId(const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return v.typeId();
#else
    return v.userType();
#endif
}

/// QVariant -> JSON for property dumps. Plain scalars stay scalars, geometry
/// types become objects, everything else falls back to a (truncated) string.
QJsonValue propertyValueToJson(const QVariant &value)
{
    switch (variantTypeId(value)) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return value.toDouble();
    case QMetaType::QString:
        return truncate(value.toString());
    case QMetaType::QStringList: {
        QJsonArray arr;
        const QStringList list = value.toStringList();
        for (const QString &s : list)
            arr.append(truncate(s));
        return arr;
    }
    case QMetaType::QPoint: {
        const QPoint p = value.toPoint();
        return QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
    }
    case QMetaType::QPointF: {
        const QPointF p = value.toPointF();
        return QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
    }
    case QMetaType::QSize: {
        const QSize s = value.toSize();
        return QJsonObject{{QStringLiteral("width"), s.width()}, {QStringLiteral("height"), s.height()}};
    }
    case QMetaType::QRect: {
        const QRect r = value.toRect();
        return geometryJson(r);
    }
    default:
        break;
    }

    QString str = value.toString();
    if (str.isEmpty())
        str = QStringLiteral("<%1>").arg(QString::fromLatin1(value.typeName()));
    return truncate(str);
}

/// All QMetaObject properties except the ones already shown in headers.
QJsonObject readProperties(QObject *obj)
{
    static const QSet<QString> skip = {
        QStringLiteral("objectName"), QStringLiteral("visible"),
        QStringLiteral("enabled"), QStringLiteral("geometry"),
    };

    QJsonObject props;
    const QMetaObject *meta = obj->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QMetaProperty prop = meta->property(i);
        const QString name = QString::fromLatin1(prop.name());
        if (skip.contains(name))
            continue;
        const QVariant value = obj->property(prop.name());
        if (!value.isValid())
            continue;
        props.insert(name, propertyValueToJson(value));
    }
    return props;
}

void readTreeWidgetItems(QTreeWidgetItem *item, QTreeWidget *tree, int depth,
                         RefRegistry &registry, QJsonArray *items)
{
    const QString ref = registry.registerTreeItem(item, tree);
    const QRect visualRect = tree->visualItemRect(item);
    items->append(QJsonObject{
        {QStringLiteral("ref"), ref},
        {QStringLiteral("text"), item->text(0)},
        {QStringLiteral("depth"), depth},
        {QStringLiteral("expanded"), item->isExpanded()},
        {QStringLiteral("children"), item->childCount()},
        {QStringLiteral("checkable"), bool(item->flags() & Qt::ItemIsUserCheckable)},
        {QStringLiteral("checked"), item->checkState(0) == Qt::Checked},
        {QStringLiteral("click_x"), visualRect.center().x()},
        {QStringLiteral("click_y"), visualRect.center().y()},
    });
    for (int i = 0; i < item->childCount(); ++i)
        readTreeWidgetItems(item->child(i), tree, depth + 1, registry, items);
}

QJsonArray treeWidgetItems(QTreeWidget *tree, RefRegistry &registry)
{
    QJsonArray items;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        readTreeWidgetItems(tree->topLevelItem(i), tree, 0, registry, &items);
    return items;
}

QString sizePolicyName(QSizePolicy::Policy policy)
{
    switch (policy) {
    case QSizePolicy::Fixed: return QStringLiteral("Fixed");
    case QSizePolicy::Minimum: return QStringLiteral("Minimum");
    case QSizePolicy::Maximum: return QStringLiteral("Maximum");
    case QSizePolicy::Preferred: return QStringLiteral("Preferred");
    case QSizePolicy::Expanding: return QStringLiteral("Expanding");
    case QSizePolicy::MinimumExpanding: return QStringLiteral("MinimumExpanding");
    case QSizePolicy::Ignored: return QStringLiteral("Ignored");
    }
    return QStringLiteral("Unknown");
}

QWidgetList visibleTopLevelWidgets(QApplication *app)
{
    QWidgetList result;
    const QWidgetList topLevels = app->topLevelWidgets();
    for (QWidget *w : topLevels) {
        if (w->isVisible())
            result.append(w);
    }
    return result;
}

} // namespace

Introspector::Introspector(RefRegistry &registry)
    : m_registry(registry)
{
}

// ------------------------------------------------------------------ snapshot

QJsonObject Introspector::snapshot(int maxDepth, const QString &rootRef, bool skipHidden)
{
    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) {
        return QJsonObject{
            {QStringLiteral("tree"), QStringLiteral("(no QApplication)")},
            {QStringLiteral("widget_count"), 0},
            {QStringLiteral("generation"), 0},
        };
    }

    if (!rootRef.isEmpty()) {
        // Subtree snapshot: don't clear the registry.
        QObject *obj = m_registry.resolveOrThrow(rootRef);
        QWidget *root = qobject_cast<QWidget *>(obj);
        if (!root)
            throw ToolError(QStringLiteral("Ref %1 is not a QWidget").arg(rootRef));
        QStringList lines;
        const int count = walkWidget(root, 0, maxDepth, lines, skipHidden);
        return QJsonObject{
            {QStringLiteral("tree"), lines.join(QLatin1Char('\n'))},
            {QStringLiteral("widget_count"), count},
            {QStringLiteral("generation"), m_registry.generation()},
        };
    }

    m_registry.sweep();
    QStringList lines;
    int count = 0;

    QWidgetList windows = visibleTopLevelWidgets(app);

    // Retry once if no visible windows during a transition.
    if (windows.isEmpty()) {
        app->processEvents();
        QThread::msleep(50);
        app->processEvents();
        windows = visibleTopLevelWidgets(app);
    }

    for (QWidget *window : qAsConst(windows))
        count += walkWidget(window, 0, maxDepth, lines, skipHidden);

    // Include the active popup if it is not already a top-level widget.
    if (QWidget *popup = app->activePopupWidget()) {
        if (popup->isVisible() && !windows.contains(popup))
            count += walkWidget(popup, 0, maxDepth, lines, skipHidden);
    }

    return QJsonObject{
        {QStringLiteral("tree"), lines.join(QLatin1Char('\n'))},
        {QStringLiteral("widget_count"), count},
        {QStringLiteral("generation"), m_registry.generation()},
    };
}

int Introspector::walkWidget(QWidget *widget, int depth, int maxDepth, QStringList &lines,
                             bool skipHidden)
{
    if (depth > maxDepth)
        return 0;

    // Collapse Qt-internal helper widgets (scroll area viewports/containers
    // follow the "qt_" objectName convention): no semantic value for an
    // agent, but their children (the actual content) still get walked.
    if (widget->objectName().startsWith(QStringLiteral("qt_"))) {
        int count = 0;
        const QObjectList children = widget->children();
        for (QObject *child : children) {
            if (QWidget *childWidget = qobject_cast<QWidget *>(child))
                count += walkWidget(childWidget, depth, maxDepth, lines, skipHidden);
        }
        return count;
    }

    const QString ref = m_registry.registerObject(widget, QStringLiteral("w"));
    const QString cls = QString::fromLatin1(widget->metaObject()->className());
    const QString name = widget->objectName();
    const QString indent = QString(depth * 2, QLatin1Char(' '));

    QStringList parts;
    parts << QStringLiteral("%1- %2").arg(indent, cls);
    if (!name.isEmpty())
        parts << QStringLiteral("\"%1\"").arg(name);
    parts << QStringLiteral("[ref=%1]").arg(ref);

    const QString text = safeText(widget);
    if (!text.isEmpty())
        parts << QStringLiteral("\"%1\"").arg(text);

    const bool hidden = !widget->isVisible();
    if (hidden) {
        // Geometry of hidden widgets is a meaningless placeholder — omit it.
        parts << QStringLiteral("[hidden]");
    } else {
        const QRect g = widget->geometry();
        parts << QStringLiteral("[%1x%2]").arg(g.width()).arg(g.height());
    }
    if (!widget->isEnabled())
        parts << QStringLiteral("[disabled]");

    // Checked state: "checked" Q_PROPERTY (QAbstractButton) or isChecked().
    const QMetaObject *meta = widget->metaObject();
    const int checkedProp = meta->indexOfProperty("checked");
    bool checked = false;
    bool hasChecked = false;
    if (checkedProp >= 0 && meta->property(checkedProp).userType() == QMetaType::Bool) {
        checked = widget->property("checked").toBool();
        hasChecked = true;
    } else if (invokeBoolMethod(widget, "isChecked()", &checked)) {
        hasChecked = true;
    }
    if (hasChecked && checked)
        parts << QStringLiteral("[checked]");

    // Tooltip: auxiliary info for both humans and AI agents. Emitted only
    // when non-empty so tooltip-free apps pay no token cost.
    const QString tip = widget->toolTip();
    if (!tip.isEmpty())
        parts << QStringLiteral("[tip: %1]").arg(truncate(tip));

    // Combo boxes: current value and options are essential agent context.
    if (QComboBox *combo = qobject_cast<QComboBox *>(widget)) {
        parts << QStringLiteral("[current: %1]").arg(truncate(combo->currentText()));
        QStringList options;
        for (int i = 0; i < combo->count(); ++i)
            options << combo->itemText(i);
        if (!options.isEmpty())
            parts << QStringLiteral("[items: %1]").arg(options.join(QStringLiteral(" | ")));
    }

    if (QTabWidget *tabWidget = qobject_cast<QTabWidget *>(widget)) {
        QStringList labels;
        for (int i = 0; i < tabWidget->count(); ++i) {
            const QString marker = i == tabWidget->currentIndex() ? QStringLiteral("*") : QString();
            labels << marker + tabWidget->tabText(i);
        }
        if (!labels.isEmpty())
            parts << QStringLiteral("[tabs: %1]").arg(labels.join(QStringLiteral(" | ")));
    } else if (QTabBar *tabBar = qobject_cast<QTabBar *>(widget)) {
        QStringList labels;
        for (int i = 0; i < tabBar->count(); ++i) {
            const QString marker = i == tabBar->currentIndex() ? QStringLiteral("*") : QString();
            labels << marker + tabBar->tabText(i);
        }
        if (!labels.isEmpty())
            parts << QStringLiteral("[tabs: %1]").arg(labels.join(QStringLiteral(" | ")));
    }

    lines.append(parts.join(QLatin1Char(' ')));
    int count = 1;

    // Skip children of hidden containers when requested.
    if (skipHidden && hidden)
        return count;

    // Inline tree widget items.
    if (QTreeWidget *tree = qobject_cast<QTreeWidget *>(widget)) {
        const QJsonArray items = treeWidgetItems(tree, m_registry);
        for (const QJsonValue &v : items) {
            const QJsonObject item = v.toObject();
            const QString itemIndent((depth + 1 + item.value(QStringLiteral("depth")).toInt()) * 2,
                                     QLatin1Char(' '));
            const QString expanded = item.value(QStringLiteral("expanded")).toBool()
                                         ? QStringLiteral(" [expanded]")
                                         : QString();
            QString checkState;
            if (item.value(QStringLiteral("checkable")).toBool()) {
                checkState = item.value(QStringLiteral("checked")).toBool()
                                 ? QStringLiteral(" [checked]")
                                 : QStringLiteral(" [unchecked]");
            }
            lines.append(QStringLiteral("%1- QTreeWidgetItem \"%2\" [ref=%3] [click: %4,%5]%6%7")
                             .arg(itemIndent,
                                  item.value(QStringLiteral("text")).toString(),
                                  item.value(QStringLiteral("ref")).toString())
                             .arg(item.value(QStringLiteral("click_x")).toInt())
                             .arg(item.value(QStringLiteral("click_y")).toInt())
                             .arg(expanded)
                             .arg(checkState));
        }
    }

    // Inline list widget items (cap to bound token usage). The click point is
    // the item's center in widget coordinates — pass it as qt_click's
    // position on the list widget itself.
    if (QListWidget *list = qobject_cast<QListWidget *>(widget)) {
        const int total = list->count();
        const int shown = qMin(total, 20);
        for (int i = 0; i < shown; ++i) {
            QListWidgetItem *item = list->item(i);
            const QPoint center = list->visualItemRect(item).center();
            const QString current = i == list->currentRow() ? QStringLiteral(" [current]")
                                                            : QString();
            lines.append(QStringLiteral("%1  - QListWidgetItem \"%2\" [click: %3,%4]%5")
                             .arg(indent, item->text())
                             .arg(center.x())
                             .arg(center.y())
                             .arg(current));
        }
        if (total > shown)
            lines.append(QStringLiteral("%1  ... (%2 more items)").arg(indent).arg(total - shown));
    }

    // Inline table contents as one line per row (capped in both dimensions).
    if (QTableWidget *table = qobject_cast<QTableWidget *>(widget)) {
        const int rows = qMin(table->rowCount(), 10);
        const int cols = qMin(table->columnCount(), 8);
        for (int r = 0; r < rows; ++r) {
            QStringList cells;
            for (int c = 0; c < cols; ++c) {
                QTableWidgetItem *cell = table->item(r, c);
                cells << (cell ? cell->text() : QString());
            }
            lines.append(QStringLiteral("%1  - [row %2] %3")
                             .arg(indent)
                             .arg(r)
                             .arg(cells.join(QStringLiteral(" | "))));
        }
        if (table->rowCount() > rows)
            lines.append(QStringLiteral("%1  ... (%2 more rows)")
                             .arg(indent)
                             .arg(table->rowCount() - rows));
    }

    const QObjectList children = widget->children();
    for (QObject *child : children) {
        if (QWidget *childWidget = qobject_cast<QWidget *>(child)) {
            // Top-level windows (e.g. a QDialog parented to this widget) are
            // walked separately — walking them here too would duplicate them.
            if (childWidget->isWindow())
                continue;
            count += walkWidget(childWidget, depth + 1, maxDepth, lines, skipHidden);
        }
    }
    return count;
}

// ------------------------------------------------------------- active_popup

QJsonObject Introspector::activePopup()
{
    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        throw ToolError(QStringLiteral("No QApplication running"));

    // The entry point when something blocks the UI: "a warning popped up",
    // "exit asked whether to save", ... The agent does not know the dialog's
    // objectName, so give it the dialog plus directly-clickable button refs.
    QWidget *popup = app->activeModalWidget();
    QString kind = QStringLiteral("modal");
    if (!popup) {
        popup = app->activePopupWidget();
        kind = QStringLiteral("popup");
    }
    if (!popup || !popup->isVisible())
        return QJsonObject{{QStringLiteral("found"), false}};

    QJsonArray buttons;
    const auto childButtons = popup->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : childButtons) {
        buttons.append(QJsonObject{
            {QStringLiteral("ref"), m_registry.registerObject(button, QStringLiteral("w"))},
            {QStringLiteral("text"), button->text()},
            {QStringLiteral("enabled"), button->isEnabled()},
        });
    }

    return QJsonObject{
        {QStringLiteral("found"), true},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("ref"), m_registry.registerObject(popup, QStringLiteral("w"))},
        {QStringLiteral("class"), QString::fromLatin1(popup->metaObject()->className())},
        {QStringLiteral("objectName"), popup->objectName()},
        {QStringLiteral("title"), popup->windowTitle()},
        {QStringLiteral("text"), safeText(popup)},
        {QStringLiteral("buttons"), buttons},
    };
}

// ------------------------------------------------------------- widget_details

QJsonObject Introspector::widgetDetails(const QString &ref)
{
    // Tree item refs ("i...") resolve to a non-QObject item.
    if (QTreeWidgetItem *item = m_registry.resolveTreeItem(ref)) {
        QTreeWidget *tree = item->treeWidget();
        QJsonArray columns;
        const int columnCount = tree ? tree->columnCount() : 1;
        for (int c = 0; c < columnCount; ++c)
            columns.append(item->text(c));
        const QRect visualRect = tree ? tree->visualItemRect(item) : QRect();
        return QJsonObject{
            {QStringLiteral("class"), QStringLiteral("QTreeWidgetItem")},
            {QStringLiteral("objectName"), QString()},
            {QStringLiteral("text"), item->text(0)},
            {QStringLiteral("columns"), columns},
            {QStringLiteral("expanded"), item->isExpanded()},
            {QStringLiteral("selected"), item->isSelected()},
            {QStringLiteral("disabled"), !(item->flags() & Qt::ItemIsEnabled)},
            {QStringLiteral("checkable"), bool(item->flags() & Qt::ItemIsUserCheckable)},
            {QStringLiteral("checked"), item->checkState(0) == Qt::Checked},
            {QStringLiteral("children"), item->childCount()},
            {QStringLiteral("click_x"), visualRect.center().x()},
            {QStringLiteral("click_y"), visualRect.center().y()},
        };
    }

    QObject *obj = m_registry.resolveOrThrow(ref);

    QJsonObject result{
        {QStringLiteral("class"), QString::fromLatin1(obj->metaObject()->className())},
        {QStringLiteral("objectName"), obj->objectName()},
    };

    if (QWidget *widget = qobject_cast<QWidget *>(obj)) {
        result.insert(QStringLiteral("geometry"), geometryJson(widget->geometry()));
        result.insert(QStringLiteral("visible"), widget->isVisible());
        result.insert(QStringLiteral("enabled"), widget->isEnabled());

        const QSize hint = widget->sizeHint();
        result.insert(QStringLiteral("size_hint"),
                      QJsonArray{hint.width(), hint.height()});
        const QSize minHint = widget->minimumSizeHint();
        result.insert(QStringLiteral("min_size_hint"),
                      QJsonArray{minHint.width(), minHint.height()});

        const QSizePolicy sp = widget->sizePolicy();
        result.insert(QStringLiteral("size_policy"),
                      QStringLiteral("%1x%2").arg(sizePolicyName(sp.horizontalPolicy()),
                                                  sizePolicyName(sp.verticalPolicy())));

        QLayout *layout = widget->layout();
        result.insert(QStringLiteral("layout"),
                      layout ? QString::fromLatin1(layout->metaObject()->className())
                             : QStringLiteral("none"));

        const QMargins m = widget->contentsMargins();
        result.insert(QStringLiteral("margins"), QJsonArray{m.left(), m.top(), m.right(), m.bottom()});
    }

    result.insert(QStringLiteral("properties"), readProperties(obj));

    QStringList chain;
    for (QObject *p = obj->parent(); p; p = p->parent()) {
        chain.append(QStringLiteral("%1(%2)")
                         .arg(QString::fromLatin1(p->metaObject()->className()), p->objectName()));
    }
    result.insert(QStringLiteral("parent_chain"), QJsonArray::fromStringList(chain));

    if (QTreeWidget *tree = qobject_cast<QTreeWidget *>(obj))
        result.insert(QStringLiteral("items"), treeWidgetItems(tree, m_registry));

    return result;
}

// --------------------------------------------------------------- find_widget

QJsonObject Introspector::findWidget(const QString &pattern, const QString &className,
                                     const QString &objectName, const QString &text,
                                     const QString &rootRef, bool visibleOnly, int maxResults)
{
    if (pattern.isEmpty() && className.isEmpty() && objectName.isEmpty() && text.isEmpty()) {
        throw ToolError(QStringLiteral(
            "At least one of pattern, class_name, object_name, or text required"));
    }

    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return QJsonObject{{QStringLiteral("widgets"), QJsonArray{}},
                           {QStringLiteral("count"), 0}};

    QWidgetList searchRoots;
    if (!rootRef.isEmpty()) {
        QObject *obj = m_registry.resolveOrThrow(rootRef);
        QWidget *root = qobject_cast<QWidget *>(obj);
        if (!root)
            throw ToolError(QStringLiteral("Ref %1 is not a QWidget").arg(rootRef));
        searchRoots << root;
    } else if (visibleOnly) {
        searchRoots = visibleTopLevelWidgets(app);
    } else {
        searchRoots = app->topLevelWidgets();
    }

    const QString patternLower = pattern.toLower();
    const QString classLower = className.toLower();
    const QString objectLower = objectName.toLower();
    const QString textLower = text.toLower();

    QJsonArray matches;

    // A window parented to another widget (e.g. a QDialog) is reachable both
    // via its parent's children and via topLevelWidgets — visit once.
    QSet<QWidget *> seen;

    std::function<void(QWidget *)> walk = [&](QWidget *widget) {
        if (matches.size() >= maxResults)
            return;
        if (seen.contains(widget))
            return;
        seen.insert(widget);
        if (visibleOnly && !widget->isVisible())
            return;

        const QString wClass = QString::fromLatin1(widget->metaObject()->className());
        const QString wName = widget->objectName();
        const QString wText = safeText(widget);

        // All explicit filters must match (AND logic); pattern ORs across fields.
        bool ok = true;
        if (!className.isEmpty() && wClass.toLower() != classLower)
            ok = false;
        if (ok && !objectName.isEmpty() && !wName.toLower().contains(objectLower))
            ok = false;
        if (ok && !text.isEmpty() && !wText.toLower().contains(textLower))
            ok = false;
        if (ok && !pattern.isEmpty()
            && !wClass.toLower().contains(patternLower)
            && !wName.toLower().contains(patternLower)
            && !wText.toLower().contains(patternLower)) {
            ok = false;
        }

        if (ok) {
            const QString ref = m_registry.registerObject(widget, QStringLiteral("w"));
            QJsonObject entry{
                {QStringLiteral("ref"), ref},
                {QStringLiteral("class"), wClass},
                {QStringLiteral("objectName"), wName},
                {QStringLiteral("text"),
                 wText.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(wText)},
                {QStringLiteral("geometry"), geometryJson(widget->geometry())},
                {QStringLiteral("visible"), widget->isVisible()},
                {QStringLiteral("enabled"), widget->isEnabled()},
            };
            // Tooltip: auxiliary info for both humans and AI agents (see snapshot).
            const QString tip = widget->toolTip();
            if (!tip.isEmpty())
                entry.insert(QStringLiteral("tooltip"), truncate(tip));
            matches.append(entry);
        }

        const QObjectList children = widget->children();
        for (QObject *child : children) {
            if (matches.size() >= maxResults)
                break;
            if (QWidget *childWidget = qobject_cast<QWidget *>(child))
                walk(childWidget);
        }
    };

    for (QWidget *root : qAsConst(searchRoots)) {
        walk(root);
        if (matches.size() >= maxResults)
            break;
    }

    return QJsonObject{
        {QStringLiteral("widgets"), matches},
        {QStringLiteral("count"), matches.size()},
    };
}

// -------------------------------------------------------------- list_windows

QJsonObject Introspector::listWindows(bool skipHidden)
{
    QJsonArray windows;
    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (app) {
        const QWidgetList topLevels = app->topLevelWidgets();
        for (QWidget *w : topLevels) {
            if (skipHidden && !w->isVisible())
                continue;
            windows.append(QJsonObject{
                {QStringLiteral("class"), QString::fromLatin1(w->metaObject()->className())},
                {QStringLiteral("objectName"), w->objectName()},
                {QStringLiteral("size"), QStringLiteral("%1x%2").arg(w->width()).arg(w->height())},
                {QStringLiteral("visible"), w->isVisible()},
            });
        }
    }
    return QJsonObject{{QStringLiteral("windows"), windows}};
}

// --------------------------------------------------------------- object_tree

QJsonObject Introspector::objectTree(const QString &rootRef, int maxDepth)
{
    if (!rootRef.isEmpty()) {
        // Resolve first, then walk without clearing.
        QObject *root = m_registry.resolveOrThrow(rootRef);
        QStringList lines;
        const int count = walkObject(root, 0, maxDepth, lines);
        return QJsonObject{
            {QStringLiteral("tree"), lines.join(QLatin1Char('\n'))},
            {QStringLiteral("count"), count},
        };
    }

    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) {
        return QJsonObject{{QStringLiteral("tree"), QStringLiteral("(no QApplication)")},
                           {QStringLiteral("count"), 0}};
    }

    m_registry.sweep();
    QStringList lines;
    const QString appRef = m_registry.registerObject(app, QStringLiteral("w"));
    lines.append(QStringLiteral("- QApplication [ref=%1]").arg(appRef));
    int count = 1;

    const QWidgetList topLevels = app->topLevelWidgets();
    for (QWidget *widget : topLevels)
        count += walkObject(widget, 1, maxDepth, lines);

    return QJsonObject{
        {QStringLiteral("tree"), lines.join(QLatin1Char('\n'))},
        {QStringLiteral("count"), count},
    };
}

int Introspector::walkObject(QObject *obj, int depth, int maxDepth, QStringList &lines)
{
    if (depth > maxDepth)
        return 0;

    const QString ref = m_registry.registerObject(obj, QStringLiteral("w"));
    const QString cls = QString::fromLatin1(obj->metaObject()->className());
    const QString name = obj->objectName();
    const QString indent(depth * 2, QLatin1Char(' '));

    QString line = QStringLiteral("%1- %2").arg(indent, cls);
    if (!name.isEmpty())
        line += QStringLiteral(" \"%1\"").arg(name);
    line += QStringLiteral(" [ref=%1]").arg(ref);
    lines.append(line);

    int count = 1;
    const QObjectList children = obj->children();
    for (QObject *child : children)
        count += walkObject(child, depth + 1, maxDepth, lines);
    return count;
}

// ------------------------------------------------------------------- schemas

void Introspector::registerTools(ToolRegistry &registry)
{
    registry.registerTool(
        QStringLiteral("qt_snapshot"),
        QStringLiteral("Capture the full Qt widget tree as a structured text snapshot with "
                       "widget types, object names, text, geometry, visibility and refs "
                       "(w1, w2, ...). Refs are used by all other tools."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("max_depth"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("default"), 10}}},
                 {QStringLiteral("root_ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Limit the snapshot to this widget's subtree.")}}},
                 {QStringLiteral("skip_hidden"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false},
                              {QStringLiteral("description"),
                               QStringLiteral("Skip children of hidden containers.")}}},
             }},
        },
        [this](const QJsonObject &args) {
            return ToolResult::fromData(snapshot(
                args.value(QStringLiteral("max_depth")).toInt(10),
                args.value(QStringLiteral("root_ref")).toString(),
                args.value(QStringLiteral("skip_hidden")).toBool(false)));
        });

    registry.registerTool(
        QStringLiteral("qt_widget_details"),
        QStringLiteral("Get detailed information about one widget: all Qt properties, "
                       "geometry, size hints, layout, parent chain."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
             }},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("ref")}},
        },
        [this](const QJsonObject &args) {
            const QString ref = args.value(QStringLiteral("ref")).toString();
            if (ref.isEmpty())
                throw ToolError(QStringLiteral("ref is required"));
            return ToolResult::fromData(widgetDetails(ref));
        });

    registry.registerTool(
        QStringLiteral("qt_list_windows"),
        QStringLiteral("List all top-level windows with class, objectName, size and "
                       "visibility."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("skip_hidden"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), true}}},
             }},
        },
        [this](const QJsonObject &args) {
            return ToolResult::fromData(
                listWindows(args.value(QStringLiteral("skip_hidden")).toBool(true)));
        });

    registry.registerTool(
        QStringLiteral("qt_active_popup"),
        QStringLiteral("Report the currently active modal dialog or popup (e.g. a "
                       "QMessageBox blocking the UI): its ref, title, text, and all "
                       "buttons with clickable refs. Returns found=false when nothing "
                       "blocks the UI."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{}},
        },
        [this](const QJsonObject &) { return ToolResult::fromData(activePopup()); });

    registry.registerTool(
        QStringLiteral("qt_object_tree"),
        QStringLiteral("Get the full QObject parent-child tree (not just visible widgets), "
                       "rooted at QApplication or at a given ref."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("root_ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("max_depth"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("default"), 10}}},
             }},
        },
        [this](const QJsonObject &args) {
            return ToolResult::fromData(objectTree(
                args.value(QStringLiteral("root_ref")).toString(),
                args.value(QStringLiteral("max_depth")).toInt(10)));
        });

    registry.registerTool(
        QStringLiteral("qt_find_widget"),
        QStringLiteral("Search the widget tree by class name, objectName and/or text. "
                       "Prefer this over qt_snapshot when you know what you are looking for. "
                       "Returned refs are immediately usable with other tools."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("pattern"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Case-insensitive substring matched against class "
                                              "name, objectName OR text.")}}},
                 {QStringLiteral("class_name"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Exact class name, case-insensitive "
                                              "(e.g. 'QLineEdit').")}}},
                 {QStringLiteral("object_name"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Substring match on objectName.")}}},
                 {QStringLiteral("text"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Substring match on widget text.")}}},
                 {QStringLiteral("root_ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("visible_only"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), true}}},
                 {QStringLiteral("max_results"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("default"), 20}}},
             }},
        },
        [this](const QJsonObject &args) {
            return ToolResult::fromData(findWidget(
                args.value(QStringLiteral("pattern")).toString(),
                args.value(QStringLiteral("class_name")).toString(),
                args.value(QStringLiteral("object_name")).toString(),
                args.value(QStringLiteral("text")).toString(),
                args.value(QStringLiteral("root_ref")).toString(),
                args.value(QStringLiteral("visible_only")).toBool(true),
                args.value(QStringLiteral("max_results")).toInt(20)));
        });
}

} // namespace QtMcp

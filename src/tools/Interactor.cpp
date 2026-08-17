#include "Interactor.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRadioButton>
#include <QSet>
#include <QStyle>
#include <QStyleOption>
#include <QTableWidget>
#include <QTextDocument>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVariant>
#include <QWidget>

#include "../core/RefRegistry.h"
#include "../core/ToolError.h"
#include "../protocol/ToolRegistry.h"

#include <functional>

namespace QtMcp {

namespace {

const int DEFAULT_WAIT_TIMEOUT_MS = 5000;
const int WAIT_POLL_INTERVAL_MS = 50;

Qt::MouseButton parseButton(const QString &button)
{
    if (button == QLatin1String("right"))
        return Qt::RightButton;
    if (button == QLatin1String("middle"))
        return Qt::MiddleButton;
    return Qt::LeftButton;
}

Qt::KeyboardModifiers parseModifiers(const QStringList &modifiers)
{
    Qt::KeyboardModifiers result = Qt::NoModifier;
    for (const QString &m : modifiers) {
        const QString lower = m.toLower();
        if (lower == QLatin1String("shift"))
            result |= Qt::ShiftModifier;
        else if (lower == QLatin1String("ctrl"))
            result |= Qt::ControlModifier;
        else if (lower == QLatin1String("alt"))
            result |= Qt::AltModifier;
        else if (lower == QLatin1String("meta"))
            result |= Qt::MetaModifier;
    }
    return result;
}

struct ParsedKey {
    int key = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QString text;
};

/// Parse key strings like "Return", "Escape", "Ctrl+S", "a".
ParsedKey parseKey(const QString &keyStr)
{
    static const QHash<QString, int> keyMap = {
        {QStringLiteral("return"), Qt::Key_Return},
        {QStringLiteral("enter"), Qt::Key_Return},
        {QStringLiteral("escape"), Qt::Key_Escape},
        {QStringLiteral("esc"), Qt::Key_Escape},
        {QStringLiteral("tab"), Qt::Key_Tab},
        {QStringLiteral("backspace"), Qt::Key_Backspace},
        {QStringLiteral("delete"), Qt::Key_Delete},
        {QStringLiteral("space"), Qt::Key_Space},
        {QStringLiteral("up"), Qt::Key_Up},
        {QStringLiteral("down"), Qt::Key_Down},
        {QStringLiteral("left"), Qt::Key_Left},
        {QStringLiteral("right"), Qt::Key_Right},
        {QStringLiteral("home"), Qt::Key_Home},
        {QStringLiteral("end"), Qt::Key_End},
        {QStringLiteral("pageup"), Qt::Key_PageUp},
        {QStringLiteral("pagedown"), Qt::Key_PageDown},
        {QStringLiteral("f1"), Qt::Key_F1},
        {QStringLiteral("f2"), Qt::Key_F2},
        {QStringLiteral("f3"), Qt::Key_F3},
        {QStringLiteral("f4"), Qt::Key_F4},
        {QStringLiteral("f5"), Qt::Key_F5},
        {QStringLiteral("f6"), Qt::Key_F6},
        {QStringLiteral("f7"), Qt::Key_F7},
        {QStringLiteral("f8"), Qt::Key_F8},
        {QStringLiteral("f9"), Qt::Key_F9},
        {QStringLiteral("f10"), Qt::Key_F10},
        {QStringLiteral("f11"), Qt::Key_F11},
        {QStringLiteral("f12"), Qt::Key_F12},
    };

    ParsedKey result;
    const QStringList parts = keyStr.split(QLatin1Char('+'));
    const QString keyPart = parts.last();
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const QString mod = parts[i].toLower();
        if (mod == QLatin1String("shift"))
            result.modifiers |= Qt::ShiftModifier;
        else if (mod == QLatin1String("ctrl"))
            result.modifiers |= Qt::ControlModifier;
        else if (mod == QLatin1String("alt"))
            result.modifiers |= Qt::AltModifier;
        else if (mod == QLatin1String("meta"))
            result.modifiers |= Qt::MetaModifier;
    }

    const auto it = keyMap.constFind(keyPart.toLower());
    if (it != keyMap.constEnd()) {
        result.key = it.value();
        return result;
    }

    if (keyPart.size() == 1) {
        const QChar ch = keyPart.at(0);
        result.key = ch.toUpper().unicode();
        result.text = QString(ch);
        return result;
    }

    throw ToolError(QStringLiteral(
        "Unknown key: '%1'. Supported names: Return, Escape, Tab, Backspace, Delete, "
        "Space, Up, Down, Left, Right, Home, End, PageUp, PageDown, F1-F12, modifiers "
        "Ctrl/Shift/Alt/Meta, or a single character. To type text, use qt_type_text.")
                        .arg(keyPart));
}

/// Find a visible widget by objectName across all top-level widgets.
QWidget *findWidgetByName(const QString &name)
{
    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return nullptr;
    const QWidgetList topLevels = app->topLevelWidgets();
    for (QWidget *tlw : topLevels) {
        if (tlw->objectName() == name && tlw->isVisible())
            return tlw;
        if (QWidget *found = tlw->findChild<QWidget *>(name)) {
            if (found->isVisible())
                return found;
        }
    }
    return nullptr;
}

/// Read a QTreeWidgetItem pseudo-property. Invalid QVariant = unsupported name.
QVariant treeItemProperty(QTreeWidgetItem *item, const QString &name)
{
    if (name == QLatin1String("expanded"))
        return item->isExpanded();
    if (name == QLatin1String("checked"))
        return item->checkState(0) == Qt::Checked;
    if (name == QLatin1String("selected"))
        return item->isSelected();
    if (name == QLatin1String("text"))
        return item->text(0);
    return QVariant();
}

const char TREE_ITEM_PROPERTIES[] = "expanded, checked, selected, text";

bool jsonToPoint(const QJsonValue &v, QPoint *out)
{
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() != 2)
            return false;
        *out = QPoint(a[0].toInt(), a[1].toInt());
        return true;
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        *out = QPoint(o.value(QStringLiteral("x")).toInt(),
                      o.value(QStringLiteral("y")).toInt());
        return true;
    }
    return false;
}

bool jsonToSize(const QJsonValue &v, QSize *out)
{
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() != 2)
            return false;
        *out = QSize(a[0].toInt(), a[1].toInt());
        return true;
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        *out = QSize(o.value(QStringLiteral("width")).toInt(),
                     o.value(QStringLiteral("height")).toInt());
        return true;
    }
    return false;
}

bool jsonToRect(const QJsonValue &v, QRect *out)
{
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() != 4)
            return false;
        *out = QRect(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt());
        return true;
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        *out = QRect(o.value(QStringLiteral("x")).toInt(),
                     o.value(QStringLiteral("y")).toInt(),
                     o.value(QStringLiteral("width")).toInt(),
                     o.value(QStringLiteral("height")).toInt());
        return true;
    }
    return false;
}

/// Depth-first search of a model for the first index whose DisplayRole
/// contains `text`. Table-like models scan every column; tree children only
/// column 0. Depth-capped against pathological models.
QModelIndex findViewIndexByText(QAbstractItemModel *model, const QString &text)
{
    const QString needle = text.trimmed();
    std::function<QModelIndex(const QModelIndex &, int)> walk =
        [&](const QModelIndex &parent, int depth) -> QModelIndex {
        if (depth > 8)
            return QModelIndex();
        const int rows = model->rowCount(parent);
        const int cols = model->columnCount(parent);
        for (int r = 0; r < rows; ++r) {
            const int colLimit = parent.isValid() ? 1 : cols;
            for (int c = 0; c < colLimit; ++c) {
                const QModelIndex idx = model->index(r, c, parent);
                if (idx.isValid()
                    && idx.data(Qt::DisplayRole).toString().trimmed().contains(needle))
                    return idx;
            }
            const QModelIndex child = walk(model->index(r, 0, parent), depth + 1);
            if (child.isValid())
                return child;
        }
        return QModelIndex();
    };
    return walk(QModelIndex(), 0);
}

/// Dump the DisplayRole contents of an item view's model as text (rows as
/// lines, columns tab-separated, tree children indented). Capped to keep
/// responses bounded on huge models.
QString dumpViewText(QAbstractItemView *view)
{
    QAbstractItemModel *model = view->model();
    if (!model)
        return QString();
    QStringList lines;
    std::function<void(const QModelIndex &, int)> walk =
        [&](const QModelIndex &parent, int depth) {
        if (lines.size() >= 200 || depth > 8)
            return;
        const int rows = model->rowCount(parent);
        const int cols = qMin(model->columnCount(parent), 20);
        for (int r = 0; r < rows && lines.size() < 200; ++r) {
            QStringList cells;
            for (int c = 0; c < cols; ++c)
                cells << model->index(r, c, parent).data(Qt::DisplayRole).toString();
            lines << QString(depth * 2, QLatin1Char(' ')) + cells.join(QLatin1Char('\t'));
            walk(model->index(r, 0, parent), depth + 1);
        }
    };
    walk(QModelIndex(), 0);
    return lines.join(QLatin1Char('\n'));
}

QString jsonTypeName(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return QStringLiteral("bool");
    case QJsonValue::Double: return QStringLiteral("number");
    case QJsonValue::String: return QStringLiteral("string");
    case QJsonValue::Array: return QStringLiteral("array");
    case QJsonValue::Object: return QStringLiteral("object");
    default: return QStringLiteral("null");
    }
}

/// Convert a numeric QVariant to an exact metatype id (Qt5/Qt6 compatible).
QVariant convertNumeric(qint64 number, int typeId)
{
    QVariant v = QVariant::fromValue(number);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    v.convert(QMetaType(typeId));
#else
    v.convert(typeId);
#endif
    return v;
}

/// JSON -> QVariant conversion table for qt_set_property / qt_invoke_slot.
/// On failure *error describes the problem and an invalid QVariant is returned.
QVariant jsonToVariant(const QJsonValue &value, int typeId, QString *error)
{
    switch (typeId) {
    case QMetaType::Bool:
        if (value.isBool())
            return value.toBool();
        break;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::UChar:
        if (value.isDouble())
            return convertNumeric(qint64(value.toDouble()), typeId);
        if (value.isString()) {
            bool ok = false;
            const qint64 n = value.toString().toLongLong(&ok);
            if (ok)
                return convertNumeric(n, typeId);
        }
        break;
    case QMetaType::Float:
    case QMetaType::Double:
        if (value.isDouble())
            return value.toDouble();
        break;
    case QMetaType::QString:
        if (value.isString())
            return value.toString();
        if (value.isDouble())
            return QString::number(value.toDouble());
        if (value.isBool())
            return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        break;
    case QMetaType::QStringList: {
        if (!value.isArray())
            break;
        QStringList list;
        const QJsonArray arr = value.toArray();
        for (const QJsonValue &item : arr)
            list << item.toString();
        return list;
    }
    case QMetaType::QPoint: {
        QPoint p;
        if (jsonToPoint(value, &p))
            return p;
        break;
    }
    case QMetaType::QSize: {
        QSize s;
        if (jsonToSize(value, &s))
            return s;
        break;
    }
    case QMetaType::QRect: {
        QRect r;
        if (jsonToRect(value, &r))
            return r;
        break;
    }
    case QMetaType::QColor: {
        if (value.isString()) {
            const QColor c(value.toString());
            if (c.isValid())
                return c;
        }
        break;
    }
    case QMetaType::QFont: {
        if (value.isString()) {
            QFont f;
            if (f.fromString(value.toString()))
                return f;
        }
        break;
    }
    case QMetaType::QUrl:
        if (value.isString())
            return QUrl(value.toString());
        break;
    default:
        *error = QStringLiteral("unsupported type id %1").arg(typeId);
        return QVariant();
    }

    *error = QStringLiteral("cannot convert JSON %1 to target type").arg(jsonTypeName(value));
    return QVariant();
}

/// Loose JSON <-> QVariant comparison for qt_wait_for property_equals.
bool jsonEqualsVariant(const QJsonValue &json, const QVariant &variant)
{
    if (QJsonValue::fromVariant(variant) == json)
        return true;
    if (json.isString() && variant.toString() == json.toString())
        return true;
    if (json.isDouble() && variant.canConvert<double>()
        && variant.toDouble() == json.toDouble())
        return true;
    if (json.isBool() && variant.canConvert<bool>() && variant.toBool() == json.toBool())
        return true;
    return false;
}

} // namespace

Interactor::Interactor(RefRegistry &registry)
    : m_registry(registry)
{
}

QWidget *Interactor::resolveWidget(const QString &ref)
{
    QObject *obj = m_registry.resolve(ref);
    if (!obj)
        throw ToolError(QStringLiteral("Widget ref not found: %1").arg(ref));
    QWidget *widget = qobject_cast<QWidget *>(obj);
    if (!widget) {
        throw ToolError(QStringLiteral("Ref %1 (%2) is not a QWidget")
                            .arg(ref, QString::fromLatin1(obj->metaObject()->className())));
    }
    return widget;
}

void Interactor::processEventsFor(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents();
        const qint64 remaining = ms - timer.elapsed();
        if (remaining > 0)
            QThread::msleep(static_cast<unsigned long>(qMin<qint64>(50, qMax<qint64>(1, remaining))));
    }
}

void Interactor::ensureInteractable(QWidget *widget, const QString &ref, bool force)
{
    if (force)
        return;

    const QString name = widget->objectName().isEmpty()
                             ? QString::fromLatin1(widget->metaObject()->className())
                             : widget->objectName();
    const QString hint = QStringLiteral("Use force=true to send events anyway.");

    if (!widget->isVisible()) {
        throw ToolError(QStringLiteral("Widget '%1' (ref %2) is hidden. %3")
                            .arg(name, ref, hint));
    }
    if (!widget->isEnabled()) {
        throw ToolError(QStringLiteral("Widget '%1' (ref %2) is disabled. %3")
                            .arg(name, ref, hint));
    }
    if (QWidget *modal = QApplication::activeModalWidget()) {
        if (widget->window() != modal) {
            const QString modalName = modal->objectName().isEmpty()
                                          ? QString::fromLatin1(modal->metaObject()->className())
                                          : modal->objectName();
            throw ToolError(QStringLiteral("Widget '%1' (ref %2) is blocked by modal window "
                                           "'%3'. %4")
                                .arg(name, ref, modalName, hint));
        }
    }
}

// -------------------------------------------------------------------- click

QJsonObject Interactor::click(const QString &ref, const QString &button,
                              const QStringList &modifiers, const QPoint &position,
                              bool hasPosition, bool force, bool doubleClick,
                              int itemRow, int itemCol, const QString &itemText)
{
    // Tree item refs ("i...") click an item inside its QTreeWidget's viewport.
    {
        QTreeWidget *itemTree = nullptr;
        QTreeWidgetItem *item = nullptr;
        if (m_registry.resolveTreeItemRef(ref, &itemTree, &item))
            return clickTreeItem(ref, itemTree, item, button, modifiers, force, doubleClick);
    }

    QWidget *widget = resolveWidget(ref);
    ensureInteractable(widget, ref, force);

    if (hasPosition && !widget->rect().contains(position)) {
        throw ToolError(QStringLiteral("position (%1,%2) is outside the widget's rect (%3x%4)")
                            .arg(position.x())
                            .arg(position.y())
                            .arg(widget->width())
                            .arg(widget->height()));
    }

    const Qt::MouseButton qtButton = parseButton(button);
    const Qt::KeyboardModifiers qtMods = parseModifiers(modifiers);

    // Cell/item targeting: row/col (or item_text) address a model index
    // directly, no coordinate math required from the client. Off-screen
    // cells are scrolled into view first, so any cell is reachable.
    bool cellTarget = false;
    QPointF cellPos; // in viewport coordinates
    if (itemRow >= 0 || !itemText.isEmpty()) {
        if (QTableWidget *table = qobject_cast<QTableWidget *>(widget)) {
            if (itemCol < 0)
                itemCol = 0;
            if (itemRow >= table->rowCount() || itemCol >= table->columnCount())
                throw ToolError(QStringLiteral("cell (%1,%2) out of range (%3 rows x %4 cols)")
                                    .arg(itemRow).arg(itemCol)
                                    .arg(table->rowCount()).arg(table->columnCount()));
            QTableWidgetItem *cell = table->item(itemRow, itemCol);
            if (!itemText.isEmpty()) {
                // item_text on a QTableWidget: locate the cell by its text
                const QList<QTableWidgetItem *> found =
                    table->findItems(itemText, Qt::MatchContains);
                cell = found.isEmpty() ? nullptr : found.first();
                if (!cell)
                    throw ToolError(QStringLiteral("no cell containing text '%1'").arg(itemText));
            } else if (!cell) {
                throw ToolError(QStringLiteral("cell (%1,%2) is empty").arg(itemRow).arg(itemCol));
            }
            table->scrollToItem(cell);
            cellPos = QPointF(table->visualItemRect(cell).center());
            cellTarget = true;
        } else if (QAbstractItemView *view = qobject_cast<QAbstractItemView *>(widget)) {
            QAbstractItemModel *model = view->model();
            if (!model)
                throw ToolError(QStringLiteral("View %1 has no model").arg(ref));
            QModelIndex index;
            if (!itemText.isEmpty()) {
                index = findViewIndexByText(model, itemText);
                if (!index.isValid())
                    throw ToolError(QStringLiteral("no item containing text '%1' in view %2")
                                        .arg(itemText, ref));
            } else {
                if (itemCol < 0)
                    itemCol = 0;
                if (itemRow >= model->rowCount() || itemCol >= model->columnCount())
                    throw ToolError(QStringLiteral("cell (%1,%2) out of range (%3 rows x %4 cols)")
                                        .arg(itemRow).arg(itemCol)
                                        .arg(model->rowCount()).arg(model->columnCount()));
                index = model->index(itemRow, itemCol);
                if (!index.isValid())
                    throw ToolError(QStringLiteral("cell (%1,%2) has no model index")
                                        .arg(itemRow).arg(itemCol));
            }
            // Collapsed tree branches have no visual rect: expand the chain.
            if (QTreeView *treeView = qobject_cast<QTreeView *>(widget)) {
                for (QModelIndex p = index.parent(); p.isValid(); p = p.parent())
                    treeView->expand(p);
            }
            view->scrollTo(index);
            const QRect r = view->visualRect(index);
            if (!r.isValid())
                throw ToolError(QStringLiteral("item at (%1,%2) has no visible rect (collapsed?)")
                                    .arg(index.row()).arg(index.column()));
            cellPos = QPointF(r.center());
            cellTarget = true;
        } else if (itemRow >= 0 || !itemText.isEmpty()) {
            throw ToolError(QStringLiteral(
                "row/col/item_text clicking is only supported on item views "
                "(QListView/QTableView/QTreeView/QTableWidget); ref %1 is %2")
                                .arg(ref, QString::fromLatin1(widget->metaObject()->className())));
        }
    }

    QPointF pos;
    if (cellTarget) {
        // pos resolved below via the viewport branch
    } else if (hasPosition) {
        pos = QPointF(position);
    } else if (QAbstractButton *btn = qobject_cast<QAbstractButton *>(widget)) {
        // The center of a layout-stretched QCheckBox/QRadioButton usually falls
        // on dead space: only the indicator+text rect accepts clicks. Ask the
        // style for the real clickable area instead of the rect center.
        QStyle::SubElement element = QStyle::SE_PushButtonContents;
        if (qobject_cast<QCheckBox *>(btn))
            element = QStyle::SE_CheckBoxClickRect;
        else if (qobject_cast<QRadioButton *>(btn))
            element = QStyle::SE_RadioButtonClickRect;
        QStyleOptionButton opt;
        opt.initFrom(btn);
        const QRect clickRect = btn->style()->subElementRect(element, &opt, btn);
        pos = clickRect.isValid() ? QPointF(clickRect.center())
                                  : QPointF(widget->rect().center());
    } else {
        pos = QPointF(widget->rect().center());
    }

    // Item views and scroll areas (QListWidget/QTreeWidget/QTableWidget/
    // QScrollArea) handle mouse input on their viewport(), not on the frame
    // widget itself — deliver there.
    QWidget *target = widget;
    if (QAbstractScrollArea *scrollArea = qobject_cast<QAbstractScrollArea *>(widget))
        target = scrollArea->viewport();

    QPointF targetPos = pos;
    if (cellTarget)
        targetPos = cellPos; // already in viewport coordinates
    else if (target != widget)
        targetPos = QPointF(target->mapFromGlobal(widget->mapToGlobal(pos.toPoint())));
    const QPointF globalPos = QPointF(target->mapToGlobal(targetPos.toPoint()));

    // A real mouse press also moves keyboard focus; posted synthetic events
    // don't (focus assignment lives in QApplication's notify path).
    if (widget->focusPolicy() != Qt::NoFocus)
        widget->setFocus(Qt::MouseFocusReason);

    // Events are POSTED, not sent synchronously. A synchronous sendEvent would
    // run the widget's handlers on our own call stack; if one of them enters a
    // nested event loop (modal QDialog::exec(), QMessageBox, ...), this MCP
    // request could not respond until the dialog closes — deadlocking a
    // sequential MCP client whose next requests are the ones that interact
    // with that dialog. Posting returns control immediately; the events are
    // delivered by the event loop right after. Clients that need to
    // synchronize on the outcome use qt_wait_for. Qt auto-deletes posted
    // events and drops them if the receiver is destroyed first.
    QApplication::postEvent(target, new QMouseEvent(QEvent::MouseButtonPress, targetPos,
                                                    globalPos, qtButton, qtButton, qtMods));
    QApplication::postEvent(target, new QMouseEvent(QEvent::MouseButtonRelease, targetPos,
                                                    globalPos, qtButton, Qt::NoButton, qtMods));
    if (doubleClick) {
        QApplication::postEvent(target, new QMouseEvent(QEvent::MouseButtonDblClick, targetPos,
                                                        globalPos, qtButton, qtButton, qtMods));
        QApplication::postEvent(target, new QMouseEvent(QEvent::MouseButtonRelease, targetPos,
                                                        globalPos, qtButton, Qt::NoButton,
                                                        qtMods));
    }
    // Native right-clicks are translated by the platform layer into a
    // QContextMenuEvent; synthetic mouse events never become one. Synthesize
    // it so customContextMenuRequested / contextMenuEvent handlers work.
    if (qtButton == Qt::RightButton) {
        QApplication::postEvent(target, new QContextMenuEvent(QContextMenuEvent::Mouse,
                                                              targetPos.toPoint(),
                                                              globalPos.toPoint(), qtMods));
    }

    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("visible"), widget->isVisible()},
                       {QStringLiteral("enabled"), widget->isEnabled()}};
    if (cellTarget) {
        result.insert(QStringLiteral("cell"), QJsonObject{
            {QStringLiteral("row"), itemRow},
            {QStringLiteral("col"), itemCol},
            {QStringLiteral("x"), cellPos.x()},
            {QStringLiteral("y"), cellPos.y()},
        });
    }
    return result;
}

QJsonObject Interactor::clickTreeItem(const QString &ref, QTreeWidget *tree,
                                      QTreeWidgetItem *item, const QString &button,
                                      const QStringList &modifiers, bool force,
                                      bool doubleClick)
{
    ensureInteractable(tree, ref, force);

    // Visibility: an item is unreachable when it or any ancestor is hidden,
    // or any ancestor is collapsed. (visualItemRect alone is not a reliable
    // indicator for this in Qt5.)
    bool reachable = !item->isHidden();
    for (QTreeWidgetItem *p = item->parent(); p && reachable; p = p->parent()) {
        if (p->isHidden() || !p->isExpanded())
            reachable = false;
    }
    if (!reachable) {
        throw ToolError(QStringLiteral(
            "Tree item (ref %1, \"%2\") is not visible — an ancestor is collapsed or "
            "hidden. Expand it first (qt_set_property expanded=true).")
                            .arg(ref, item->text(0)));
    }

    // Bring the item into view (handles the scrolled-out-of-viewport case).
    tree->scrollToItem(item);
    const QRect rect = tree->visualItemRect(item);
    if (!rect.isValid() || !tree->viewport()->rect().intersects(rect)) {
        throw ToolError(QStringLiteral(
            "Tree item (ref %1, \"%2\") is not visible — an ancestor may be collapsed. "
            "Expand it first (qt_set_property expanded=true).")
                            .arg(ref, item->text(0)));
    }

    const Qt::MouseButton qtButton = parseButton(button);
    const Qt::KeyboardModifiers qtMods = parseModifiers(modifiers);
    QWidget *viewport = tree->viewport();
    const QPointF pos = QPointF(rect.center());
    const QPointF globalPos = QPointF(viewport->mapToGlobal(rect.center()));

    if (tree->focusPolicy() != Qt::NoFocus)
        tree->setFocus(Qt::MouseFocusReason);

    // Posted, not sent — see click() for the rationale.
    QApplication::postEvent(viewport, new QMouseEvent(QEvent::MouseButtonPress, pos, globalPos,
                                                      qtButton, qtButton, qtMods));
    QApplication::postEvent(viewport, new QMouseEvent(QEvent::MouseButtonRelease, pos, globalPos,
                                                      qtButton, Qt::NoButton, qtMods));
    if (doubleClick) {
        QApplication::postEvent(viewport, new QMouseEvent(QEvent::MouseButtonDblClick, pos,
                                                          globalPos, qtButton, qtButton, qtMods));
        QApplication::postEvent(viewport, new QMouseEvent(QEvent::MouseButtonRelease, pos,
                                                          globalPos, qtButton, Qt::NoButton,
                                                          qtMods));
    }
    // Synthesize the context menu event for right clicks — see click().
    if (qtButton == Qt::RightButton) {
        QApplication::postEvent(viewport, new QContextMenuEvent(QContextMenuEvent::Mouse,
                                                                pos.toPoint(),
                                                                globalPos.toPoint(), qtMods));
    }

    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("item_text"), item->text(0)}};
}

QJsonObject Interactor::setTreeItemProperty(const QString &ref, QTreeWidgetItem *item,
                                            const QString &propertyName,
                                            const QJsonValue &value)
{
    const QVariant oldValue = treeItemProperty(item, propertyName);
    if (!oldValue.isValid()) {
        throw ToolError(QStringLiteral("Unsupported tree item property '%1' (ref %2). "
                                       "Supported: %3")
                            .arg(propertyName, ref, QString::fromLatin1(TREE_ITEM_PROPERTIES)));
    }

    if (propertyName == QLatin1String("expanded")) {
        if (!value.isBool())
            throw ToolError(QStringLiteral("expanded expects a boolean"));
        item->setExpanded(value.toBool());
    } else if (propertyName == QLatin1String("checked")) {
        if (!value.isBool())
            throw ToolError(QStringLiteral("checked expects a boolean"));
        item->setCheckState(0, value.toBool() ? Qt::Checked : Qt::Unchecked);
    } else if (propertyName == QLatin1String("selected")) {
        if (!value.isBool())
            throw ToolError(QStringLiteral("selected expects a boolean"));
        item->setSelected(value.toBool());
    } else if (propertyName == QLatin1String("text")) {
        if (!value.isString())
            throw ToolError(QStringLiteral("text expects a string (column 0)"));
        item->setText(0, value.toString());
    }

    QApplication::processEvents();
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("old_value"), QJsonValue::fromVariant(oldValue)},
        {QStringLiteral("value"), QJsonValue::fromVariant(treeItemProperty(item, propertyName))},
    };
}

// --------------------------------------------------------------- type_text

QJsonObject Interactor::typeText(const QString &ref, const QString &text, bool clearFirst,
                                 bool useClipboard, bool force)
{
    QWidget *widget = resolveWidget(ref);
    ensureInteractable(widget, ref, force);
    widget->setFocus();
    QApplication::processEvents();

    if (clearFirst) {
        QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, QString());
        QApplication::sendEvent(widget, &selectAll);
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier, QString());
        QApplication::sendEvent(widget, &del);
        QApplication::processEvents();
    }

    if (useClipboard) {
        QClipboard *clipboard = QApplication::clipboard();
        const QString previous = clipboard->text();
        clipboard->setText(text);
        QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier, QString());
        QKeyEvent pasteRelease(QEvent::KeyRelease, Qt::Key_V, Qt::ControlModifier, QString());
        QApplication::sendEvent(widget, &paste);
        QApplication::sendEvent(widget, &pasteRelease);
        QApplication::processEvents();
        clipboard->setText(previous); // always restore previous clipboard content
    } else {
        for (const QChar ch : text) {
            QKeyEvent pressEvent(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
            QKeyEvent releaseEvent(QEvent::KeyRelease, 0, Qt::NoModifier, QString(ch));
            QApplication::sendEvent(widget, &pressEvent);
            QApplication::sendEvent(widget, &releaseEvent);
        }
    }

    QApplication::processEvents();

    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("visible"), widget->isVisible()},
                       {QStringLiteral("enabled"), widget->isEnabled()}};

    // Echo the resulting text where the widget exposes it — lets agents verify
    // the effect without a separate qt_get_text round trip. Typing uses
    // synchronous sendEvent, so this is the actual post-typing content.
    QString currentText;
    bool hasText = false;
    if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(widget)) {
        currentText = lineEdit->text();
        hasText = true;
    } else if (QPlainTextEdit *plainEdit = qobject_cast<QPlainTextEdit *>(widget)) {
        currentText = plainEdit->toPlainText();
        hasText = true;
    } else if (QTextEdit *textEdit = qobject_cast<QTextEdit *>(widget)) {
        currentText = textEdit->toPlainText();
        hasText = true;
    } else if (widget->metaObject()->indexOfProperty("text") >= 0) {
        currentText = widget->property("text").toString();
        hasText = true;
    }
    if (hasText)
        result.insert(QStringLiteral("text"), currentText);
    return result;
}

// --------------------------------------------------------------- key_press

QJsonObject Interactor::keyPress(const QString &key, const QString &ref, bool force)
{
    QWidget *widget = nullptr;
    if (!ref.isEmpty()) {
        widget = resolveWidget(ref);
        ensureInteractable(widget, ref, force);
    } else {
        widget = QApplication::focusWidget();
        if (!widget)
            throw ToolError(QStringLiteral("No focused widget and no ref provided"));
    }

    const ParsedKey parsed = parseKey(key);
    // Posted, not sent — see click() for the rationale (modal-loop deadlock).
    QApplication::postEvent(widget, new QKeyEvent(QEvent::KeyPress, parsed.key,
                                                  parsed.modifiers, parsed.text));
    QApplication::postEvent(widget, new QKeyEvent(QEvent::KeyRelease, parsed.key,
                                                  parsed.modifiers, parsed.text));

    return QJsonObject{{QStringLiteral("ok"), true}};
}

// --------------------------------------------------------------------- drag

QJsonObject Interactor::drag(const QString &ref, const QPoint &start, bool hasStart,
                             const QString &toRef, const QPoint &end, bool hasEnd,
                             int steps, int durationMs, bool force)
{
    QWidget *source = resolveWidget(ref);
    ensureInteractable(source, ref, force);
    if (toRef.isEmpty())
        throw ToolError(QStringLiteral("to_ref is required"));
    QWidget *target = qobject_cast<QWidget *>(m_registry.resolveOrThrow(toRef));
    if (!target)
        throw ToolError(QStringLiteral("Ref %1 is not a QWidget").arg(toRef));

    const QPoint startLocal = hasStart ? start : source->rect().center();
    const QPoint endLocal = hasEnd ? end : target->rect().center();
    const QPoint startGlobal = source->mapToGlobal(startLocal);
    const QPoint endGlobal = target->mapToGlobal(endLocal);

    // The physical cursor is warped along the drag path: drag implementations
    // (e.g. Qt-Advanced-Docking-System) read QCursor::pos() directly instead
    // of trusting event coordinates, so purely synthetic events cannot drive
    // them. Events are still posted (see click() for the modal-deadlock
    // rationale) and the event loop keeps pumping between steps so the
    // receiver can run its drag-start logic mid-gesture.
    QCursor::setPos(startGlobal);
    QApplication::postEvent(source, new QMouseEvent(QEvent::MouseButtonPress,
                                                    QPointF(startLocal), QPointF(startGlobal),
                                                    Qt::LeftButton, Qt::LeftButton,
                                                    Qt::NoModifier));
    QApplication::processEvents();

    const int n = qMax(2, steps);
    const qint64 totalMs = qMax(0, durationMs);
    QElapsedTimer timer;
    timer.start();
    for (int i = 1; i <= n; ++i) {
        const double t = double(i) / n;
        const QPoint p(startGlobal.x() + int((endGlobal.x() - startGlobal.x()) * t),
                       startGlobal.y() + int((endGlobal.y() - startGlobal.y()) * t));
        QCursor::setPos(p);
        QWidget *under = QApplication::widgetAt(p);
        if (!under)
            under = source;
        const QPointF local = QPointF(under->mapFromGlobal(p));
        QApplication::postEvent(under, new QMouseEvent(QEvent::MouseMove, local, QPointF(p),
                                                       Qt::NoButton, Qt::LeftButton,
                                                       Qt::NoModifier));
        QApplication::processEvents();
        // Pace the gesture while keeping the event loop responsive.
        while (timer.elapsed() < totalMs * i / n) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
    }

    QCursor::setPos(endGlobal);
    QWidget *under = QApplication::widgetAt(endGlobal);
    if (!under)
        under = source;
    const QPointF local = QPointF(under->mapFromGlobal(endGlobal));
    QApplication::postEvent(under, new QMouseEvent(QEvent::MouseButtonRelease, local,
                                                   QPointF(endGlobal), Qt::LeftButton,
                                                   Qt::NoButton, Qt::NoModifier));
    QApplication::processEvents();

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("from"), QStringLiteral("%1,%2").arg(startGlobal.x()).arg(startGlobal.y())},
        {QStringLiteral("to"), QStringLiteral("%1,%2").arg(endGlobal.x()).arg(endGlobal.y())},
        {QStringLiteral("dropped_on"),
         QString::fromLatin1(under->metaObject()->className())},
    };
}

// ------------------------------------------------------------ set_property

QJsonObject Interactor::setProperty(const QString &ref, const QString &propertyName,
                                    const QJsonValue &value)
{
    // Tree item refs get pseudo-property handling.
    if (QTreeWidgetItem *item = m_registry.resolveTreeItem(ref))
        return setTreeItemProperty(ref, item, propertyName, value);

    QObject *obj = m_registry.resolveOrThrow(ref);

    const QVariant oldValue = obj->property(propertyName.toLatin1().constData());

    QVariant converted;
    const QMetaObject *meta = obj->metaObject();
    const int propIndex = meta->indexOfProperty(propertyName.toLatin1().constData());
    if (propIndex >= 0) {
        const QMetaProperty prop = meta->property(propIndex);
        if (prop.isEnumType()) {
            // Enums: accept key name ("Checked"), flag lists ("A|B") or int.
            const QMetaEnum en = prop.enumerator();
            if (value.isString()) {
                const QString key = value.toString();
                bool ok = false;
                int enumValue = -1;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                const bool isFlag = en.isFlagType();
#else
                const bool isFlag = en.isFlag();
#endif
                if (isFlag)
                    enumValue = en.keysToValue(key.toUtf8(), &ok);
                if (!ok)
                    enumValue = en.keyToValue(key.toUtf8(), &ok);
                if (!ok) {
                    // Case-insensitive fallback over single keys.
                    for (int i = 0; i < en.keyCount(); ++i) {
                        if (key.compare(QString::fromLatin1(en.key(i)),
                                        Qt::CaseInsensitive) == 0) {
                            enumValue = en.value(i);
                            ok = true;
                            break;
                        }
                    }
                }
                if (!ok) {
                    throw ToolError(QStringLiteral("Invalid value '%1' for enum property '%2'")
                                        .arg(key, propertyName));
                }
                converted = enumValue;
            } else if (value.isDouble()) {
                converted = value.toInt();
            } else {
                throw ToolError(QStringLiteral("Cannot convert JSON %1 to enum property '%2'")
                                    .arg(jsonTypeName(value), propertyName));
            }
        } else {
            QString error;
            converted = jsonToVariant(value, prop.userType(), &error);
            if (!converted.isValid()) {
                throw ToolError(QStringLiteral("Unsupported value for property '%1' of type '%2': %3")
                                    .arg(propertyName, QString::fromLatin1(prop.typeName()), error));
            }
        }
    } else {
        // Dynamic property: natural JSON conversion.
        converted = value.toVariant();
    }

    const bool ok = obj->setProperty(propertyName.toLatin1().constData(), converted);
    QApplication::processEvents();

    // Read back the actual value after the write: event handlers may reject or
    // adjust it (validation, clamping, interdependent options...), and the
    // caller deserves to see the real outcome, not just success/failure.
    const QVariant current = obj->property(propertyName.toLatin1().constData());

    if (propIndex < 0) {
        // Dynamic property: QObject::setProperty adds it but returns false for
        // non-meta properties — that is success here, not failure. Report it
        // as dynamic so a typo'd property name is at least visible.
        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("dynamic"), true},
            {QStringLiteral("old_value"), QJsonValue::fromVariant(oldValue)},
            {QStringLiteral("value"), QJsonValue::fromVariant(current)},
        };
    }
    if (!ok) {
        throw ToolError(QStringLiteral("Failed to set property '%1' on %2 (read-only or "
                                       "value type mismatch; value unchanged)")
                            .arg(propertyName, QString::fromLatin1(meta->className())));
    }
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("old_value"), QJsonValue::fromVariant(oldValue)},
        {QStringLiteral("value"), QJsonValue::fromVariant(current)},
    };
}

// ------------------------------------------------------------ invoke_slot

QJsonObject Interactor::invokeSlot(const QString &ref, const QString &methodName,
                                   const QJsonArray &args)
{
    QObject *obj = m_registry.resolveOrThrow(ref);

    if (args.size() > 4)
        throw ToolError(QStringLiteral("invoke_slot supports at most 4 arguments"));

    // Tolerate signature-style names like "toggleView(bool)": QMetaMethod::name()
    // carries no parameter list, so match on the bare name.
    QString bareName = methodName;
    const int paren = bareName.indexOf(QLatin1Char('('));
    if (paren > 0)
        bareName.truncate(paren);

    const QMetaObject *meta = obj->metaObject();
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.name() != bareName || method.parameterCount() != args.size())
            continue;

        // Try to convert all arguments to this overload's parameter types.
        QVariant values[4];
        QGenericArgument genericArgs[4];
        bool convertible = true;
        for (int p = 0; p < args.size(); ++p) {
            QString error;
            values[p] = jsonToVariant(args.at(p), method.parameterType(p), &error);
            if (!values[p].isValid()) {
                convertible = false;
                break;
            }
            genericArgs[p] = QGenericArgument(method.parameterTypes().at(p).constData(),
                                              values[p].constData());
        }
        if (!convertible)
            continue;

        const int returnType = method.returnType();
        bool invoked = false;
        QJsonValue returnValue;

        if (returnType == QMetaType::Void || returnType == QMetaType::UnknownType) {
            invoked = method.invoke(obj, Qt::DirectConnection,
                                    genericArgs[0], genericArgs[1], genericArgs[2], genericArgs[3]);
        } else if (returnType == QMetaType::QString) {
            QString r;
            invoked = method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(QString, r),
                                    genericArgs[0], genericArgs[1], genericArgs[2], genericArgs[3]);
            returnValue = r;
        } else if (returnType == QMetaType::Int) {
            int r = 0;
            invoked = method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(int, r),
                                    genericArgs[0], genericArgs[1], genericArgs[2], genericArgs[3]);
            returnValue = r;
        } else if (returnType == QMetaType::Bool) {
            bool r = false;
            invoked = method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(bool, r),
                                    genericArgs[0], genericArgs[1], genericArgs[2], genericArgs[3]);
            returnValue = r;
        } else if (returnType == QMetaType::Double) {
            double r = 0.0;
            invoked = method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(double, r),
                                    genericArgs[0], genericArgs[1], genericArgs[2], genericArgs[3]);
            returnValue = r;
        } else {
            throw ToolError(QStringLiteral("Unsupported return type '%1' of method '%2'")
                                .arg(QString::fromLatin1(method.typeName()), methodName));
        }

        QJsonObject result{{QStringLiteral("ok"), invoked}};
        if (invoked && returnType != QMetaType::Void && returnType != QMetaType::UnknownType)
            result.insert(QStringLiteral("return"), returnValue);
        QApplication::processEvents();
        return result;
    }

    throw ToolError(QStringLiteral("No matching method '%1' with %2 argument(s) on %3")
                        .arg(methodName)
                        .arg(args.size())
                        .arg(QString::fromLatin1(meta->className())));
}

// ---------------------------------------------------------------- wait_for

QJsonObject Interactor::waitFor(const QString &condition, int timeoutMs,
                                const QString &objectName, const QString &ref,
                                const QString &propertyName, const QJsonValue &value)
{
    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        throw ToolError(QStringLiteral("No QApplication running"));

    QElapsedTimer timer;
    timer.start();

    // Last observed value for property_equals, reported on timeout.
    QString lastSeenValue;
    bool hasLastSeen = false;

    auto conditionMet = [&]() -> bool {
        if (condition == QLatin1String("widget_visible")) {
            if (objectName.isEmpty())
                throw ToolError(QStringLiteral("object_name required for widget_visible condition"));
            return findWidgetByName(objectName) != nullptr;
        }
        if (condition == QLatin1String("window_count_changed")) {
            // Baseline is captured once before the loop; see below.
            return false; // handled specially
        }
        if (condition == QLatin1String("property_equals")) {
            if (ref.isEmpty() || propertyName.isEmpty()) {
                throw ToolError(QStringLiteral(
                    "ref and property_name required for property_equals condition"));
            }
            // Tree item refs compare against pseudo-properties.
            if (QTreeWidgetItem *item = m_registry.resolveTreeItem(ref)) {
                const QVariant current = treeItemProperty(item, propertyName);
                if (!current.isValid()) {
                    throw ToolError(QStringLiteral("Unsupported tree item property '%1'. "
                                                   "Supported: %2")
                                        .arg(propertyName,
                                             QString::fromLatin1(TREE_ITEM_PROPERTIES)));
                }
                lastSeenValue = current.toString();
                hasLastSeen = true;
                return jsonEqualsVariant(value, current);
            }
            QObject *obj = m_registry.resolveOrThrow(ref);
            const QVariant current = obj->property(propertyName.toLatin1().constData());
            lastSeenValue = current.toString();
            hasLastSeen = true;
            return jsonEqualsVariant(value, current);
        }
        throw ToolError(QStringLiteral("Unknown condition: %1").arg(condition));
    };

    if (condition == QLatin1String("window_count_changed")) {
        const int initial = app->topLevelWidgets().size();
        while (timer.elapsed() < timeoutMs) {
            app->processEvents();
            if (app->topLevelWidgets().size() != initial) {
                return QJsonObject{{QStringLiteral("ok"), true},
                                   {QStringLiteral("elapsed_ms"), int(timer.elapsed())}};
            }
            QThread::msleep(WAIT_POLL_INTERVAL_MS);
            app->processEvents();
        }
    } else {
        // Validate parameters eagerly (throws before waiting).
        while (timer.elapsed() < timeoutMs) {
            app->processEvents();
            if (conditionMet()) {
                return QJsonObject{{QStringLiteral("ok"), true},
                                   {QStringLiteral("elapsed_ms"), int(timer.elapsed())}};
            }
            QThread::msleep(WAIT_POLL_INTERVAL_MS);
            app->processEvents();
        }
    }

    QString message = QStringLiteral("Timed out after %1ms waiting for condition: %2")
                          .arg(timeoutMs)
                          .arg(condition);
    if (condition == QLatin1String("property_equals") && hasLastSeen) {
        message += QStringLiteral(" (property '%1' last seen as: '%2')")
                       .arg(propertyName, lastSeenValue);
    }
    throw ToolError(message);
}

// ---------------------------------------------------------------- get_text

QJsonObject Interactor::getText(const QString &ref)
{
    // Tree item refs: column 0 text plus all columns.
    if (QTreeWidgetItem *item = m_registry.resolveTreeItem(ref)) {
        QJsonArray columns;
        QTreeWidget *tree = item->treeWidget();
        const int columnCount = tree ? tree->columnCount() : 1;
        for (int c = 0; c < columnCount; ++c)
            columns.append(item->text(c));
        const QString text = item->text(0);
        return QJsonObject{
            {QStringLiteral("text"), text},
            {QStringLiteral("columns"), columns},
            {QStringLiteral("length"), text.size()},
        };
    }

    QObject *obj = m_registry.resolveOrThrow(ref);

    QString text;
    QTextDocument *document = nullptr;
    bool found = false;

    if (QPlainTextEdit *plainEdit = qobject_cast<QPlainTextEdit *>(obj)) {
        text = plainEdit->toPlainText();
        document = plainEdit->document();
        found = true;
    } else if (QTextEdit *textEdit = qobject_cast<QTextEdit *>(obj)) {
        text = textEdit->toPlainText();
        document = textEdit->document();
        found = true;
    } else if (QAbstractItemView *view = qobject_cast<QAbstractItemView *>(obj)) {
        // Model views (QListView/QTableView/QTreeView and the *Widget
        // subclasses): dump the model's display text.
        text = dumpViewText(view);
        found = true;
    } else {
        // Generic paths, in order: invokable text()/currentText() methods, then
        // the "text"/"currentText" Q_PROPERTY (QLabel/QLineEdit/QAbstractButton
        // expose text as a property, not as an invokable method).
        const QMetaObject *meta = obj->metaObject();
        for (const char *sig : {"text()", "currentText()"}) {
            const int index = meta->indexOfMethod(sig);
            if (index < 0)
                continue;
            const QMetaMethod method = meta->method(index);
            if (method.parameterCount() == 0 && method.returnType() == QMetaType::QString) {
                if (method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(QString, text))) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (const char *propName : {"text", "currentText"}) {
                const int index = meta->indexOfProperty(propName);
                if (index >= 0 && meta->property(index).userType() == QMetaType::QString) {
                    text = obj->property(propName).toString();
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) {
        throw ToolError(QStringLiteral("Widget %1 does not support text extraction")
                            .arg(QString::fromLatin1(obj->metaObject()->className())));
    }

    QJsonObject result{
        {QStringLiteral("text"), text},
        {QStringLiteral("length"), text.size()},
    };

    if (document)
        result.insert(QStringLiteral("line_count"), document->lineCount());
    else
        result.insert(QStringLiteral("line_count"), int(text.count(QLatin1Char('\n'))) + 1);

    bool readOnly = false;
    const QMetaObject *meta = obj->metaObject();
    const int roIndex = meta->indexOfMethod("isReadOnly()");
    if (roIndex >= 0) {
        const QMetaMethod method = meta->method(roIndex);
        if (method.parameterCount() == 0 && method.returnType() == QMetaType::Bool) {
            if (method.invoke(obj, Qt::DirectConnection, Q_RETURN_ARG(bool, readOnly)))
                result.insert(QStringLiteral("read_only"), readOnly);
        }
    } else if (meta->indexOfProperty("readOnly") >= 0) {
        // QLineEdit & co. expose read-only state as a property, not a method.
        result.insert(QStringLiteral("read_only"), obj->property("readOnly").toBool());
    }

    return result;
}

// ---------------------------------------------------------- trigger_action

QJsonObject Interactor::triggerAction(const QString &ref, const QString &actionText,
                                      bool hasActionIndex, int actionIndex)
{
    QObject *obj = m_registry.resolve(ref);
    if (!obj)
        throw ToolError(QStringLiteral("Ref not found: %1").arg(ref));

    QWidget *widget = qobject_cast<QWidget *>(obj);
    if (!widget) {
        throw ToolError(QStringLiteral("Widget %1 has no actions()")
                            .arg(QString::fromLatin1(obj->metaObject()->className())));
    }

    const QList<QAction *> actions = widget->actions();
    if (actions.isEmpty())
        throw ToolError(QStringLiteral("Widget has no actions"));

    const bool hasActionText = !actionText.isNull();
    if (hasActionText == hasActionIndex)
        throw ToolError(QStringLiteral("Provide exactly one of action_text or action_index"));

    // Leaf actions include submenu entries (e.g. QMenuBar -> View -> "Label 4"),
    // so agents can trigger nested menu items in one call. Cycles in menu
    // ownership are guarded by the visited set.
    QList<QAction *> candidates = actions;
    if (hasActionText) {
        candidates.clear();
        QSet<const QAction *> visited;
        std::function<void(const QList<QAction *> &)> collect =
            [&](const QList<QAction *> &level) {
                for (QAction *action : level) {
                    if (!action || visited.contains(action))
                        continue;
                    visited.insert(action);
                    candidates.append(action);
                    if (QMenu *menu = action->menu())
                        collect(menu->actions());
                }
            };
        collect(actions);
    }

    QAction *target = nullptr;
    if (hasActionIndex) {
        if (actionIndex < 0 || actionIndex >= actions.size()) {
            throw ToolError(QStringLiteral("action_index %1 out of range (0..%2)")
                                .arg(actionIndex)
                                .arg(actions.size() - 1));
        }
        target = actions.at(actionIndex);
    } else {
        QString clean = actionText;
        clean.remove(QLatin1Char('&'));
        clean = clean.trimmed();
        for (QAction *action : candidates) {
            QString candidate = action->text();
            candidate.remove(QLatin1Char('&'));
            if (candidate.trimmed() == clean) {
                target = action;
                break;
            }
        }
        if (!target) {
            QStringList available;
            for (QAction *action : candidates) {
                if (!action->isSeparator())
                    available << action->text();
            }
            throw ToolError(QStringLiteral("No action matching '%1'. Available: [%2]")
                                .arg(actionText, available.join(QStringLiteral(", "))));
        }
    }

    if (target->isSeparator())
        throw ToolError(QStringLiteral("Cannot trigger a separator"));
    if (!target->isEnabled())
        throw ToolError(QStringLiteral("Action '%1' is disabled").arg(target->text()));

    // Deferred trigger: the action's slot may open a modal dialog (exec()),
    // which would block this request's response and deadlock a sequential
    // MCP client. Same rationale as click().
    const QString actionTextResult = target->text();
    QPointer<QAction> guard(target);
    QTimer::singleShot(0, qApp, [guard]() {
        if (guard)
            guard->trigger();
    });

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("action_text"), actionTextResult},
    };
}

// ---------------------------------------------------------- file_dialog

QJsonObject Interactor::fileDialog(const QString &path, bool confirm)
{
    if (path.isEmpty())
        throw ToolError(QStringLiteral("path is required"));

    QWidget *active = QApplication::activeModalWidget();
    if (!active)
        active = QApplication::activePopupWidget();
    // Tolerate focus proxies: walk up from the active widget to the dialog.
    QFileDialog *dialog = nullptr;
    for (QWidget *w = active; w; w = w->parentWidget()) {
        dialog = qobject_cast<QFileDialog *>(w);
        if (dialog)
            break;
    }
    if (!dialog) {
        throw ToolError(QStringLiteral(
            "The active modal/popup is not a QFileDialog (it is %1). "
            "Note: the probe sets Qt::AA_DontUseNativeDialogs at install(), so only "
            "dialogs created BEFORE QtMcp::install() can be OS-native and invisible.")
                            .arg(active ? QString::fromLatin1(active->metaObject()->className())
                                        : QStringLiteral("<none>")));
    }

    const QFileInfo info(path);
    // selectFile() alone does not reliably populate the file-name line edit on
    // Qt 5.15's widget dialog (accept() then does nothing); set the text of the
    // internal "fileNameEdit" directly — accept() resolves it against the
    // current directory, for open/save and directory modes alike.
    QLineEdit *nameEdit = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
    QString applied;
    if (dialog->fileMode() == QFileDialog::Directory) {
        // Directory chooser: navigating into the target and accepting selects it.
        const QString target = info.isDir() ? info.absoluteFilePath()
                                            : info.absolutePath();
        dialog->setDirectory(target);
        if (nameEdit)
            nameEdit->setText(target);
        applied = target;
    } else {
        dialog->setDirectory(info.absolutePath());
        dialog->selectFile(info.fileName());
        if (nameEdit)
            nameEdit->setText(info.fileName());
        applied = info.absoluteFilePath();
    }

    // Deferred like trigger_action: accepting may run host code that opens
    // another modal exec(); responding first keeps a sequential client alive.
    // QFileDialog hides accept() as protected — go through the meta-object
    // (QDialog::accept/reject are slots; virtual dispatch reaches the override).
    QPointer<QFileDialog> guard(dialog);
    QTimer::singleShot(0, qApp, [guard, confirm]() {
        if (!guard)
            return;
        QMetaObject::invokeMethod(guard, confirm ? "accept" : "reject");
    });

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("applied_path"), applied},
        {QStringLiteral("confirmed"), confirm},
    };
}

// ------------------------------------------------------------------ schemas

void Interactor::registerTools(ToolRegistry &registry)
{
    registry.registerTool(
        QStringLiteral("qt_click"),
        QStringLiteral("Click a widget or tree item (synthesized QMouseEvent press+release, "
                       "posted asynchronously). Accepts widget refs (w...) and QTreeWidget "
                       "item refs (i...)."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("button"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"),
                               QJsonArray{QStringLiteral("left"), QStringLiteral("right"),
                                          QStringLiteral("middle")}},
                              {QStringLiteral("default"), QStringLiteral("left")}}},
                 {QStringLiteral("double_click"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false}}},
                 {QStringLiteral("modifiers"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                              {QStringLiteral("items"),
                               QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                              {QStringLiteral("description"),
                               QStringLiteral("'shift', 'ctrl', 'alt', 'meta'.")}}},
                 {QStringLiteral("position"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                              {QStringLiteral("items"),
                               QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                              {QStringLiteral("description"),
                               QStringLiteral("[x, y] relative to widget top-left. "
                                              "Default: center.")}}},
                 {QStringLiteral("row"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("description"),
                               QStringLiteral("Item views (QListView/QTableView/QTreeView/"
                                              "QTableWidget): click the cell at this row "
                                              "(with 'col'). Off-screen cells are scrolled into "
                                              "view first. Combine with double_click to open "
                                              "the cell editor.")}}},
                 {QStringLiteral("col"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("description"),
                               QStringLiteral("Cell column for 'row'; default 0.")}}},
                 {QStringLiteral("item_text"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Item views: click the first item whose text "
                                              "contains this substring (tables scan all columns, "
                                              "trees/lists scan column 0; collapsed branches are "
                                              "expanded automatically).")}}},
                 {QStringLiteral("force"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false},
                              {QStringLiteral("description"),
                               QStringLiteral("Bypass the hidden/disabled/modal-blocked "
                                              "guard. Default: refuse to click such widgets.")}}},
             }},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("ref")}},
        },
        [this](const QJsonObject &args) {
            const QString ref = args.value(QStringLiteral("ref")).toString();
            if (ref.isEmpty())
                throw ToolError(QStringLiteral("ref is required"));
            QStringList modifiers;
            const QJsonArray mods = args.value(QStringLiteral("modifiers")).toArray();
            for (const QJsonValue &m : mods)
                modifiers << m.toString();
            QPoint position;
            bool hasPosition = false;
            const QJsonArray posArr = args.value(QStringLiteral("position")).toArray();
            if (posArr.size() == 2) {
                position = QPoint(posArr[0].toInt(), posArr[1].toInt());
                hasPosition = true;
            }
            return ToolResult::fromData(click(
                ref, args.value(QStringLiteral("button")).toString(QStringLiteral("left")),
                modifiers, position, hasPosition,
                args.value(QStringLiteral("force")).toBool(false),
                args.value(QStringLiteral("double_click")).toBool(false),
                args.value(QStringLiteral("row")).toInt(-1),
                args.value(QStringLiteral("col")).toInt(-1),
                args.value(QStringLiteral("item_text")).toString()));
        });

    registry.registerTool(
        QStringLiteral("qt_file_dialog"),
        QStringLiteral("Fill in and confirm (or cancel) the currently active QFileDialog. "
                       "Open/save dialogs: sets the directory and selects the file. Directory "
                       "choosers (getExistingDirectory): navigates to the directory so confirm "
                       "selects it. The probe sets Qt::AA_DontUseNativeDialogs at install(), so "
                       "file dialogs created after that are Qt widgets and drivable; OS-native "
                       "dialogs are not. Call after opening the dialog (e.g. via qt_click), "
                       "optionally qt_wait_for its appearance first."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("path"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Absolute file path (open/save) or directory "
                                              "path (directory chooser).")}}},
                 {QStringLiteral("confirm"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), true},
                              {QStringLiteral("description"),
                               QStringLiteral("true: accept the dialog; false: cancel it.")}}},
             }},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}},
        },
        [this](const QJsonObject &args) {
            return ToolResult::fromData(fileDialog(
                args.value(QStringLiteral("path")).toString(),
                args.value(QStringLiteral("confirm")).toBool(true)));
        });

    registry.registerTool(
        QStringLiteral("qt_type_text"),
        QStringLiteral("Type text into a widget via per-character key events, or via "
                       "clipboard paste (use_clipboard=true) for multi-line text."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("text"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("clear_first"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false}}},
                 {QStringLiteral("use_clipboard"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false}}},
                 {QStringLiteral("force"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false},
                              {QStringLiteral("description"),
                               QStringLiteral("Bypass the hidden/disabled/modal-blocked "
                                              "guard.")}}},
             }},
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("ref"), QStringLiteral("text")}},
        },
        [this](const QJsonObject &args) {
            const QString ref = args.value(QStringLiteral("ref")).toString();
            if (ref.isEmpty())
                throw ToolError(QStringLiteral("ref is required"));
            return ToolResult::fromData(typeText(
                ref, args.value(QStringLiteral("text")).toString(),
                args.value(QStringLiteral("clear_first")).toBool(false),
                args.value(QStringLiteral("use_clipboard")).toBool(false),
                args.value(QStringLiteral("force")).toBool(false)));
        });

    registry.registerTool(
        QStringLiteral("qt_key_press"),
        QStringLiteral("Send a key event (e.g. 'Return', 'Escape', 'Ctrl+S', 'a') to a "
                       "widget or the focused widget."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("key"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("force"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false},
                              {QStringLiteral("description"),
                               QStringLiteral("Bypass the hidden/disabled/modal-blocked "
                                              "guard (only applies when ref is given).")}}},
             }},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("key")}},
        },
        [this](const QJsonObject &args) {
            const QString key = args.value(QStringLiteral("key")).toString();
            if (key.isEmpty())
                throw ToolError(QStringLiteral("key is required"));
            return ToolResult::fromData(
                keyPress(key, args.value(QStringLiteral("ref")).toString(),
                         args.value(QStringLiteral("force")).toBool(false)));
        });

    registry.registerTool(
        QStringLiteral("qt_drag"),
        QStringLiteral("Drag from a widget (e.g. a dock tab) onto another widget (drop "
                       "target). Warps the physical cursor along the path — required "
                       "because drag implementations read QCursor::pos() — while mouse "
                       "events are posted asynchronously. Use for dock rearrangement and "
                       "other drag & drop interactions."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Drag source widget (press happens here).")}}},
                 {QStringLiteral("start"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                              {QStringLiteral("items"),
                               QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                              {QStringLiteral("description"),
                               QStringLiteral("[x, y] in source widget coords; default center.")}}},
                 {QStringLiteral("to_ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"),
                               QStringLiteral("Drop target widget (release happens over it).")}}},
                 {QStringLiteral("end"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                              {QStringLiteral("items"),
                               QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                              {QStringLiteral("description"),
                               QStringLiteral("[x, y] in target widget coords; default center.")}}},
                 {QStringLiteral("steps"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("default"), 20}}},
                 {QStringLiteral("duration_ms"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("default"), 500}}},
                 {QStringLiteral("force"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                              {QStringLiteral("default"), false}}},
             }},
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("ref"), QStringLiteral("to_ref")}},
        },
        [this](const QJsonObject &args) {
            const QString ref = args.value(QStringLiteral("ref")).toString();
            if (ref.isEmpty())
                throw ToolError(QStringLiteral("ref is required"));
            QPoint start, end;
            bool hasStart = false, hasEnd = false;
            const QJsonArray startArr = args.value(QStringLiteral("start")).toArray();
            if (startArr.size() == 2) {
                start = QPoint(startArr[0].toInt(), startArr[1].toInt());
                hasStart = true;
            }
            const QJsonArray endArr = args.value(QStringLiteral("end")).toArray();
            if (endArr.size() == 2) {
                end = QPoint(endArr[0].toInt(), endArr[1].toInt());
                hasEnd = true;
            }
            return ToolResult::fromData(drag(
                ref, start, hasStart,
                args.value(QStringLiteral("to_ref")).toString(), end, hasEnd,
                args.value(QStringLiteral("steps")).toInt(20),
                args.value(QStringLiteral("duration_ms")).toInt(500),
                args.value(QStringLiteral("force")).toBool(false)));
        });

    registry.registerTool(
        QStringLiteral("qt_set_property"),
        QStringLiteral("Set a Qt property on a QObject. Supported value types: int, double, "
                       "bool, string, string list, QPoint, QSize, QRect, QColor, QFont, QUrl "
                       "and enums (by key name or value)."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("property_name"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("value"),
                  QJsonObject{{QStringLiteral("description"),
                               QStringLiteral("New value for the property.")}}},
             }},
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("ref"), QStringLiteral("property_name"),
                        QStringLiteral("value")}},
        },
        [this](const QJsonObject &args) {
            const QString ref = args.value(QStringLiteral("ref")).toString();
            const QString propertyName = args.value(QStringLiteral("property_name")).toString();
            if (ref.isEmpty() || propertyName.isEmpty())
                throw ToolError(QStringLiteral("ref and property_name are required"));
            return ToolResult::fromData(
                setProperty(ref, propertyName, args.value(QStringLiteral("value"))));
        });

    registry.registerTool(
        QStringLiteral("qt_invoke_slot"),
        QStringLiteral("Invoke a slot or invokable method on a QObject (up to 4 arguments, "
                       "common scalar/string types; void or simple return values)."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("method_name"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("args"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
             }},
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("ref"), QStringLiteral("method_name")}},
        },
        [this](const QJsonObject &args) {
            const QString ref = args.value(QStringLiteral("ref")).toString();
            const QString methodName = args.value(QStringLiteral("method_name")).toString();
            if (ref.isEmpty() || methodName.isEmpty())
                throw ToolError(QStringLiteral("ref and method_name are required"));
            return ToolResult::fromData(invokeSlot(
                ref, methodName, args.value(QStringLiteral("args")).toArray()));
        });

    registry.registerTool(
        QStringLiteral("qt_wait_for"),
        QStringLiteral("Wait for a UI state change: 'widget_visible' (object_name), "
                       "'window_count_changed', or 'property_equals' (ref, property_name, "
                       "value). Polls inside the Qt event loop."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("condition"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("enum"),
                               QJsonArray{QStringLiteral("widget_visible"),
                                          QStringLiteral("window_count_changed"),
                                          QStringLiteral("property_equals")}}}},
                 {QStringLiteral("timeout_ms"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                              {QStringLiteral("default"), DEFAULT_WAIT_TIMEOUT_MS}}},
                 {QStringLiteral("object_name"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("property_name"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("value"),
                  QJsonObject{{QStringLiteral("description"),
                               QStringLiteral("Expected value (property_equals).")}}},
             }},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("condition")}},
        },
        [this](const QJsonObject &args) {
            const QString condition = args.value(QStringLiteral("condition")).toString();
            if (condition.isEmpty())
                throw ToolError(QStringLiteral("condition is required"));
            return ToolResult::fromData(waitFor(
                condition,
                args.value(QStringLiteral("timeout_ms")).toInt(DEFAULT_WAIT_TIMEOUT_MS),
                args.value(QStringLiteral("object_name")).toString(),
                args.value(QStringLiteral("ref")).toString(),
                args.value(QStringLiteral("property_name")).toString(),
                args.value(QStringLiteral("value"))));
        });

    registry.registerTool(
        QStringLiteral("qt_get_text"),
        QStringLiteral("Extract text content from a text-bearing widget (QLineEdit, QLabel, "
                       "QTextEdit, QPlainTextEdit, QComboBox, anything with text()/"
                       "toPlainText())."),
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
            return ToolResult::fromData(getText(ref));
        });

    registry.registerTool(
        QStringLiteral("qt_trigger_action"),
        QStringLiteral("Trigger a QAction on a QMenu/QMenuBar/QToolBar or any widget with "
                       "actions, by display text ('&' stripped) or 0-based index."),
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("ref"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("action_text"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                 {QStringLiteral("action_index"),
                  QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
             }},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("ref")}},
        },
        [this](const QJsonObject &args) {
            const QString ref = args.value(QStringLiteral("ref")).toString();
            if (ref.isEmpty())
                throw ToolError(QStringLiteral("ref is required"));
            const QJsonValue textValue = args.value(QStringLiteral("action_text"));
            const QJsonValue indexValue = args.value(QStringLiteral("action_index"));
            const bool hasIndex = indexValue.isDouble();
            return ToolResult::fromData(triggerAction(
                ref, textValue.isString() ? textValue.toString() : QString(),
                hasIndex, indexValue.toInt()));
        });
}

} // namespace QtMcp

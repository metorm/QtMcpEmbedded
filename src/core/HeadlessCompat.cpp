#include "HeadlessCompat.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QWindow>

// QWindowSystemInterface (used to synthetically activate a window on
// headless platforms) is a QPA header; it ships with standard Qt installs
// but needs the gui-private include paths. Degrade gracefully without them.
#if defined(__has_include)
#if __has_include(<qpa/qwindowsysteminterface.h>)
#include <qpa/qwindowsysteminterface.h>
#define QTMCP_HAVE_WINDOW_ACTIVATION 1
#endif
#endif

namespace QtMcp {

namespace {
// Dynamic-property keys used to pass guard state from the swallowed Show
// event to a later Close event on the same box.
const char GUARDED_PROP[] = "_qtmcp_msgbox_guarded";
const char ESCAPE_PROP[] = "_qtmcp_msgbox_escape";
} // namespace

HeadlessCompat::HeadlessCompat(bool windowActivation, bool messageBoxGuard,
                               QObject *parent)
    : QObject(parent),
      m_windowActivation(windowActivation),
      m_messageBoxGuard(messageBoxGuard)
{
}

HeadlessCompat *HeadlessCompat::installIfNeeded(QObject *parent)
{
#ifdef Q_OS_WIN
    // See the class documentation: QMessageBox::showEvent() crashes when the
    // platform has no native interface.
    const bool messageBoxGuard = !QGuiApplication::platformNativeInterface();
#else
    const bool messageBoxGuard = false;
#endif
    // offscreen/minimal never activate a window; everything focus-related is
    // dead until one is activated synthetically.
    const QString platform = QGuiApplication::platformName();
    const bool windowActivation =
#ifdef QTMCP_HAVE_WINDOW_ACTIVATION
        platform == QLatin1String("offscreen") || platform == QLatin1String("minimal");
#else
        false;
#endif

    if (!messageBoxGuard && !windowActivation)
        return nullptr;

    auto *guard = new HeadlessCompat(windowActivation, messageBoxGuard, parent);
    QCoreApplication::instance()->installEventFilter(guard);
    qInfo("QtMcp: headless platform '%s' detected; installed workarounds:%s%s",
          qPrintable(platform),
          windowActivation ? " window-activation" : "",
          messageBoxGuard ? " messagebox-crash-guard" : "");
    return guard;
}

QAbstractButton *HeadlessCompat::detectEscapeButton(const QMessageBox *box)
{
    // Mirrors QMessageBoxPrivate::detectEscapeButton() (Qt 5.15.2), except the
    // detailsButton two-button rule, which needs private access; that
    // combination simply falls through to the role-based rules.
    if (box->escapeButton())
        return box->escapeButton();
    if (QAbstractButton *cancel = box->button(QMessageBox::Cancel))
        return cancel;
    const QList<QAbstractButton *> buttons = box->buttons();
    if (buttons.count() == 1)
        return buttons.first();

    QAbstractButton *detected = nullptr;
    for (QAbstractButton *button : buttons) {
        if (box->buttonRole(button) == QMessageBox::RejectRole) {
            if (detected)
                return nullptr; // ambiguous, like Qt
            detected = button;
        }
    }
    if (detected)
        return detected;
    for (QAbstractButton *button : buttons) {
        if (box->buttonRole(button) == QMessageBox::NoRole) {
            if (detected)
                return nullptr;
            detected = button;
        }
    }
    return detected;
}

bool HeadlessCompat::eventFilter(QObject *watched, QEvent *event)
{
    // --- workaround 1: activate the first shown top-level window ---------
#ifdef QTMCP_HAVE_WINDOW_ACTIVATION
    if (m_windowActivation && event->type() == QEvent::Show) {
        if (auto *window = qobject_cast<QWindow *>(watched)) {
            // Only when nothing is active yet: this bootstraps Qt's focus
            // machinery for the main window. Windows shown later (dialogs)
            // are reached with explicit refs, so leave focus alone then.
            if (!window->transientParent() && !QGuiApplication::focusWindow())
                QWindowSystemInterface::handleWindowActivated(window);
        }
        // Fall through: a QWidget may also need workaround 2 below.
    }
    if (m_windowActivation && event->type() == QEvent::Hide) {
        if (auto *window = qobject_cast<QWindow *>(watched)) {
            // The offscreen platform activates every window on show (modal
            // dialogs included) but activates nothing when they hide — so
            // after a modal closes, no window is active and the whole focus
            // machinery is dead. Mimic a real window manager: when the active
            // transient window goes away, re-activate its parent.
            if (window->transientParent() && QGuiApplication::focusWindow() == window
                && window->transientParent()->isVisible())
                QWindowSystemInterface::handleWindowActivated(window->transientParent());
        }
    }
#endif

    // --- workaround 2: QMessageBox crash guard (Windows, no native iface) --
    if (!m_messageBoxGuard)
        return false;
    auto *box = qobject_cast<QMessageBox *>(watched);
    if (!box)
        return false;

    switch (event->type()) {
    case QEvent::Show: {
        // Swallow before QMessageBox::showEvent() can crash. Replicate its
        // observable behavior via public API:
        // - autoAddOkButton: a box with no buttons at all gets an Ok button.
        if (box->buttons().isEmpty())
            box->addButton(QMessageBox::Ok);
        // - detectEscapeButton(): cache the result for the Close-event path
        //   below (the private detectedEscapeButton member Qt would normally
        //   arm is unreachable from outside).
        box->setProperty(ESCAPE_PROP,
                         QVariant::fromValue(QPointer<QAbstractButton>(detectEscapeButton(box))));
        box->setProperty(GUARDED_PROP, true);
        // - updateSize(): QMessageBox::event() runs it on LayoutRequest, so
        //   posting one triggers the same code path through Qt itself.
        QCoreApplication::postEvent(box, new QEvent(QEvent::LayoutRequest));
        // - QDialog::showEvent() only repositions the dialog; meaningless on
        //   headless platforms. The accessibility Alert event is likewise a
        //   no-op for automation and is skipped.
        return true;
    }
    case QEvent::Close: {
        if (!box->property(GUARDED_PROP).toBool())
            return false; // not shown under the guard; leave Qt's path alone
        // QMessageBox::closeEvent() would consult d->detectedEscapeButton,
        // which only the swallowed showEvent could have set. Emulate it: with
        // no escape button the close is ignored; with one, Qt activates it —
        // via click(), the same path Qt's own Esc-key handling uses.
        event->ignore();
        box->setProperty(GUARDED_PROP, QVariant());
        const auto escape =
            box->property(ESCAPE_PROP).value<QPointer<QAbstractButton>>();
        box->setProperty(ESCAPE_PROP, QVariant());
        if (escape)
            escape->click();
        return true;
    }
    default:
        return false;
    }
}

} // namespace QtMcp

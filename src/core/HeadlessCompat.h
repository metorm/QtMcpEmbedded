#ifndef QTMCP_HEADLESSCOMPAT_H
#define QTMCP_HEADLESSCOMPAT_H

#include <QObject>

class QAbstractButton;
class QMessageBox;

namespace QtMcp {

/// Workarounds for Qt headless-platform (offscreen/minimal) quirks that break
/// automated driving of real applications. Installed as an application event
/// filter at probe assembly time; each workaround activates only on the
/// platforms that need it, so normal desktop runs are completely unaffected.
///
/// 1) Window activation (offscreen/minimal, all OSes):
///    These platforms never mark any window active, so QWidget::setFocus()
///    silently does nothing (it requires isActiveWindow()) and
///    QApplication::focusWidget() stays null forever — keyboard focus driven
///    automation (ref-less qt_key_press, delegate editors grabbing focus,
///    F2-to-edit flows, ...) collapses. The guard watches for the first
///    top-level QWindow being shown and activates it via
///    QWindowSystemInterface::handleWindowActivated(), after which Qt's
///    normal focus machinery works.
///
/// 2) QMessageBox crash guard (Windows + platform without native interface):
///    QMessageBox::showEvent() unconditionally calls
///    qt_getWindowsSystemMenu(), which dereferences
///    QGuiApplication::platformNativeInterface() without a null check.
///    Platforms without a native interface (offscreen, minimal) return
///    nullptr there, so any QMessageBox static function
///    (warning()/question()/...) segfaults while the dialog is being shown.
///    The guard swallows the Show event targeted at QMessageBox (before the
///    crashing showEvent runs) and replicates its observable behavior via
///    public API, minus the cosmetic Windows system-menu tweak
///    (EnableMenuItem SC_CLOSE), which cannot run without a native window
///    handle anyway. It also emulates the Close-event escape-button semantics
///    that the swallowed showEvent would have armed.
class HeadlessCompat : public QObject
{
public:
    /// Installs the guard on QApplication when any workaround applies to the
    /// current platform; returns nullptr when none is needed.
    static HeadlessCompat *installIfNeeded(QObject *parent);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    HeadlessCompat(bool windowActivation, bool messageBoxGuard, QObject *parent);

    /// Public-API replication of QMessageBoxPrivate::detectEscapeButton().
    static QAbstractButton *detectEscapeButton(const QMessageBox *box);

    bool m_windowActivation;
    bool m_messageBoxGuard;
};

} // namespace QtMcp

#endif // QTMCP_HEADLESSCOMPAT_H

#include "Screenshotter.h"

#include <QApplication>
#include <QBuffer>
#include <QJsonArray>
#include <QPixmap>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include "../core/RefRegistry.h"
#include "../core/ToolError.h"
#include "../protocol/ToolRegistry.h"

namespace QtMcp {

Screenshotter::Screenshotter(RefRegistry &registry)
    : m_registry(registry)
{
}

void Screenshotter::registerTools(ToolRegistry &registry)
{
    const QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("ref"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("description"),
                           QStringLiteral("Widget ref from qt_snapshot (e.g. 'w5'). "
                                          "If omitted, captures the active window.")}}},
             {QStringLiteral("full_window"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                          {QStringLiteral("default"), false},
                          {QStringLiteral("description"),
                           QStringLiteral("Capture the first visible top-level window.")}}},
             {QStringLiteral("max_width"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                          {QStringLiteral("default"), 1920}}},
             {QStringLiteral("max_height"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                          {QStringLiteral("default"), 1080}}},
             {QStringLiteral("format"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("enum"),
                           QJsonArray{QStringLiteral("png"), QStringLiteral("jpeg")}},
                          {QStringLiteral("default"), QStringLiteral("png")}}},
             {QStringLiteral("quality"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                          {QStringLiteral("default"), 80},
                          {QStringLiteral("description"),
                           QStringLiteral("JPEG quality 1-100 (ignored for PNG).")}}},
         }},
    };

    registry.registerTool(
        QStringLiteral("qt_screenshot"),
        QStringLiteral("Take a screenshot of a widget or the active window. Returns a "
                       "base64-encoded image (PNG by default)."),
        schema, [this](const QJsonObject &args) {
            const QJsonObject data = screenshot(
                args.value(QStringLiteral("ref")).toString(),
                args.value(QStringLiteral("full_window")).toBool(false),
                args.value(QStringLiteral("max_width")).toInt(1920),
                args.value(QStringLiteral("max_height")).toInt(1080),
                args.value(QStringLiteral("format")).toString(QStringLiteral("png")),
                args.value(QStringLiteral("quality")).toInt(80));

            ToolResult result;
            result.data = data;
            result.isImage = true;
            result.imageBase64 = data.value(QStringLiteral("image")).toString().toLatin1();
            result.mimeType = data.value(QStringLiteral("format")).toString() == QLatin1String("jpeg")
                                  ? QStringLiteral("image/jpeg")
                                  : QStringLiteral("image/png");
            result.imageText = QStringLiteral("Size: %1x%2")
                                   .arg(data.value(QStringLiteral("width")).toInt())
                                   .arg(data.value(QStringLiteral("height")).toInt());
            return result;
        });
}

QJsonObject Screenshotter::screenshot(const QString &ref, bool fullWindow, int maxWidth,
                                      int maxHeight, const QString &format, int quality)
{
    QWidget *widget = nullptr;

    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        throw ToolError(QStringLiteral("No QApplication running"));

    if (!ref.isEmpty()) {
        QObject *obj = m_registry.resolveOrThrow(ref);
        widget = qobject_cast<QWidget *>(obj);
        if (!widget)
            throw ToolError(QStringLiteral("Ref %1 is not a QWidget").arg(ref));
    } else if (fullWindow) {
        const QWidgetList windows = app->topLevelWidgets();
        for (QWidget *w : windows) {
            if (w->isVisible()) {
                widget = w;
                break;
            }
        }
        if (!widget)
            throw ToolError(QStringLiteral("No visible top-level window"));
    } else {
        widget = app->activeWindow();
        if (!widget) {
            const QWidgetList windows = app->topLevelWidgets();
            for (QWidget *w : windows) {
                if (w->isVisible()) {
                    widget = w;
                    break;
                }
            }
        }
        if (!widget)
            throw ToolError(QStringLiteral("No visible window"));
    }

    // Capture path: prefer QScreen::grabWindow() (real screen/backing-store
    // pixels) and clip to the widget rect. Plain QWidget::grab() re-renders
    // the widget into a fresh pixmap, and on some Windows/Qt5 setups that
    // re-render loses all text glyphs (controls draw fine, text is blank) —
    // grabWindow does not have that problem. Fall back to grab() when the
    // screen path is unavailable (e.g. unsupported by the platform plugin).
    QPixmap pixmap;
    QWidget *native = widget->window();
    QScreen *screen = native->windowHandle() ? native->windowHandle()->screen() : nullptr;
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QPixmap full = screen->grabWindow(native->winId());
        if (!full.isNull()) {
            const qreal dpr = full.devicePixelRatioF();
            const QPointF topLeft = QPointF(widget->mapTo(native, QPoint(0, 0))) * dpr;
            pixmap = full.copy(QRectF(topLeft, QSizeF(widget->size()) * dpr).toRect());
            pixmap.setDevicePixelRatio(1.0);
        }
    }
    if (pixmap.isNull())
        pixmap = widget->grab();
    if (pixmap.isNull())
        throw ToolError(QStringLiteral("grab() returned null pixmap"));

    if (pixmap.width() > maxWidth || pixmap.height() > maxHeight) {
        pixmap = pixmap.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QString fmt = format.toUpper();
    if (fmt != QLatin1String("PNG") && fmt != QLatin1String("JPEG"))
        fmt = QStringLiteral("PNG");

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (fmt == QLatin1String("JPEG"))
        pixmap.save(&buffer, "JPEG", quality);
    else
        pixmap.save(&buffer, "PNG");
    const QByteArray bytes = buffer.data();
    buffer.close();

    return QJsonObject{
        {QStringLiteral("image"), QString::fromLatin1(bytes.toBase64())},
        {QStringLiteral("width"), pixmap.width()},
        {QStringLiteral("height"), pixmap.height()},
        {QStringLiteral("format"), fmt.toLower()},
    };
}

} // namespace QtMcp

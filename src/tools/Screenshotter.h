#ifndef QTMCP_SCREENSHOTTER_H
#define QTMCP_SCREENSHOTTER_H

#include <QJsonObject>
#include <QString>

namespace QtMcp {

class RefRegistry;
class ToolRegistry;

/// QWidget::grab() -> PNG/JPEG -> base64, mirroring qt-mcp's screenshotter.py.
class Screenshotter
{
public:
    explicit Screenshotter(RefRegistry &registry);

    void registerTools(ToolRegistry &registry);

    /// Returns {image: base64, width, height, format}.
    /// Throws ToolError on failure.
    QJsonObject screenshot(const QString &ref, bool fullWindow, int maxWidth, int maxHeight,
                           const QString &format, int quality);

private:
    RefRegistry &m_registry;
};

} // namespace QtMcp

#endif // QTMCP_SCREENSHOTTER_H

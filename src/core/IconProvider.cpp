#include "IconProvider.h"
#include <QIcon>
#include <QFile>
#include <QDirIterator>
#include <QStandardPaths>
#include <QUrl>
namespace Alure {
IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) {
    if (id.startsWith("app/")) {
        auto appId = QUrl::fromPercentEncoding(id.section('/', 1, 1).toUtf8());
        if (appId.endsWith(".desktop")) appId.chop(8);
        const auto fallbackName = id.section('/', 2, 2);
        QIcon icon;
        // Match desktop-file IDs, not Exec commands; never launch applications.
        if (!appId.isEmpty() && !appId.contains('/') && !appId.contains(QChar::Null)) {
            for (const auto &directory : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
                QString desktop = directory + '/' + appId + ".desktop";
                if (!QFile::exists(desktop)) {
                    QDirIterator entries(directory, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
                    while (entries.hasNext()) {
                        const auto candidate = entries.next();
                        auto desktopId = QDir(directory).relativeFilePath(candidate); desktopId.replace('/', '-');
                        if (desktopId == appId + ".desktop") { desktop = candidate; break; }
                    }
                }
                QFile file(desktop);
                if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) continue;
                bool group = false;
                for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
                    const auto text = line.trimmed();
                    if (text.startsWith('[')) group = text == "[Desktop Entry]";
                    if (!group || !text.startsWith("Icon=")) continue;
                    auto name = text.mid(5).trimmed();
                    name.replace("\\s", " "); name.replace("\\\\", "\\");
                    icon = QDir::isAbsolutePath(name) ? QIcon(name) : QIcon::fromTheme(name);
                    break;
                }
                break; // User desktop entries override system entries, including missing icons.
            }
            if (icon.isNull()) icon = QIcon::fromTheme(appId);
        }
        QSize fallbackSize;
        auto fallback = requestPixmap("builtin/" + fallbackName, &fallbackSize, requestedSize);
        const QSize target(requestedSize.width() > 0 ? qBound(1, requestedSize.width(), 512) : 32,
                           requestedSize.height() > 0 ? qBound(1, requestedSize.height(), 512) : 32);
        auto pixmap = icon.isNull() ? fallback : icon.pixmap(target);
        if (pixmap.isNull()) pixmap = fallback;
        if (size) *size = pixmap.size();
        return pixmap;
    }
    const auto name = id.section('/', 1);
    const QString candidate = ":/assets/icons/" + name + ".svg";
    const QString fallback = !name.contains('/') && QFile::exists(candidate) ? candidate : ":/assets/icons/fallback.svg";
    const QIcon icon = id.startsWith("theme/") ? QIcon::fromTheme(name, QIcon(fallback)) : QIcon(fallback);
    const QSize target(requestedSize.width() > 0 ? qBound(1, requestedSize.width(), 512) : 32,
                       requestedSize.height() > 0 ? qBound(1, requestedSize.height(), 512) : 32);
    auto pixmap = icon.pixmap(target);
    if (size) *size = pixmap.size();
    return pixmap;
}
}

#include "IconProvider.h"
#include <QIcon>
#include <QFile>
namespace Alure {
IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) {
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

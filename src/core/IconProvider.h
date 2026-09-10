#pragma once
#include <QQuickImageProvider>
namespace Alure {
// image://icons/builtin/alure or image://icons/theme/<freedesktop-icon-name>.
class IconProvider final : public QQuickImageProvider {
public:
    IconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
}

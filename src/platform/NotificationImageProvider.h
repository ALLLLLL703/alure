#pragma once
#include "NotificationService.h"
#include <QPointer>
#include <QQuickImageProvider>
namespace Alure {
class NotificationImageProvider final : public QQuickImageProvider {
public:
    explicit NotificationImageProvider(NotificationService *service) : QQuickImageProvider(Image), m_service(service) {}
    QImage requestImage(const QString &key, QSize *size, const QSize &) override {
        const auto image = m_service ? m_service->image(key) : QImage{};
        if (size) *size = image.size();
        return image;
    }
private:
    QPointer<NotificationService> m_service;
};
}

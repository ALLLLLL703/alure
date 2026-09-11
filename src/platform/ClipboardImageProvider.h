#pragma once
#include "ClipboardService.h"
#include <QPointer>
#include <QQuickImageProvider>
namespace Alure {
class ClipboardImageProvider final : public QQuickImageProvider {
public:
    explicit ClipboardImageProvider(ClipboardService *service) : QQuickImageProvider(Image), m_service(service) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        const auto image = m_service ? m_service->previewImage(id.section('/', 0, 0)) : QImage{};
        if (size) *size = image.size();
        return image;
    }
private:
    QPointer<ClipboardService> m_service; // synchronous provider; service outlives its QML engine
};
}

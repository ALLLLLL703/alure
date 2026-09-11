#pragma once
#include "Service.h"
#include <optional>

namespace Alure {
// Linux backlight/keyboard LED snapshots; one writer and one latest pending input.
class BrightnessService final : public Service {
    Q_OBJECT
    Q_PROPERTY(bool adjusting READ adjusting NOTIFY changed)
public:
    explicit BrightnessService(QObject *parent = nullptr);
    ~BrightnessService() override;
    bool adjusting() const { return m_pending.has_value() || m_dispatched.has_value(); }
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &name, const QVariantMap &args) override;
    bool canQueueAction(const QString &name) const override;
private:
    struct Target { QString kind, device; int level, maximum; };
    QVariantMap readDevice(const QString &kind) const;
    QStringList command() const;
    void dispatch();
    CommandRunner m_runner;
    QTimer m_debounce;
    std::optional<Target> m_pending, m_dispatched;
    QString m_actionError;
};
}

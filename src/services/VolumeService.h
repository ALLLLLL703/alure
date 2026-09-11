#pragma once
#include "Service.h"
#include <QSet>
#include <QElapsedTimer>
#include <optional>

namespace Alure {
class VolumeService final : public Service {
    Q_OBJECT
    Q_PROPERTY(bool adjusting READ adjusting NOTIFY changed)
    // A requested target, never a hardware snapshot; invalid after readback/failure.
    Q_PROPERTY(QVariant pendingPercent READ pendingPercent NOTIFY changed)
public:
    explicit VolumeService(QObject *parent = nullptr);
    ~VolumeService() override;
    bool adjusting() const { return m_pending.has_value() || m_dispatched.has_value(); }
    QVariant pendingPercent() const {
        const auto &target = m_pending ? m_pending : m_dispatched;
        return target ? QVariant(target->percent) : QVariant{};
    }
    static bool parsePulse(const QByteArray &info, const QByteArray &sinks, QVariantMap &state, QString &error);
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &name, const QVariantMap &args) override;
    bool canQueueAction(const QString &name) const override;
private:
    enum class Backend { PipeWire, PulseAudio };
    enum class Phase { Idle, PipeWire, PulseInfo, PulseSinks, Action };
    struct PendingVolume { double percent; Backend backend; QString sink; };
    QStringList command(const char *key) const;
    const char *volumeKey() const;
    const char *muteKey() const;
    void probe(Backend backend);
    void read(const char *key, Phase phase);
    void readFailed(const QString &error);
    void publishVolume(QVariantMap state);
    bool executeAction(QStringList command);
    void scheduleVolume();
    void dispatchVolume();
    CommandRunner m_runner;
    QTimer m_debounce;
    QElapsedTimer m_lastDispatch;
    // Bound observed-feedback/sink-detection starvation without reading after
    // every queued setter. Commands and reads remain strictly serialized.
    static constexpr int maximumWritesBeforeReadback = 2;
    int m_writesSinceReadback = 0;
    Backend m_active = Backend::PipeWire, m_probe = Backend::PipeWire;
    Phase m_phase = Phase::Idle;
    QSet<Backend> m_attempted;
    QStringList m_errors;
    QString m_sink;
    QByteArray m_pulseInfo;
    std::optional<PendingVolume> m_pending, m_dispatched;
};
}

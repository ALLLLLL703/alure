#pragma once
#include <QObject>
#include <QVariantMap>

namespace Alure {
// Observed changes only: busy/action signals never count as new hardware data.
class OsdController final : public QObject {
    Q_OBJECT
public:
    explicit OsdController(QObject *parent = nullptr) : QObject(parent) {}
    void configure(const QVariantMap &options);
    void observeVolume(bool available, const QVariantMap &state);
    void observeBrightness(const QVariantMap &state);
signals:
    void requested(const QVariantMap &snapshot);
    void reset();
private:
    void observe(const QString &kind, const QString &identity, const QVariantMap &value);
    QVariantMap m_options;
    QMap<QString, QPair<QString, QVariantMap>> m_baselines;
};
}

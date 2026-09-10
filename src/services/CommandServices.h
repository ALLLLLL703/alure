#pragma once
#include "Service.h"
namespace Alure {
class CommandService : public Service {
    Q_OBJECT
public:
    enum Kind { Workspaces, Volume, Updates, Wifi };
    CommandService(Kind kind, QObject *parent = nullptr);
    static bool parse(Kind kind, const QByteArray &output, int exitCode, QVariantMap &state, QVariantList &items, QString &error);
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    bool execute(const QStringList &argv, bool action);
    Kind m_kind;
    CommandRunner m_runner;
    QTimer m_debounce;
    double m_pendingVolume = 0;
    bool m_action = false;
    int m_wifiPhase = 0;
    QVariantMap m_wifiState;
    QVariantList m_wifiItems;
};
class BatteryService : public Service {
    Q_OBJECT
public:
    using Service::Service;
protected:
    void poll() override;
};
}

#pragma once
#include <QObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>

namespace Alure {
// One editor and one toggleable clipboard per session bus, independent of config path.
class SingleInstance final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.alure.Instance")
public:
    explicit SingleInstance(QString service) : m_service(std::move(service)) {}
    ~SingleInstance() override {
        if (m_owner) {
            auto bus = QDBusConnection::sessionBus();
            bus.unregisterService(m_service); bus.unregisterObject("/alure/instance");
        }
    }
    // 0 = owner, 1 = existing instance activated, -1 = unavailable IPC.
    int acquire() {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) { qWarning() << "Single-instance mode requires the session bus."; return -1; }
        if (bus.registerService(m_service)) {
            m_owner = true;
            if (bus.registerObject("/alure/instance", this, QDBusConnection::ExportScriptableSlots)) return 0;
            qWarning() << "Could not export instance endpoint."; return -1;
        }
        const auto reply = bus.call(QDBusMessage::createMethodCall(m_service, "/alure/instance", "org.alure.Instance", "Activate"), QDBus::Block, 3000);
        if (reply.type() == QDBusMessage::ErrorMessage) { qWarning() << "Could not reach existing instance:" << reply.errorMessage(); return -1; }
        return 1;
    }
public slots:
    Q_SCRIPTABLE void Activate() { emit activated(); }
signals:
    void activated();
private:
    QString m_service;
    bool m_owner = false;
};
}

#pragma once
#include "Service.h"
#include <QDBusContext>
namespace Alure {
class NotificationService;
class NotificationEndpoint : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationEndpoint(NotificationService *service);
public slots:
    Q_SCRIPTABLE QStringList GetCapabilities();
    Q_SCRIPTABLE QString GetServerInformation(QString &vendor, QString &version, QString &specVersion);
    Q_SCRIPTABLE uint Notify(const QString &appName, uint replacesId, const QString &appIcon, const QString &summary,
                            const QString &body, const QStringList &actions, const QVariantMap &hints, int expireTimeout);
    Q_SCRIPTABLE void CloseNotification(uint id);
signals:
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);
    Q_SCRIPTABLE void ActionInvoked(uint id, const QString &actionKey);
private:
    NotificationService *m_service;
};
class NotificationService : public Service {
    Q_OBJECT
public:
    explicit NotificationService(QObject *parent = nullptr);
    ~NotificationService() override;
    void configure(const QVariantMap &module) override;
    uint notify(const QString &sender, const QString &appName, uint replacesId, const QString &icon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int expireTimeout);
    void close(uint id, uint reason);
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    void update();
    NotificationEndpoint m_endpoint;
    QVariantList m_history;
    uint m_nextId = 1;
    bool m_owned = false, m_dnd = false;
};
}

#pragma once
#include "Service.h"
#include <QDBusContext>
#include <QImage>
#include <QCache>
#include <QHash>
#include <QSet>
#include <QLocalSocket>
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
    QImage image(const QString &key) const { const auto *value = m_images.object(key); return value ? *value : QImage{}; }
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    void update();
    void initPresentation();
    QString iconSource(uint id, const QString &icon, const QVariantMap &hints);
    void focusSender(const QVariantMap &notification);
    void requestFocus(const QByteArray &payload);
    QCache<QString, QImage> m_images{8192};
    QHash<QString, uint> m_senderPids;
    QSet<QString> m_pidRequests;
    quint64 m_imageRevision = 0;
    QLocalSocket m_focusSocket;
    QTimer m_focusDeadline;
    QByteArray m_focusResponse, m_focusPayload;
    QVariantMap m_focusNotification;
    bool m_focusing = false;
    NotificationEndpoint m_endpoint;
    QVariantList m_history;
    uint m_nextId = 1;
    bool m_owned = false, m_dnd = false;
};
}

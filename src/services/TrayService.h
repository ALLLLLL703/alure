#pragma once
#include "Service.h"
#include "TrayMenu.h"
#include <QDBusContext>
namespace Alure {
class TrayService;
class TrayWatcher : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)
public:
    explicit TrayWatcher(TrayService *service);
    QStringList registeredItems() const { return m_items; }
    bool hostRegistered() const { return true; }
    int protocolVersion() const { return 0; }
    void add(const QString &id);
    void clear();
public slots:
    Q_SCRIPTABLE void RegisterStatusNotifierItem(const QString &service);
    Q_SCRIPTABLE void RegisterStatusNotifierHost(const QString &service);
    void ownerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
signals:
    Q_SCRIPTABLE void StatusNotifierItemRegistered(const QString &service);
    Q_SCRIPTABLE void StatusNotifierItemUnregistered(const QString &service);
    Q_SCRIPTABLE void StatusNotifierHostRegistered();
private:
    TrayService *m_service;
    QStringList m_items;
};
class TrayService : public Service {
    Q_OBJECT
    Q_PROPERTY(Alure::TrayMenu *menu READ menu CONSTANT)
public:
    enum class WatcherPolicy { OwnIfAbsent, ObserveOnly };
    explicit TrayService(QObject *parent = nullptr, WatcherPolicy policy = WatcherPolicy::OwnIfAbsent);
    ~TrayService() override;
    void registerItem(const QString &service, const QString &sender);
    TrayMenu *menu() { return &m_menu; }
    Q_INVOKABLE void openMenu(const QString &id);
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    void readItems(QStringList ids, QVariantList rows);
    const WatcherPolicy m_policy;
    TrayMenu m_menu;
    TrayWatcher m_watcher;
    bool m_ownsWatcher = false, m_hostRegistered = false;
    QString m_hostName;
};
}

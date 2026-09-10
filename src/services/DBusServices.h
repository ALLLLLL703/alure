#pragma once
#include "Service.h"
#include <QDBusObjectPath>
namespace Alure {
using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;
class MediaService : public Service {
    Q_OBJECT
public:
    using Service::Service;
protected:
    void poll() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    void readPlayers(QStringList names, QVariantList rows);
};
class BluetoothService : public Service {
    Q_OBJECT
public:
    explicit BluetoothService(QObject *parent = nullptr);
protected:
    void poll() override;
    bool act(const QString &, const QVariantMap &) override;
};
}
Q_DECLARE_METATYPE(Alure::InterfaceMap)
Q_DECLARE_METATYPE(Alure::ObjectMap)

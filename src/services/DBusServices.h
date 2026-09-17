#pragma once
#include "Service.h"
#include <QDBusObjectPath>
namespace Alure {
using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;
class MediaService : public Service {
    Q_OBJECT
public:
    explicit MediaService(QObject *parent = nullptr) : Service(parent, false) {}
protected:
    void poll() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    void readPlayers(QStringList names, QVariantList rows);
    void readPlayer(const QString &name, QStringList names, QVariantList rows, const QString &identity);
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

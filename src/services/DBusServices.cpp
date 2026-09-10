#include "DBusServices.h"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusVariant>
namespace Alure {
void MediaService::poll() {
    setBusy(true);
    call(QDBusConnection::sessionBus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", {},
         [this](const QVariantList &args) {
        QStringList names;
        const auto all = qdbus_cast<QStringList>(args.value(0));
        for (const auto &name : all) if (name.startsWith("org.mpris.MediaPlayer2.")) names << name;
        names.sort(); readPlayers(names.mid(0, 64), {});
    });
}
void MediaService::readPlayers(QStringList names, QVariantList rows) {
    if (names.isEmpty()) {
        setBusy(false);
        if (rows.isEmpty()) fail("No MPRIS players"); else publish({{"count", rows.size()}}, rows);
        return;
    }
    const auto name = names.takeFirst();
    properties(QDBusConnection::sessionBus(), name, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
               [this, name, names, rows](QVariantMap props) mutable {
        const auto metadata = dbusMap(unbox(props.value("Metadata")));
        QVariantMap row{{"service", name}, {"title", metadata.value("xesam:title")}, {"artist", qdbus_cast<QStringList>(metadata.value("xesam:artist"))},
                        {"album", metadata.value("xesam:album")}, {"artUrl", metadata.value("mpris:artUrl")},
                        {"playbackStatus", props.value("PlaybackStatus")}};
        for (const auto &key : {"CanControl", "CanPlay", "CanPause", "CanGoNext", "CanGoPrevious"}) row[key] = props.value(key).toBool();
        rows << row; readPlayers(names, rows);
    });
}
bool MediaService::act(const QString &name, const QVariantMap &args) {
    QString method, capability;
    if (name == "playPause") { method = "PlayPause"; }
    else if (name == "next") { method = "Next"; capability = "CanGoNext"; }
    else if (name == "previous") { method = "Previous"; capability = "CanGoPrevious"; }
    else return false;
    for (const auto &entry : items()) {
        const auto row = entry.toMap();
        if (row.value("service") != args.value("service") || !row.value("CanControl").toBool()) continue;
        if (name == "playPause") capability = row.value("playbackStatus") == "Playing" ? "CanPause" : "CanPlay";
        if (!row.value(capability).toBool()) return false;
        return dbusAction(QDBusConnection::sessionBus(), row.value("service").toString(), "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", method);
    }
    return false;
}
BluetoothService::BluetoothService(QObject *parent) : Service(parent) {
    qDBusRegisterMetaType<InterfaceMap>(); qDBusRegisterMetaType<ObjectMap>();
}
void BluetoothService::poll() {
    setBusy(true);
    call(QDBusConnection::systemBus(), "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", {}, [this](const QVariantList &args) {
        const auto objects = qdbus_cast<ObjectMap>(args.value(0));
        QVariantList rows, adapters; int connected = 0;
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            if (it.value().contains("org.bluez.Adapter1")) {
                const auto props = it.value().value("org.bluez.Adapter1");
                QVariantMap row{{"path", it.key().path()}};
                for (const auto &key : {"Powered", "Discovering", "Alias", "Name", "Address"}) row[key] = props.value(key);
                if (adapters.size() < 32) adapters << row;
            }
            if (it.value().contains("org.bluez.Device1")) {
                const auto props = it.value().value("org.bluez.Device1");
                QVariantMap row{{"path", it.key().path()}, {"Adapter", qvariant_cast<QDBusObjectPath>(props.value("Adapter")).path()}};
                for (const auto &key : {"Connected", "Paired", "Trusted", "Alias", "Name", "Address", "Icon"}) row[key] = props.value(key);
                if (rows.size() < 256) { connected += row.value("Connected").toBool(); rows << row; }
            }
        }
        setBusy(false);
        if (adapters.isEmpty()) fail("No BlueZ adapter");
        else publish({{"adapters", adapters}, {"connectedCount", connected}}, rows);
    });
}
bool BluetoothService::act(const QString &name, const QVariantMap &args) {
    const auto path = args.value("path").toString();
    if (name == "setPowered") {
        if (args.value("powered").metaType().id() != QMetaType::Bool) return false;
        for (const auto &adapter : state().value("adapters").toList()) {
            if (adapter.toMap().value("path").toString() != path) continue;
            return dbusAction(QDBusConnection::systemBus(), "org.bluez", path, "org.freedesktop.DBus.Properties", "Set",
                              {"org.bluez.Adapter1", "Powered", QVariant::fromValue(QDBusVariant(args.value("powered")))});
        }
    } else if (name == "connect" || name == "disconnect") {
        for (const auto &entry : items()) {
            const auto row = entry.toMap();
            if (row.value("path").toString() != path) continue;
            // Pairing/agent and trust changes are intentionally outside this host.
            if (name == "connect" && !row.value("Paired").toBool()) return false;
            return dbusAction(QDBusConnection::systemBus(), "org.bluez", path, "org.bluez.Device1", name == "connect" ? "Connect" : "Disconnect");
        }
    }
    return false;
}
}

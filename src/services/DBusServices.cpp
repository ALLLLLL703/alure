#include "DBusServices.h"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <algorithm>
#include <cmath>
#include <tuple>
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
        const auto preferred = m_options.value("preferred_player").toString();
        const auto rank = [&preferred](const QVariant &entry) {
            const auto row = entry.toMap(); const auto name = row.value("service").toString();
            const auto status = row.value("playbackStatus").toString();
            const bool match = !preferred.isEmpty() && (name == preferred || name.startsWith(preferred + '.'));
            return std::tuple{!match, status == "Playing" ? 0 : status == "Paused" ? 1 : 2, name};
        };
        std::stable_sort(rows.begin(), rows.end(), [&rank](const auto &a, const auto &b) { return rank(a) < rank(b); });
        if (rows.isEmpty()) fail("No readable MPRIS players"); else publish({{"count", rows.size()}}, rows);
        return;
    }
    const auto name = names.takeFirst();
    const auto fallback = name.section('.', 3).section(".instance", 0, 0);
    call(QDBusConnection::sessionBus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner", {name},
         [this, name, names, rows, fallback](const QVariantList &args) {
        const auto owner = args.value(0).toString();
        if (owner.isEmpty()) { readPlayers(names, rows); return; }
        properties(QDBusConnection::sessionBus(), owner, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2",
                   [this, name, owner, names, rows, fallback](const QVariantMap &props) {
            readPlayer(name, owner, names, rows, props.value("Identity", fallback).toString());
        }, [this, name, owner, names, rows, fallback](const QString &) {
            readPlayer(name, owner, names, rows, fallback);
        });
    }, [this, names, rows](const QString &) { readPlayers(names, rows); });
}
void MediaService::readPlayer(const QString &name, const QString &owner, QStringList names, QVariantList rows, const QString &identity) {
    properties(QDBusConnection::sessionBus(), owner, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
               [this, name, owner, names, rows, identity](QVariantMap props) mutable {
        if (!props.contains("PlaybackStatus")) { readPlayers(names, rows); return; }
        const auto metadata = dbusMap(unbox(props.value("Metadata")));
        QVariantMap row{{"service", name}, {"owner", owner}, {"identity", identity},
                        {"title", metadata.value("xesam:title")}, {"artist", qdbus_cast<QStringList>(metadata.value("xesam:artist"))},
                        {"album", metadata.value("xesam:album")}, {"artUrl", metadata.value("mpris:artUrl")},
                        {"trackId", qvariant_cast<QDBusObjectPath>(unbox(metadata.value("mpris:trackid"))).path()},
                        {"lengthUs", qMax<qint64>(0, metadata.value("mpris:length").toLongLong())},
                        {"positionUs", qMax<qint64>(0, props.value("Position").toLongLong())},
                        {"shuffle", props.value("Shuffle").toBool()}, {"loopStatus", props.value("LoopStatus").toString()},
                        {"hasShuffle", props.contains("Shuffle")}, {"hasLoopStatus", props.contains("LoopStatus")},
                        {"playbackStatus", props.value("PlaybackStatus")}};
        for (const auto &key : {"CanControl", "CanPlay", "CanPause", "CanGoNext", "CanGoPrevious", "CanSeek"}) row[key] = props.value(key).toBool();
        rows << row; readPlayers(names, rows);
    }, [this, names, rows](const QString &) { readPlayers(names, rows); });
}
bool MediaService::act(const QString &name, const QVariantMap &args) {
    QString method, capability;
    if (name == "playPause") { method = "PlayPause"; }
    else if (name == "next") { method = "Next"; capability = "CanGoNext"; }
    else if (name == "previous") { method = "Previous"; capability = "CanGoPrevious"; }
    else if (name != "setPosition" && name != "setShuffle" && name != "setLoopStatus") return false;
    for (const auto &entry : items()) {
        const auto row = entry.toMap();
        if (row.value("service") != args.value("service") || row.value("owner") != args.value("owner") ||
            !row.value("CanControl").toBool()) continue;
        const auto destination = row.value("owner").toString();
        if (name == "setPosition") {
            const auto value = args.value("positionUs");
            if (value.metaType().id() != QMetaType::Double && value.metaType().id() != QMetaType::LongLong && value.metaType().id() != QMetaType::Int) return false;
            const double position = value.toDouble();
            const auto track = row.value("trackId").toString();
            if (!row.value("CanSeek").toBool() || track.isEmpty() || track == "/org/mpris/MediaPlayer2/TrackList/NoTrack" ||
                args.value("trackId").toString() != track || !std::isfinite(position) || position < 0 || position >= row.value("lengthUs").toDouble()) return false;
            return dbusAction(QDBusConnection::sessionBus(), destination, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "SetPosition",
                              {QVariant::fromValue(QDBusObjectPath(track)), QVariant::fromValue(static_cast<qlonglong>(position))});
        }
        if (name == "setShuffle" || name == "setLoopStatus") {
            const bool shuffle = name == "setShuffle";
            const auto value = args.value(shuffle ? "shuffle" : "loopStatus");
            if (shuffle ? !row.value("hasShuffle").toBool() || value.metaType().id() != QMetaType::Bool :
                !row.value("hasLoopStatus").toBool() || !QStringList{"None", "Track", "Playlist"}.contains(value.toString())) return false;
            return dbusAction(QDBusConnection::sessionBus(), destination, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Set",
                              {"org.mpris.MediaPlayer2.Player", shuffle ? "Shuffle" : "LoopStatus", QVariant::fromValue(QDBusVariant(value))});
        }
        if (name == "playPause") capability = row.value("playbackStatus") == "Playing" ? "CanPause" : "CanPlay";
        if (!row.value(capability).toBool()) return false;
        return dbusAction(QDBusConnection::sessionBus(), destination, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", method);
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

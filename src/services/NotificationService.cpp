#include "NotificationService.h"
#include <QDateTime>
#include <QDBusMessage>
#include <algorithm>
namespace Alure {
namespace { constexpr auto name = "org.freedesktop.Notifications"; constexpr auto path = "/org/freedesktop/Notifications"; }
NotificationEndpoint::NotificationEndpoint(NotificationService *service) : m_service(service) {}
QStringList NotificationEndpoint::GetCapabilities() { return {"actions", "body", "icon-static", "persistence"}; }
QString NotificationEndpoint::GetServerInformation(QString &vendor, QString &version, QString &specVersion) {
    vendor = "Alure"; version = "0.1.0"; specVersion = "1.2"; return "Alure";
}
uint NotificationEndpoint::Notify(const QString &appName, uint replacesId, const QString &icon, const QString &summary,
                                  const QString &body, const QStringList &actions, const QVariantMap &hints, int expiry) {
    return m_service->notify(message().service(), appName, replacesId, icon, summary, body, actions, hints, expiry);
}
void NotificationEndpoint::CloseNotification(uint id) { m_service->close(id, 3); }
NotificationService::NotificationService(QObject *parent) : Service(parent), m_endpoint(this) { initPresentation(); }
NotificationService::~NotificationService() { stop(); }
void NotificationService::configure(const QVariantMap &module) {
    const bool configuredDnd = m_options.value("dnd").toBool();
    Service::configure(module);
    m_images.setMaxCost(m_options.value("icon_cache_kib").toInt());
    if (configuredDnd != m_options.value("dnd").toBool()) {
        m_dnd = m_options.value("dnd").toBool();
        if (m_owned) update();
    }
}
void NotificationService::stop() {
    m_focusNotification.clear(); m_focusDeadline.stop(); m_focusSocket.abort();
    m_images.clear(); m_senderPids.clear(); m_pidRequests.clear();
    if (m_owned) {
        const auto history = m_history;
        for (const auto &entry : history) if (entry.toMap().value("active").toBool()) close(entry.toMap().value("id").toUInt(), 3);
        auto bus = QDBusConnection::sessionBus(); bus.unregisterService(name); bus.unregisterObject(path);
    }
    m_owned = false; m_history.clear();
}
void NotificationService::poll() {
    if (!m_options.value("server_enabled").toBool()) { fail("Notification server disabled (opt-in required)"); return; }
    if (!m_owned) {
        auto bus = QDBusConnection::sessionBus();
        setBusy(true);
        call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", {name}, [this, bus](const QVariantList &args) mutable {
            setBusy(false);
            if (args.value(0).toBool()) { fail("Notification server unavailable: name already owned"); return; }
            // Name registration is a one-time broker operation, never replacement or queueing.
            if (!bus.registerService(name)) { fail("Notification server unavailable: registration denied or another owner won the race"); return; }
            m_owned = bus.registerObject(path, &m_endpoint, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
            if (!m_owned) { bus.unregisterService(name); fail("Cannot export notification server"); return; }
            m_dnd = m_options.value("dnd").toBool();
            update();
        });
        return;
    }
    if (!QDBusConnection::sessionBus().isConnected()) { m_owned = false; fail("Session bus disconnected"); return; }
    const auto now = QDateTime::currentMSecsSinceEpoch(); const auto history = m_history;
    for (const auto &entry : history) {
        const auto row = entry.toMap(); const auto expiry = row.value("expiresAt").toLongLong();
        if (row.value("active").toBool() && expiry > 0 && expiry <= now) close(row.value("id").toUInt(), 1);
    }
    update();
}
void NotificationService::update() {
    int active = 0;
    for (const auto &entry : m_history) active += entry.toMap().value("active").toBool();
    publish({{"dnd", m_dnd}, {"count", m_history.size()}, {"activeCount", active}}, m_history);
}
uint NotificationService::notify(const QString &sender, const QString &appName, uint replacesId, const QString &icon, const QString &summary,
                                 const QString &body, const QStringList &actions, const QVariantMap &hints, int expiry) {
    if (!enabled() || !m_owned) return 0;
    int replace = -1;
    for (int i = 0; i < m_history.size(); ++i) {
        const auto row = m_history[i].toMap();
        if (row.value("id").toUInt() == replacesId && row.value("sender").toString() == sender && row.value("active").toBool()) replace = i;
    }
    // Never reuse a live ID on integer wrap.
    uint id = replacesId;
    if (replace < 0) {
        bool used;
        do { id = m_nextId++; used = id == 0; for (const auto &entry : m_history) used |= entry.toMap().value("id").toUInt() == id; } while (used);
    }
    if (expiry < 0) expiry = m_options.value("default_expire_ms").toInt();
    expiry = std::clamp(expiry, 0, m_options.value("max_expire_ms").toInt());
    QVariantList actionRows;
    for (int i = 0; i + 1 < actions.size() && i < 64; i += 2) actionRows << QVariantMap{{"key", actions[i].left(256)}, {"label", actions[i + 1].left(256)}};
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QVariantMap row{{"id", id}, {"sender", sender}, {"appName", appName.left(256)}, {"icon", icon.left(1024)}, {"summary", summary.left(1024)},
                    {"body", body.left(16384)}, {"actions", actionRows}, {"urgency", unbox(hints.value("urgency")).toUInt()},
                    {"active", true}, {"createdAt", now}, {"expiresAt", expiry ? now + expiry : 0}, {"suppressed", m_dnd}};
    row["iconUrl"] = iconSource(id, icon, hints);
    row["desktopEntry"] = unbox(hints.value("desktop-entry")).toString().left(256);
    row["pid"] = m_senderPids.value(sender);
    if (replace >= 0) m_history[replace] = row;
    else {
        while (m_history.size() >= m_options.value("history_limit").toInt()) {
            const auto oldest = m_history.first().toMap();
            if (oldest.value("active").toBool()) close(oldest.value("id").toUInt(), 3);
            m_history.removeFirst();
        }
        m_history << row;
    }
    update();
    if (!m_senderPids.contains(sender) && !m_pidRequests.contains(sender) && m_pidRequests.size() < 64) {
        m_pidRequests.insert(sender);
        call(QDBusConnection::sessionBus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", {sender}, [this, sender](const QVariantList &args) {
            m_pidRequests.remove(sender);
            if (m_senderPids.size() >= 128) m_senderPids.clear();
            m_senderPids[sender] = args.value(0).toUInt();
            for (auto &entry : m_history) { auto row = entry.toMap(); if (row.value("sender") == sender) { row["pid"] = m_senderPids.value(sender); entry = row; } }
            update();
        }, [this, sender](const QString &) { m_pidRequests.remove(sender); });
    }
    return id;
}
void NotificationService::close(uint id, uint reason) {
    for (auto &entry : m_history) {
        auto row = entry.toMap();
        if (row.value("id").toUInt() != id || !row.value("active").toBool()) continue;
        row["active"] = false; row["closeReason"] = reason; entry = row;
        emit m_endpoint.NotificationClosed(id, reason); update(); return;
    }
}
bool NotificationService::act(const QString &actionName, const QVariantMap &args) {
    if (!m_owned) return false;
    if (actionName == "setDnd" && args.value("dnd").metaType().id() == QMetaType::Bool) { m_dnd = args.value("dnd").toBool(); update(); return true; }
    if (actionName == "clearHistory") {
        const auto history = m_history;
        for (const auto &entry : history) close(entry.toMap().value("id").toUInt(), 2);
        m_history.clear(); m_images.clear(); update(); return true;
    }
    const uint id = args.value("id").toUInt();
    for (const auto &entry : m_history) {
        const auto row = entry.toMap();
        if (row.value("id").toUInt() != id) continue;
        if (actionName == "activate") {
            bool invoked = false;
            if (row.value("active").toBool()) for (const auto &action : row.value("actions").toList()) {
                if (action.toMap().value("key") == "default") { emit m_endpoint.ActionInvoked(id, "default"); invoked = true; break; }
            }
            const bool focus = m_options.value("focus_on_click").toBool();
            if (focus) focusSender(row);
            if (invoked || focus) { close(id, 2); return true; }
            return false;
        }
        if (!row.value("active").toBool()) continue;
        if (actionName == "dismiss") { close(id, 2); return true; }
        if (actionName == "invoke") for (const auto &action : row.value("actions").toList()) {
            const auto key = action.toMap().value("key").toString();
            if (key == args.value("key").toString()) { emit m_endpoint.ActionInvoked(id, key); close(id, 2); return true; }
        }
    }
    return false;
}
}

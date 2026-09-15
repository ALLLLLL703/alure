#include "TrayMenu.h"
#include "Service.h"
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDateTime>

namespace Alure {
namespace { constexpr auto interface = "com.canonical.dbusmenu"; }
QDBusArgument &operator<<(QDBusArgument &arg, const MenuLayout &layout) {
    arg.beginStructure(); arg << layout.id << layout.properties << layout.children; arg.endStructure(); return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, MenuLayout &layout) {
    arg.beginStructure(); arg >> layout.id >> layout.properties >> layout.children; arg.endStructure(); return arg;
}
TrayMenu::TrayMenu(QObject *parent) : QObject(parent) {
    qDBusRegisterMetaType<MenuLayout>();
    m_reload.setSingleShot(true);
    connect(&m_reload, &QTimer::timeout, this, [this] { load(false); });
    QDBusConnection::sessionBus().connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                                         this, SLOT(ownerChanged(QString,QString,QString)));
}
void TrayMenu::clear(const QString &error) {
    ++m_generation; m_reload.stop();
    if (!m_destination.isEmpty()) {
        auto bus = QDBusConnection::sessionBus();
        for (const auto *signal : {"LayoutUpdated", "ItemsPropertiesUpdated"})
            bus.disconnect(m_destination, m_path, interface, signal, this, SLOT(menuUpdated(QDBusMessage)));
    }
    for (auto *watcher : findChildren<QDBusPendingCallWatcher *>()) delete watcher;
    m_destination.clear(); m_path.clear(); m_parents.clear(); m_items.clear(); m_loading = false; m_refreshPending = false; m_error = error;
    emit changed();
}
void TrayMenu::open(const QString &destination, const QString &path, int timeout) {
    clear();
    if (destination.isEmpty() || path.isEmpty() || path == "/") { clear("This tray application does not export a DBusMenu."); return; }
    m_destination = destination; m_path = path; m_timeout = timeout; m_parents = {0};
    auto bus = QDBusConnection::sessionBus();
    for (const auto *signal : {"LayoutUpdated", "ItemsPropertiesUpdated"})
        bus.connect(destination, path, interface, signal, this, SLOT(menuUpdated(QDBusMessage)));
    load(true);
}
void TrayMenu::request(const QString &method, const QVariantList &args, std::function<void(const QVariantList &)> success) {
    auto message = QDBusMessage::createMethodCall(m_destination, m_path, interface, method); message.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, m_timeout), this);
    const auto generation = m_generation;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, success = std::move(success)] {
        const auto reply = watcher->reply(); watcher->deleteLater();
        if (generation != m_generation) return;
        if (reply.type() == QDBusMessage::ErrorMessage) {
            m_loading = false; m_items.clear(); m_error = reply.errorName() + ": " + reply.errorMessage(); emit changed(); return;
        }
        success(reply.arguments());
    });
}
void TrayMenu::load(bool aboutToShow) {
    if (m_parents.isEmpty()) return;
    if (m_loading) { m_refreshPending = true; return; }
    m_loading = true; m_error.clear(); emit changed();
    const auto layout = [this](const QVariantList &) {
        request("GetLayout", {m_parents.last(), 1, QStringList{}}, [this](const QVariantList &args) {
            if (args.size() != 2 || args[1].metaType() != QMetaType::fromType<QDBusArgument>() ||
                qvariant_cast<QDBusArgument>(args[1]).currentSignature() != "(ia{sv}av)") {
                m_loading = false; m_items.clear(); m_error = "Invalid DBusMenu layout"; emit changed(); return;
            }
            const auto tree = qdbus_cast<MenuLayout>(args[1]);
            m_items.clear();
            for (const auto &child : tree.children.mid(0, 512)) {
                const auto node = qdbus_cast<MenuLayout>(unbox(child));
                auto row = node.properties;
                if (!row.value("visible", true).toBool()) continue;
                row["id"] = node.id;
                row["enabled"] = row.value("enabled", true);
                row["submenu"] = row.value("children-display").toString() == "submenu" || !node.children.isEmpty();
                m_items << row;
            }
            m_loading = false; emit changed();
            if (m_refreshPending) { m_refreshPending = false; m_reload.start(0); }
        });
    };
    if (aboutToShow) request("AboutToShow", {m_parents.last()}, layout); else layout({});
}
bool TrayMenu::select(int id) {
    if (m_loading || m_destination.isEmpty()) return false;
    for (const auto &entry : m_items) {
        const auto row = entry.toMap();
        if (row.value("id").toInt() != id || !row.value("enabled").toBool() || row.value("type") == "separator") continue;
        if (row.value("submenu").toBool()) {
            if (m_parents.size() >= 32 || m_parents.contains(id)) return false;
            m_parents << id; m_items.clear(); load(true);
        } else {
            if (!m_allowActions) return false;
            m_loading = true; emit changed();
            request("Event", {id, "clicked", QVariant::fromValue(QDBusVariant(0)),
                              static_cast<uint>(QDateTime::currentMSecsSinceEpoch())}, [this](const QVariantList &) { clear(); emit activated(); });
        }
        return true;
    }
    return false;
}
void TrayMenu::back() {
    if (!canGoBack()) return;
    ++m_generation; m_reload.stop();
    for (auto *watcher : findChildren<QDBusPendingCallWatcher *>()) delete watcher;
    m_loading = false; m_refreshPending = false;
    m_parents.removeLast(); m_items.clear(); load(true);
}
void TrayMenu::menuUpdated(const QDBusMessage &) {
    if (m_loading) m_refreshPending = true; else m_reload.start(0);
}
void TrayMenu::ownerChanged(const QString &name, const QString &oldOwner, const QString &) {
    if (name == m_destination && !oldOwner.isEmpty()) clear("Tray application disconnected.");
}
}

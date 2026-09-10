#include "TrayService.h"
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QImage>
#include <QBuffer>
namespace Alure {
namespace {
constexpr auto watcherName = "org.kde.StatusNotifierWatcher";
constexpr auto watcherPath = "/StatusNotifierWatcher";
QPair<QString, QString> splitId(const QString &id) {
    const auto slash = id.indexOf('/');
    return slash < 0 ? qMakePair(id, QString("/StatusNotifierItem")) : qMakePair(id.left(slash), id.mid(slash));
}
QString pixmapUrl(const QVariant &value) {
    if (value.metaType() != QMetaType::fromType<QDBusArgument>()) return {};
    const auto arg = qvariant_cast<QDBusArgument>(value);
    if (arg.currentSignature() != "a(iiay)") return {};
    QImage best;
    arg.beginArray();
    while (!arg.atEnd()) {
        int width = 0, height = 0; QByteArray bytes;
        arg.beginStructure(); arg >> width >> height >> bytes; arg.endStructure();
        if (width < 1 || height < 1 || width > 512 || height > 512 || bytes.size() != width * height * 4 || width <= best.width()) continue;
        QImage image(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto i = (y * width + x) * 4;
            image.setPixel(x, y, qRgba(uchar(bytes[i + 1]), uchar(bytes[i + 2]), uchar(bytes[i + 3]), uchar(bytes[i])));
        }
        best = image;
    }
    arg.endArray();
    if (best.isNull()) return {};
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); best.save(&buffer, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}
}
TrayWatcher::TrayWatcher(TrayService *service) : m_service(service) {}
void TrayWatcher::RegisterStatusNotifierItem(const QString &service) { m_service->registerItem(service, message().service()); }
void TrayWatcher::RegisterStatusNotifierHost(const QString &) { emit StatusNotifierHostRegistered(); }
void TrayWatcher::add(const QString &id) {
    if (m_items.contains(id) || m_items.size() >= 128) return;
    m_items << id; emit StatusNotifierItemRegistered(id); m_service->refresh();
}
void TrayWatcher::clear() { m_items.clear(); }
void TrayWatcher::ownerChanged(const QString &name, const QString &oldOwner, const QString &) {
    if (oldOwner.isEmpty()) return;
    const auto items = m_items;
    for (const auto &id : items) if (splitId(id).first == name) { m_items.removeAll(id); emit StatusNotifierItemUnregistered(id); }
    m_service->refresh();
}
TrayService::TrayService(QObject *parent) : Service(parent), m_watcher(this),
    m_hostName("org.kde.StatusNotifierHost.Alure" + QString::number(QCoreApplication::applicationPid())) {
    QDBusConnection::sessionBus().connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                                         &m_watcher, SLOT(ownerChanged(QString,QString,QString)));
}
TrayService::~TrayService() { stop(); }
void TrayService::stop() {
    auto bus = QDBusConnection::sessionBus();
    if (m_ownsWatcher) { bus.unregisterService(watcherName); bus.unregisterObject(watcherPath); }
    if (m_hostRegistered) bus.unregisterService(m_hostName);
    m_ownsWatcher = false; m_hostRegistered = false; m_watcher.clear();
}
void TrayService::registerItem(const QString &service, const QString &sender) {
    if (!enabled() || !m_ownsWatcher || sender.isEmpty()) return;
    const auto id = service.startsWith('/') ? sender + service : service + "/StatusNotifierItem";
    const auto [destination, path] = splitId(id);
    if (!QDBusObjectPath(path).path().startsWith('/')) return;
    call(QDBusConnection::sessionBus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner", {destination},
         [this, sender, id](const QVariantList &args) { if (args.value(0).toString() == sender) m_watcher.add(id); });
}
void TrayService::poll() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { fail("Session bus disconnected"); return; }
    if (!m_hostRegistered) m_hostRegistered = bus.registerService(m_hostName);
    if (!m_hostRegistered) { fail("Cannot register tray host name"); return; }
    setBusy(true);
    if (m_ownsWatcher) { readItems(m_watcher.registeredItems(), {}); return; }
    call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", {watcherName}, [this, bus](const QVariantList &args) mutable {
        if (!args.value(0).toBool() && bus.registerService(watcherName)) {
            m_ownsWatcher = bus.registerObject(watcherPath, &m_watcher, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals | QDBusConnection::ExportAllProperties);
            if (!m_ownsWatcher) { bus.unregisterService(watcherName); setBusy(false); fail("Cannot export tray watcher"); return; }
            readItems(m_watcher.registeredItems(), {});
        } else call(bus, watcherName, watcherPath, watcherName, "RegisterStatusNotifierHost", {m_hostName}, [this, bus](const QVariantList &) {
            properties(bus, watcherName, watcherPath, watcherName, [this](const QVariantMap &props) {
                readItems(qdbus_cast<QStringList>(props.value("RegisteredStatusNotifierItems")).mid(0, 128), {});
            });
        });
    });
}
void TrayService::readItems(QStringList ids, QVariantList rows) {
    if (ids.isEmpty()) { setBusy(false); publish({{"ownsWatcher", m_ownsWatcher}, {"count", rows.size()}}, rows); return; }
    const auto id = ids.takeFirst(); const auto [destination, path] = splitId(id);
    properties(QDBusConnection::sessionBus(), destination, path, "org.kde.StatusNotifierItem", [this, id, ids, rows](QVariantMap props) mutable {
        QVariantMap row{{"id", id}};
        for (const auto &key : {"Title", "Status", "IconName", "AttentionIconName", "ItemIsMenu", "Menu", "IconThemePath"}) row[key] = props.value(key);
        row["Menu"] = qvariant_cast<QDBusObjectPath>(props.value("Menu")).path();
        row["iconUrl"] = pixmapUrl(props.value(props.value("Status") == "NeedsAttention" ? "AttentionIconPixmap" : "IconPixmap"));
        rows << row; readItems(ids, rows);
    });
}
bool TrayService::act(const QString &name, const QVariantMap &args) {
    QString method;
    if (name == "activate") method = "Activate";
    else if (name == "secondaryActivate") method = "SecondaryActivate";
    else if (name == "contextMenu") method = "ContextMenu";
    else return false;
    for (const auto &entry : items()) {
        const auto row = entry.toMap(); const auto id = row.value("id").toString();
        if (id != args.value("id").toString()) continue;
        if (name == "activate" && row.value("ItemIsMenu").toBool()) method = "ContextMenu";
        const auto [destination, path] = splitId(id);
        return dbusAction(QDBusConnection::sessionBus(), destination, path, "org.kde.StatusNotifierItem", method, {args.value("x", 0).toInt(), args.value("y", 0).toInt()});
    }
    return false;
}
}

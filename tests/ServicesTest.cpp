#include "Services.h"
#include <QtTest>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
using namespace Alure;
namespace {
QVariantMap module(const QString &name, QVariantMap overrides = {}) {
    QVariantMap config; QString error; ConfigStore::parse({}, config, error);
    auto result = config.value("modules").toMap().value(name).toMap();
    auto options = result.value("behavior").toMap();
    for (auto it = overrides.begin(); it != overrides.end(); ++it) options[it.key()] = it.value();
    result["behavior"] = options; return result;
}
QVariantList command(const QString &mode) { return {QString(SERVICE_FIXTURE), mode}; }
void put(const QString &path, const QByteArray &text) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(text), text.size()); }
}
class SnapshotFixture : public Service {
public:
    int polls = 0;
    void poll() override { ++polls; publish({{"observed", true}}); }
};
class FakePlayer : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(bool CanControl READ yes)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanGoNext READ no)
    Q_PROPERTY(bool CanGoPrevious READ yes)
public:
    QVariantMap metadata() const { return {{"xesam:title", "Fixture Song"}, {"xesam:artist", QStringList{"Fixture Artist"}}}; }
    QString playbackStatus() const { return "Playing"; }
    bool yes() const { return true; }
    bool no() const { return false; }
    int played = 0;
public slots:
    void PlayPause() { ++played; }
    void Previous() {}
};
class FakeBluez : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")
public slots:
    Alure::ObjectMap GetManagedObjects() {
        return {{QDBusObjectPath("/org/bluez/hci0"), {{"org.bluez.Adapter1", {{"Powered", true}, {"Alias", "Fixture"}}}}},
                {QDBusObjectPath("/org/bluez/hci0/dev_A"), {{"org.bluez.Device1", {{"Connected", true}, {"Paired", false}, {"Alias", "Headphones"}}}}}};
    }
};
struct TestPixmap { int width = 1, height = 1; QByteArray data = QByteArray::fromHex("ff12ab34"); };
Q_DECLARE_METATYPE(TestPixmap)
QDBusArgument &operator<<(QDBusArgument &arg, const TestPixmap &p) { arg.beginStructure(); arg << p.width << p.height << p.data; arg.endStructure(); return arg; }
const QDBusArgument &operator>>(const QDBusArgument &arg, TestPixmap &p) { arg.beginStructure(); arg >> p.width >> p.height >> p.data; arg.endStructure(); return arg; }
using TestPixmaps = QList<TestPixmap>;
Q_DECLARE_METATYPE(TestPixmaps)
class FakeItem : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString IconName READ icon)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(bool ItemIsMenu READ menu)
    Q_PROPERTY(TestPixmaps IconPixmap READ pixmaps)
public:
    TestPixmaps pixmaps() const { return {TestPixmap{}}; }
    QString title() const { return "Fixture tray"; }
    QString icon() const { return "alure"; }
    QString status() const { return "Active"; }
    bool menu() const { return false; }
    int activations = 0;
public slots:
    void Activate(int, int) { ++activations; }
    void SecondaryActivate(int, int) {}
    void ContextMenu(int, int) {}
};
class FakeWatcher : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ host)
    Q_PROPERTY(int ProtocolVersion READ version)
public:
    QStringList ids;
    QStringList registeredItems() const { return ids; }
    bool host() const { return true; }
    int version() const { return 0; }
    int registrations = 0;
public slots:
    void RegisterStatusNotifierHost(const QString &) { ++registrations; }
};
class NotificationSignals : public QObject {
    Q_OBJECT
public:
    QList<QPair<uint, uint>> closedEvents;
    QString lastAction;
public slots:
    void closed(uint id, uint reason) { closedEvents << qMakePair(id, reason); }
    void invoked(uint, const QString &key) { lastAction = key; }
};
class ServicesTest : public QObject {
    Q_OBJECT
private slots:
    void presentationChangesKeepSnapshot() {
        SnapshotFixture service;
        service.configure(module("notifications")); QCOMPARE(service.polls, 1);
        const auto state = service.state();
        service.configure(module("notifications", {{"format", "custom"}, {"popup_enabled", false}, {"toast_enabled", false}}));
        QCOMPARE(service.polls, 1); QCOMPARE(service.state(), state);
    }
    void initTestCase() {
        qDBusRegisterMetaType<TestPixmap>(); qDBusRegisterMetaType<TestPixmaps>();
        QVERIFY(!qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS").isEmpty());
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    }
    void configuration() {
        QVariantMap config; QString error;
        QVERIFY(ConfigStore::parse({}, config, error));
        QVERIFY(!config.value("modules").toMap().value("notifications").toMap().value("behavior").toMap().value("server_enabled").toBool());
        for (const QByteArray &text : {QByteArray("[modules.volume.behavior]\ndebounce_ms=0"), QByteArray("[modules.volume.behavior]\nmax_percent=151"),
             QByteArray("[modules.notifications.behavior]\nhistory_limit=0"), QByteArray("[modules.notifications.behavior]\nserver_enabled=1"),
             QByteArray("[modules.wifi.behavior]\nradio_command=[1]"), QByteArray("[modules.battery.behavior]\nsysfs_path=\"relative\"")})
            QVERIFY2(!ConfigStore::parse(text, config, error), text.constData());
        QVERIFY(ConfigStore::parse("[modules.volume.behavior]\nmax_percent=125\ndebounce_ms=30\n[modules.notifications.behavior]\nserver_enabled=true\nhistory_limit=4", config, error));
    }
    void parsers() {
        QVariantMap state; QVariantList rows; QString error;
        QVERIFY(CommandService::parse(CommandService::Volume, "Volume: 0.52 [MUTED]\n", 0, state, rows, error));
        QCOMPARE(state.value("percent").toDouble(), 52.0); QVERIFY(state.value("muted").toBool());
        QVERIFY(!CommandService::parse(CommandService::Volume, "Volume: nan", 0, state, rows, error));
        QVERIFY(!CommandService::parse(CommandService::Volume, "Volume: 1.0 garbage", 0, state, rows, error));
        rows.clear(); state.clear();
        QVERIFY(CommandService::parse(CommandService::Updates, "", 2, state, rows, error)); QCOMPARE(state.value("count").toInt(), 0);
        QVERIFY(!CommandService::parse(CommandService::Updates, "failure", 1, state, rows, error));
        QVERIFY(CommandService::parse(CommandService::Updates, "pkg 1.0 -> 1.1\n", 0, state, rows, error)); QCOMPARE(rows.size(), 1);
        rows.clear(); state.clear();
        QVERIFY(CommandService::parse(CommandService::Wifi, "yes:Cafe\\:WiFi:82\nno:Other:12\n", 0, state, rows, error));
        QCOMPARE(state.value("ssid").toString(), "Cafe:WiFi"); QCOMPARE(rows.size(), 2);
        QVERIFY(!CommandService::parse(CommandService::Wifi, "yes:bad:200", 0, state, rows, error));
        QVERIFY(!CommandService::parse(CommandService::Workspaces, "[{}]", 0, state, rows, error));
    }
    void runnerLimits() {
        CommandRunner runner; QSignalSpy done(&runner, &CommandRunner::completed);
        QVERIFY(runner.run({SERVICE_FIXTURE, "sleep"}, 100));
        QVERIFY(!runner.run({SERVICE_FIXTURE, "volume"}, 100));
        QTRY_COMPARE(done.size(), 1); QVERIFY(done.takeFirst()[2].toString().contains("timed out"));
        QVERIFY(runner.run({SERVICE_FIXTURE, "flood"}, 2000)); QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.takeFirst()[2].toString().contains("1 MiB"));
        QVERIFY(runner.run({"/nonexistent/alure-fixture"}, 100)); QTRY_COMPARE(done.size(), 1); QVERIFY(!done.takeFirst()[2].toString().isEmpty());
        QVERIFY(runner.run({SERVICE_FIXTURE, "sleep"}, 1000)); runner.cancel(); QTest::qWait(150); QCOMPARE(done.size(), 0);
    }
    void reloadAndDisable() {
        CommandService service(CommandService::Volume);
        auto settings = module("volume", {{"command", command("sleep")}, {"timeout_ms", 100}, {"interval_ms", 1000}});
        service.configure(settings); QTRY_VERIFY(!service.busy()); QVERIFY(service.diagnostic().contains("timed out"));
        settings = module("volume", {{"command", command("volume")}, {"interval_ms", 100}});
        service.configure(settings); QTRY_VERIFY(service.available()); QCOMPARE(service.state().value("percent").toInt(), 42);
        settings["enabled"] = false; service.configure(settings); QVERIFY(!service.enabled()); QVERIFY(service.items().isEmpty());
        QSignalSpy changed(&service, &Service::changed); QTest::qWait(300); QCOMPARE(changed.size(), 0);
        QVERIFY(!service.action("toggleMute"));
        settings = module("volume", {{"command", command("sleep")}, {"timeout_ms", 500}}); service.configure(settings);
        settings = module("volume", {{"command", command("volume")}, {"interval_ms", 100}}); service.configure(settings);
        QTRY_VERIFY(service.available()); QCOMPARE(service.state().value("percent").toInt(), 42);
    }
    void wifiAndDebounce() {
        CommandService wifi(CommandService::Wifi);
        wifi.configure(module("wifi", {{"command", command("wifi")}, {"radio_status_command", command("radio")}, {"saved_command", command("saved")}}));
        QTRY_VERIFY(wifi.available()); QVERIFY(wifi.state().value("powered").toBool());
        QCOMPARE(wifi.state().value("savedConnections").toList().size(), 1);
        QVERIFY(!wifi.action("connectSaved", {{"uuid", "ffffffff-ffff-ffff-ffff-ffffffffffff"}}));
        wifi.configure(module("wifi", {{"command", command("wifi")}, {"radio_status_command", command("radio")}, {"saved_command", command("saved")}, {"radio_command", QVariantList{}}, {"connect_command", QVariantList{}}}));
        QTRY_VERIFY(wifi.available());
        QVERIFY(!wifi.action("setPowered", {{"powered", false}}));
        QVERIFY(!wifi.action("connectSaved", {{"uuid", "12345678-1234-1234-1234-123456789abc"}}));
        QTemporaryDir dir; const auto capture = dir.filePath("capture");
        CommandService volume(CommandService::Volume);
        volume.configure(module("volume", {{"command", command("volume")}, {"set_volume_command", QVariantList{QString(SERVICE_FIXTURE), "capture", capture}}, {"debounce_ms", 50}}));
        QTRY_VERIFY(volume.available());
        QVERIFY(volume.action("setVolume", {{"percent", 20}})); QVERIFY(volume.action("setVolume", {{"percent", 30}})); QVERIFY(volume.action("setVolume", {{"percent", 90}}));
        QVERIFY(!volume.action("setVolume", {{"percent", 101}}));
        QTRY_VERIFY(QFile::exists(capture)); QTRY_VERIFY(!volume.busy());
        QFile file(capture); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("0.900\n"));
        volume.configure(module("volume", {{"command", command("volume")}, {"set_volume_command", QVariantList{}}}));
        QTRY_VERIFY(volume.available()); QVERIFY(!volume.action("setVolume", {{"percent", 50}}));
        CommandService updates(CommandService::Updates); updates.configure(module("updates", {{"command", command("emptyUpdates")}}));
        QTRY_VERIFY(updates.available()); QCOMPARE(updates.state().value("count").toInt(), 0); QVERIFY(!updates.action("update"));
    }
    void registryQmlAndDefaultOptOut() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        QByteArray text;
        for (const auto &name : {"workspaces", "media", "tray", "volume", "updates", "wifi", "bluetooth", "battery"}) text += "[modules." + QByteArray(name) + "]\nenabled=false\n";
        put(path, text); ConfigStore config(path); QVERIFY(config.reload()); Services services(config);
        QVERIFY(!services.notifications()->enabled());
        QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered("org.freedesktop.Notifications").value());
        QQmlEngine engine; engine.rootContext()->setContextProperty("Services", &services);
        QQmlComponent component(&engine); component.setData("import QtQml\nQtObject { property bool off: !Services.volume.enabled && !Services.notifications.enabled }", QUrl());
        std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString())); QVERIFY(object->property("off").toBool());
        QSignalSpy changed(services.volume(), &Service::changed); QTest::qWait(150); QCOMPARE(changed.size(), 0);
    }
    void battery() {
        QTemporaryDir dir; QDir().mkpath(dir.filePath("BAT0"));
        put(dir.filePath("BAT0/type"), "Battery\n"); put(dir.filePath("BAT0/capacity"), "73\n"); put(dir.filePath("BAT0/status"), "Discharging\n");
        BatteryService service; service.configure(module("battery", {{"sysfs_path", dir.path()}}));
        QVERIFY(service.available()); QCOMPARE(service.state().value("percent").toInt(), 73);
        put(dir.filePath("BAT0/capacity"), "invalid"); service.refresh(); QVERIFY(!service.available()); QVERIFY(service.state().isEmpty());
    }
    void niriMultiOutputAndReconnect() {
        QTemporaryDir dir; QLocalServer server; const auto path = dir.filePath("socket"); QVERIFY(server.listen(path));
        QByteArray activation;
        connect(&server, &QLocalServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection(); connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, socket, [socket, &activation] {
                if (!socket->canReadLine()) return;
                const auto line = socket->readLine();
                if (line.contains("Action")) { activation = line; socket->write("{\"Ok\":\"Handled\"}\n"); }
                else socket->write(R"({"Ok":{"Workspaces":[{"id":10,"idx":1,"name":null,"output":"DP-1","is_active":true,"is_focused":true},{"id":20,"idx":1,"name":null,"output":"DP-2","is_active":true,"is_focused":false}]}})" "\n");
            });
        });
        NiriService service; auto config = module("workspaces", {{"socket_path", path}, {"interval_ms", 100}, {"timeout_ms", 100}});
        service.configure(config); QTRY_VERIFY(service.available()); QCOMPARE(service.items().size(), 2);
        QVERIFY(service.action("activate", {{"id", 20}})); QTRY_VERIFY(!activation.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(activation).object()["Action"].toObject()["FocusWorkspace"].toObject()["reference"].toObject()["Id"].toInt(), 20);
        QTRY_VERIFY(!service.busy()); server.close(); service.refresh(); QTRY_VERIFY(!service.available());
        QVERIFY(server.listen(path)); QTRY_VERIFY(service.available());
        config["enabled"] = false; service.configure(config); QSignalSpy changed(&service, &Service::changed); QTest::qWait(250); QCOMPARE(changed.size(), 0);
    }
    void mediaAndBluetooth() {
        auto bus = QDBusConnection::sessionBus(); FakePlayer player;
        QVERIFY(bus.registerService("org.mpris.MediaPlayer2.alureFixture"));
        QVERIFY(bus.registerObject("/org/mpris/MediaPlayer2", &player, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
        MediaService media; media.configure(module("media")); QTRY_VERIFY(media.available());
        QCOMPARE(media.items().first().toMap().value("title").toString(), "Fixture Song");
        QCOMPARE(media.items().first().toMap().value("artist").toStringList(), QStringList{"Fixture Artist"});
        const QVariantMap target{{"service", "org.mpris.MediaPlayer2.alureFixture"}};
        QVERIFY(!media.action("next", target)); QVERIFY(media.action("playPause", target)); QTRY_COMPARE(player.played, 1); QTRY_VERIFY(!media.busy());
        bus.unregisterObject("/org/mpris/MediaPlayer2"); bus.unregisterService("org.mpris.MediaPlayer2.alureFixture");
        media.refresh(); QTRY_VERIFY(!media.available());
        BluetoothService bluetooth; FakeBluez bluez;
        QVERIFY(bus.registerService("org.bluez")); QVERIFY(bus.registerObject("/", &bluez, QDBusConnection::ExportAllSlots));
        bluetooth.configure(module("bluetooth")); QTRY_VERIFY(bluetooth.available()); QCOMPARE(bluetooth.items().size(), 1);
        QCOMPARE(bluetooth.state().value("connectedCount").toInt(), 1);
        QVERIFY(!bluetooth.action("connect", {{"path", "/org/bluez/hci0/dev_A"}}));
        bus.unregisterObject("/"); bus.unregisterService("org.bluez");
    }
    void notifications() {
        NotificationSignals received;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "NotificationClosed", &received, SLOT(closed(uint,uint))));
        QVERIFY(bus.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "ActionInvoked", &received, SLOT(invoked(uint,QString))));
        NotificationService service;
        service.configure(module("notifications", {{"server_enabled", true}, {"history_limit", 2}, {"default_expire_ms", 100}, {"interval_ms", 100}}));
        QTRY_VERIFY(service.available());
        auto request = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");
        request.setArguments({"Fixture", uint(0), "", "Summary", "Body", QStringList{"default", "Open"}, QVariantMap{}, -1});
        QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(request, 1000));
        QSignalSpy done(&watcher, &QDBusPendingCallWatcher::finished); QTRY_VERIFY(watcher.isFinished());
        QDBusPendingReply<uint> reply = watcher; QVERIFY2(!reply.isError(), qPrintable(reply.error().message())); const auto id = reply.value(); QVERIFY(id > 0);
        QCOMPARE(service.items().size(), 1);
        QTRY_COMPARE(service.state().value("activeCount").toInt(), 0);
        QCOMPARE(service.items().first().toMap().value("closeReason").toUInt(), uint(1));
        QTRY_VERIFY(!received.closedEvents.isEmpty()); QCOMPARE(received.closedEvents.first(), qMakePair(id, uint(1)));
        QVERIFY(service.action("setDnd", {{"dnd", true}}));
        const auto second = service.notify(":fixture", "App", 0, "", "Second", "", {"key", "Label"}, {}, 0);
        QCOMPARE(service.notify(":fixture", "App", second, "", "Replaced", "", {"key", "Label"}, {}, 0), second);
        QVERIFY(service.items().last().toMap().value("suppressed").toBool());
        QVERIFY(!service.action("invoke", {{"id", second}, {"key", "bad"}}));
        QVERIFY(service.action("invoke", {{"id", second}, {"key", "key"}}));
        QTRY_COMPARE(received.lastAction, "key");
        service.notify(":fixture", "App", 0, "", "Third", "", {}, {}, 0); QCOMPARE(service.items().size(), 2);
        auto disabled = module("notifications"); disabled["enabled"] = false; service.configure(disabled);
        QVERIFY(QDBusConnection::sessionBus().registerService("org.freedesktop.Notifications")); QDBusConnection::sessionBus().unregisterService("org.freedesktop.Notifications");
    }
    void notificationNameConflict() {
        auto other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "notification-conflict");
        QVERIFY(other.registerService("org.freedesktop.Notifications"));
        { NotificationService service; service.configure(module("notifications", {{"server_enabled", true}})); QTRY_VERIFY(!service.busy()); QVERIFY(!service.available()); QVERIFY(service.diagnostic().contains("already owned")); }
        QVERIFY(other.interface()->isServiceRegistered("org.freedesktop.Notifications").value());
        other.unregisterService("org.freedesktop.Notifications"); QDBusConnection::disconnectFromBus("notification-conflict");
    }
    void trayCooperationAndRemoval() {
        auto other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "existing-tray");
        FakeWatcher watcher; FakeItem item; watcher.ids = {other.baseService() + "/Fixture"};
        QVERIFY(other.registerService("org.kde.StatusNotifierWatcher"));
        QVERIFY(other.registerObject("/StatusNotifierWatcher", &watcher, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots));
        QVERIFY(other.registerObject("/Fixture", &item, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots));
        {
            TrayService tray; tray.configure(module("tray", {{"interval_ms", 100}}));
            QTRY_VERIFY(tray.available()); QCOMPARE(tray.items().size(), 1); QVERIFY(!tray.state().value("ownsWatcher").toBool()); QVERIFY(watcher.registrations > 0);
            QCOMPARE(other.interface()->serviceOwner("org.kde.StatusNotifierWatcher").value(), other.baseService());
            watcher.ids.clear(); QTRY_VERIFY(tray.items().isEmpty());
            other.unregisterService("org.kde.StatusNotifierWatcher"); QTRY_VERIFY(tray.state().value("ownsWatcher").toBool());
        }
        other.unregisterObject("/StatusNotifierWatcher"); other.unregisterObject("/Fixture"); QDBusConnection::disconnectFromBus("existing-tray");
    }
    void tray() {
        auto bus = QDBusConnection::sessionBus(); FakeItem item;
        QVERIFY(bus.registerObject("/FixtureItem", &item, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
        TrayService tray; tray.configure(module("tray", {{"interval_ms", 100}})); QTRY_VERIFY(tray.available());
        tray.registerItem("/FixtureItem", bus.baseService()); QTRY_COMPARE(tray.items().size(), 1);
        QCOMPARE(tray.items().first().toMap().value("IconName").toString(), "alure");
        const auto url = tray.items().first().toMap().value("iconUrl").toString(); QVERIFY(url.startsWith("data:image/png;base64,"));
        const auto image = QImage::fromData(QByteArray::fromBase64(url.mid(url.indexOf(',') + 1).toLatin1()));
        QCOMPARE(image.size(), QSize(1, 1)); QCOMPARE(image.pixel(0, 0), qRgb(0x12, 0xab, 0x34));
        QVERIFY(tray.action("activate", {{"id", bus.baseService() + "/FixtureItem"}, {"x", 0}, {"y", 0}})); QTRY_COMPARE(item.activations, 1);
        bus.unregisterObject("/FixtureItem");
    }
};
QTEST_GUILESS_MAIN(ServicesTest)
#include "ServicesTest.moc"

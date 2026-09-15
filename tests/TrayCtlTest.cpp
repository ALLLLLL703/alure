#include "TrayService.h"
#include "TrayMenuFixture.h"
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMetaType>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QScopeGuard>
#include <QtTest>

class Registry : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ items)
public:
    QStringList registeredItems{"org.alure.TrayCtlFixture/StatusNotifierItem"};
    QStringList items() const { return registeredItems; }
};
class Item : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)
public:
    bool noMenu = false;
    QString title() const { return "Synthetic tray"; }
    QDBusObjectPath menu() const { return QDBusObjectPath(noMenu ? "/" : "/Menu"); }
};
class FailingMenu : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
public:
    bool hang = false;
public slots:
    bool AboutToShow(int) { return false; }
    uint GetLayout(int, int, const QStringList &, Alure::MenuLayout &layout) {
        layout = {0, {}, {QVariant::fromValue(QDBusVariant(QVariant::fromValue(Alure::MenuLayout{1, {{"label", "Fail"}}, {}})))}};
        return 1;
    }
    void Event(int, const QString &, const QDBusVariant &, uint) {
        if (hang) setDelayedReply(true);
        else sendErrorReply("org.alure.Refused", "synthetic refusal");
    }
};
class TrayCtlTest : public QObject {
    Q_OBJECT
    Registry registry;
    Item item;
    FakeTrayMenu menu;
    QTemporaryDir dir;
    QString config;
    QByteArray output, diagnostic;
    int run(QStringList args) {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("DISPLAY"); env.remove("WAYLAND_DISPLAY"); env.insert("QT_QPA_PLATFORM", "nonexistent");
        process.setProcessEnvironment(env);
        process.start(TRAYCTL_EXECUTABLE, QStringList{"--config", config} + args);
        if (!process.waitForStarted()) return -99;
        QElapsedTimer timer; timer.start();
        while (process.state() != QProcess::NotRunning && timer.elapsed() < 15000) QTest::qWait(5);
        output = process.readAllStandardOutput(); diagnostic = process.readAllStandardError();
        return process.state() == QProcess::NotRunning && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -99;
    }
private slots:
    void initTestCase() {
        qDBusRegisterMetaType<Alure::MenuLayout>();
        QVERIFY(dir.isValid()); config = dir.filePath("missing.toml");
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "trayctl-fixture");
        QVERIFY(bus.registerService("org.alure.TrayCtlFixture"));
        QVERIFY(bus.registerObject("/StatusNotifierItem", &item, QDBusConnection::ExportAllProperties));
        QVERIFY(bus.registerObject("/Menu", &menu, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
    }
    void observerNeverOwns() {
        Alure::TrayService tray(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
        const QVariantMap behavior{{"timeout_ms", 300}, {"interval_ms", 100}, {"allow_actions", true}};
        for (int i = 0; i < 2; ++i) {
            tray.configure({{"enabled", true}, {"behavior", behavior}});
            QTRY_VERIFY(tray.diagnostic().contains("No StatusNotifierWatcher"));
            QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered("org.kde.StatusNotifierWatcher").value());
            tray.refresh(); QTest::qWait(150);
            QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered("org.kde.StatusNotifierWatcher").value());
            tray.configure({{"enabled", false}, {"behavior", behavior}});
        }
        QCOMPARE(run({"list"}), 1); QVERIFY(diagnostic.contains("No StatusNotifierWatcher"));
        QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered("org.kde.StatusNotifierWatcher").value());
    }
    void pathsAndAcknowledgement() {
        auto bus = QDBusConnection("trayctl-fixture");
        QVERIFY(bus.registerService("org.kde.StatusNotifierWatcher"));
        QVERIFY(bus.registerObject("/StatusNotifierWatcher", &registry, QDBusConnection::ExportAllProperties));
        QCOMPARE(run({"--help"}), 0);
        QCOMPARE(run({"list"}), 0); QVERIFY(output.contains("Synthetic tray"));
        const QString id = registry.items().first();
        QCOMPARE(run({"menu", id}), 0); QVERIFY(output.contains("_Open fixture")); QVERIFY(!output.contains("Hidden"));
        QCOMPARE(run({"menu", id, "5"}), 0); QCOMPARE(menu.shown, 5); QVERIFY(output.contains("Submenu action"));
        QCOMPARE(run({"click", id, "5", "7"}), 0); QCOMPARE(menu.clicked, 7);
        menu.clicked = -1;
        for (const auto &path : {QStringList{"1", "7"}, QStringList{"2"}, QStringList{"3"}, QStringList{"5"}, QStringList{"6"}, QStringList{"999"}}) {
            QCOMPARE(run(QStringList{"click", id} + path), 1); QCOMPARE(menu.clicked, -1);
        }
        QCOMPARE(run({"menu", id, "1"}), 1); QCOMPARE(menu.clicked, -1);
        QCOMPARE(run({"menu", "org.alure.Stale/StatusNotifierItem"}), 1);
        item.noMenu = true; QCOMPARE(run({"menu", id}), 1); QVERIFY(diagnostic.contains("does not export")); item.noMenu = false;
        FailingMenu failure;
        bus.unregisterObject("/Menu"); QVERIFY(bus.registerObject("/Menu", &failure, QDBusConnection::ExportAllSlots));
        QCOMPARE(run({"click", id, "1"}), 1); QVERIFY(diagnostic.contains("org.alure.Refused"));
        failure.hang = true;
        QCOMPARE(run({"--timeout", "150", "click", id, "1"}), 1); QVERIFY(diagnostic.contains("timeout"));
        bus.unregisterObject("/Menu"); QVERIFY(bus.registerObject("/Menu", &menu, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
    }
    void syntaxErrors() {
        QCOMPARE(run({"--unknown", "list"}), 2); QVERIFY(diagnostic.contains("Unknown option")); QVERIFY(output.isEmpty());
        QCOMPARE(run({"list", "--config"}), 2); QVERIFY(diagnostic.contains("Missing value")); QVERIFY(output.isEmpty());
        QCOMPARE(run({"--help"}), 0); QVERIFY(output.contains("Usage:")); QVERIFY(diagnostic.isEmpty());
        QCOMPARE(run({"--help-all"}), 0); QVERIFY(output.contains("Usage:")); QVERIFY(diagnostic.isEmpty());
    }
    void invalidationDuringTraversal_data() {
        QTest::addColumn<bool>("watcherLoss");
        QTest::addColumn<bool>("sentEvent");
        QTest::newRow("removed-submenu") << false << false;
        QTest::newRow("watcher-lost-submenu") << true << false;
        QTest::newRow("removed-sent-event") << false << true;
        QTest::newRow("watcher-lost-sent-event") << true << true;
    }
    void invalidationDuringTraversal() {
        QFETCH(bool, watcherLoss); QFETCH(bool, sentEvent);
        const auto id = registry.items().first();
        QFile file(dir.filePath("fast.toml")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[modules.tray_launcher.behavior]\ninterval_ms=100\ntimeout_ms=2000\n"); file.close();
        config = file.fileName();
        menu.shown = -1; menu.clicked = -1; menu.holdSubmenu = !sentEvent; menu.holdEvent = sentEvent;
        QTimer watch;
        bool invalidated = false, released = false;
        connect(&watch, &QTimer::timeout, this, [&] {
            if (invalidated || (sentEvent ? menu.clicked != 1 : menu.shown != 5)) return;
            invalidated = true;
            if (watcherLoss) QDBusConnection("trayctl-fixture").unregisterService("org.kde.StatusNotifierWatcher");
            else registry.registeredItems.clear();
            QTimer::singleShot(350, this, [&] {
                if (sentEvent) menu.releaseEvent(); else menu.releaseSubmenu();
                released = true;
            });
        });
        watch.start(5);
        QCOMPARE(run(sentEvent ? QStringList{"click", id, "1"} : QStringList{"click", id, "5", "7"}), sentEvent ? 0 : 1);
        QVERIFY(invalidated);
        QTRY_VERIFY(released); QTest::qWait(50);
        QCOMPARE(menu.clicked, sentEvent ? 1 : -1);
        if (!sentEvent) QVERIFY(diagnostic.contains(watcherLoss ? "registry unavailable" : "no longer registered"));
        watch.stop(); menu.holdSubmenu = false; menu.holdEvent = false;
        registry.registeredItems = {id};
        if (watcherLoss) QVERIFY(QDBusConnection("trayctl-fixture").registerService("org.kde.StatusNotifierWatcher"));
        config = dir.filePath("missing.toml");
    }
    void invalidatedEventCannotNavigateBack() {
        Alure::TrayService tray(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
        tray.configure({{"enabled", true}, {"behavior", QVariantMap{{"timeout_ms", 2000}, {"interval_ms", 100}, {"allow_actions", true}}}});
        QTRY_VERIFY(tray.available());
        const auto original = registry.registeredItems;
        const auto cleanup = qScopeGuard([&] { menu.holdEvent = false; registry.registeredItems = original; });
        tray.openMenu(original.first()); QTRY_VERIFY(!tray.menu()->loading());
        QVERIFY(tray.menu()->select(5)); QTRY_VERIFY(!tray.menu()->loading());
        menu.holdEvent = true; menu.clicked = -1;
        QSignalSpy acknowledged(tray.menu(), &Alure::TrayMenu::activated);
        QVERIFY(tray.menu()->select(7)); QTRY_COMPARE(menu.clicked, 7);
        registry.registeredItems.clear(); tray.refresh();
        QTRY_VERIFY(tray.menu()->items().isEmpty()); QVERIFY(tray.menu()->loading()); QVERIFY(!tray.menu()->canGoBack());
        tray.menu()->back(); QVERIFY(!tray.menu()->select(7)); QCOMPARE(acknowledged.size(), 0);
        menu.releaseEvent(); QTRY_COMPARE(acknowledged.size(), 1);
    }
    void policyAndConfig() {
        QFile file(dir.filePath("deny.toml")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[modules.tray_launcher.behavior]\nallow_actions = false\n"); file.close(); config = file.fileName();
        QCOMPARE(run({"click", registry.items().first(), "1"}), 1); QVERIFY(diagnostic.contains("allow_actions"));
        QCOMPARE(run({"menu", registry.items().first(), "5"}), 0);
        QCOMPARE(run({"--timeout", "0", "list"}), 2);
        QCOMPARE(run({"click", registry.items().first(), "oops"}), 2);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("[modules.tray_launcher.behavior]\nexplicit_launch=false\n"); file.close();
        QCOMPARE(run({"list"}), 1); QVERIFY(diagnostic.contains("explicit_launch"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("[modules.tray_launcher.behavior]\nallow_actions=1\n"); file.close();
        QCOMPARE(run({"list"}), 2); QVERIFY(diagnostic.contains("allow_actions"));
    }
    void activatableRequestsNeverStart() {
        const QString marker = qEnvironmentVariable("ALURE_ACTIVATION_MARKER");
        QVERIFY(!marker.isEmpty()); QVERIFY(!QFile::exists(marker));
        config = dir.filePath("missing.toml");
        const auto original = registry.registeredItems;
        registry.registeredItems = {"org.alure.ActivatableFixture/StatusNotifierItem"};
        QCOMPARE(run({"list"}), 1); QVERIFY(!QFile::exists(marker));
        // Menu calls share the observer no-autostart policy, including lazy reads and Events.
        Alure::TrayService observer(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
        observer.menu()->open("org.alure.ActivatableFixture", "/Menu", 300);
        QTRY_VERIFY(!observer.menu()->loading()); QVERIFY(!observer.menu()->error().isEmpty()); QVERIFY(!QFile::exists(marker));
        QVERIFY(QDBusConnection("trayctl-fixture").unregisterService("org.kde.StatusNotifierWatcher"));
        QCOMPARE(run({"list"}), 1); QVERIFY(diagnostic.contains("No StatusNotifierWatcher")); QVERIFY(!QFile::exists(marker));
        // Positive control: activation really works on this private bus; panel default stays unchanged.
        Alure::TrayMenu panelMenu;
        panelMenu.open("org.alure.ActivatableFixture", "/Menu", 300);
        QTRY_VERIFY(QFile::exists(marker)); QTRY_VERIFY(!panelMenu.loading());
        QVERIFY(QFile::remove(marker));
        const auto reply = QDBusConnection::sessionBus().interface()->startService("org.kde.StatusNotifierWatcher");
        QVERIFY(!reply.isValid()); QTRY_VERIFY(QFile::exists(marker));
        registry.registeredItems = original;
    }
};
QTEST_GUILESS_MAIN(TrayCtlTest)
#include "TrayCtlTest.moc"

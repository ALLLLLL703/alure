#include "TrayService.h"
#include "TrayMenuFixture.h"
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMetaType>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

class Registry : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ items)
public:
    QStringList items() const { return {"org.alure.TrayCtlFixture/StatusNotifierItem"}; }
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
        auto bus = QDBusConnection::sessionBus();
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
        auto bus = QDBusConnection::sessionBus();
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
    void policyAndConfig() {
        QFile file(dir.filePath("deny.toml")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[modules.tray.behavior]\nallow_actions = false\n"); file.close(); config = file.fileName();
        QCOMPARE(run({"click", registry.items().first(), "1"}), 1); QVERIFY(diagnostic.contains("allow_actions"));
        QCOMPARE(run({"menu", registry.items().first(), "5"}), 0);
        QCOMPARE(run({"--timeout", "0", "list"}), 2);
        QCOMPARE(run({"click", registry.items().first(), "oops"}), 2);
    }
};
QTEST_GUILESS_MAIN(TrayCtlTest)
#include "TrayCtlTest.moc"

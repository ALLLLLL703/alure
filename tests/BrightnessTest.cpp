#include "BrightnessService.h"
#include "OsdController.h"
#include "ConfigStore.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
using namespace Alure;
namespace {
void put(const QString &path, const QByteArray &data) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); QCOMPARE(file.write(data), data.size());
}
QByteArray contents(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
void device(const QString &root, const QString &name, int level, int maximum) {
    put(root + '/' + name + "/brightness", QByteArray::number(level));
    put(root + '/' + name + "/max_brightness", QByteArray::number(maximum));
}
QVariantMap model() { QVariantMap result; QString error; if (!ConfigStore::parse({}, result, error)) qFatal("%s", qPrintable(error)); return result; }
QVariantMap module(const QString &root, const QVariantMap &overrides = {}) {
    auto result = model().value("modules").toMap().value("brightness").toMap();
    auto options = result.value("behavior").toMap();
    auto screen = options.value("screen").toMap(), keyboard = options.value("keyboard").toMap();
    screen["sysfs_path"] = root + "/backlight"; keyboard["sysfs_path"] = root + "/leds";
    options["screen"] = screen; options["keyboard"] = keyboard;
    options["interval_ms"] = 100; options["debounce_ms"] = 20;
    options["set_command"] = QVariantList{QString(SERVICE_FIXTURE), "brightness-write", root};
    for (auto it = overrides.begin(); it != overrides.end(); ++it) options[it.key()] = it.value();
    result["behavior"] = options; return result;
}
QVariantMap snapshot(const BrightnessService &service, const QString &kind = "screen") { return service.state().value(kind).toMap(); }
}
class BrightnessTest : public QObject {
    Q_OBJECT
private slots:
    void hardwareReadOnly() {
        if (!qEnvironmentVariableIsSet("ALURE_TEST_LIVE_BRIGHTNESS_READ")) QSKIP("Explicit native read-only probe is opt-in.");
        auto config = model().value("modules").toMap().value("brightness").toMap();
        auto options = config.value("behavior").toMap(); options["allow_actions"] = false; options["set_command"] = QVariantList{}; config["behavior"] = options;
        BrightnessService service; service.configure(config);
        QVERIFY(snapshot(service).value("available").toBool() || snapshot(service, "keyboard").value("available").toBool());
        qInfo() << "Native read-only backlights:" << service.state();
        QVERIFY(!service.action("adjustBrightness", {{"kind", "screen"}, {"delta", 1}}));
        QVERIFY(!service.action("adjustBrightness", {{"kind", "keyboard"}, {"delta", 1}}));
    }
    void configDefaultsAndOverrides() {
        auto defaults = model();
        const auto options = defaults.value("modules").toMap().value("brightness").toMap().value("behavior").toMap();
        QCOMPARE(options.value("screen").toMap().value("step").toInt(), 5);
        QCOMPARE(options.value("keyboard").toMap().value("max_level").toInt(), -1);
        QVERIFY(defaults.value("ui").toMap().value("osd").toMap().value("enabled").toBool());
        QString error; QVariantMap custom;
        QVERIFY2(ConfigStore::parse("[[panels]]\nmodules=['calendar']\nmodules_right=['volume']\n[modules.brightness.behavior.screen]\ndevice='intel_backlight'\nmin_percent=10\nmax_percent=80\n[modules.brightness.behavior.keyboard]\ndevice='platform::kbd_backlight'\nmax_level=1\n[ui.osd]\noutput='*'\nbackground='#112233'\nkeyboard_enabled=false", custom, error), qPrintable(error));
        const auto panel = custom.value("panels").toList().first().toMap();
        QCOMPARE(panel.value("modules").toList(), QVariantList{"calendar"}); QCOMPARE(panel.value("modules_right").toList(), QVariantList{"volume"});
        QCOMPARE(custom.value("ui").toMap().value("osd").toMap().value("output").toString(), "*");
        for (const auto &text : {
            "[ui.osd]\nenabled='yes'", "[ui.osd]\nwidth=0", "[ui.osd]\nopacity=nan", "[ui.osd]\noutput=''", "[ui.osd]\nbackground='bad color'", "[ui.osd]\nduration_ms=0",
            "[modules.brightness.behavior]\ndebounce_ms=0", "[modules.brightness.behavior]\nscroll_target='capslock'", "[modules.brightness.behavior]\nset_command=[1]",
            "[modules.brightness.behavior.screen]\nenabled=1", "[modules.brightness.behavior.screen]\nsysfs_path='relative'", "[modules.brightness.behavior.screen]\ndevice='../intel'",
            "[modules.brightness.behavior.screen]\ndevice='intel*'", "[modules.brightness.behavior.screen]\nmin_percent=90\nmax_percent=50", "[modules.brightness.behavior.screen]\nstep=0",
            "[modules.brightness.behavior.keyboard]\ndevice='input3::capslock'", "[modules.brightness.behavior.keyboard]\nstep=0.5", "[modules.brightness.behavior.keyboard]\nmax_level=-2",
            "[modules.brightness.behavior.keyboard]\nmin_level=2\nmax_level=1"}) {
            QVERIFY2(!ConfigStore::parse(text, custom, error), text); QVERIFY2(!error.isEmpty(), text);
        }
    }
    void discoveryPartialAndReadFailures() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        device(dir.path(), "backlight/intel_backlight", 202, 504);
        device(dir.path(), "leds/input0::capslock", 1, 1);
        device(dir.path(), "leds/platform::mute", 1, 1);
        BrightnessService service; service.configure(module(dir.path()));
        QVERIFY(snapshot(service).value("available").toBool()); QCOMPARE(snapshot(service).value("percent").toDouble(), 40.08);
        QVERIFY(!snapshot(service, "keyboard").value("available").toBool());
        QVERIFY(!service.action("setBrightness", {{"kind", "keyboard"}, {"level", 1}}));
        device(dir.path(), "leds/platform::kbd_backlight", 0, 2); service.refresh();
        QCOMPARE(snapshot(service, "keyboard").value("maximum").toInt(), 2);
        QCOMPARE(snapshot(service, "keyboard").value("level").toInt(), 0);
        device(dir.path(), "backlight/aaa", 1, 0); service.refresh(); QCOMPARE(snapshot(service).value("device").toString(), "intel_backlight");
        put(dir.filePath("backlight/intel_backlight/brightness"), "bad"); service.refresh();
        QVERIFY(!snapshot(service).value("available").toBool()); QVERIFY(snapshot(service, "keyboard").value("available").toBool());
        put(dir.filePath("backlight/intel_backlight/brightness"), "505"); service.refresh(); QVERIFY(!snapshot(service).value("available").toBool());
        put(dir.filePath("backlight/intel_backlight/brightness"), "200");
        QVERIFY(QFile::setPermissions(dir.filePath("backlight/intel_backlight/brightness"), {})); service.refresh();
        QVERIFY(!snapshot(service).value("available").toBool());
        QVERIFY(QFile::setPermissions(dir.filePath("backlight/intel_backlight/brightness"), QFile::ReadOwner | QFile::WriteOwner));
        auto config = module(dir.path()); auto options = config.value("behavior").toMap(); auto screen = options.value("screen").toMap();
        screen["device"] = "missing"; options["screen"] = screen; config["behavior"] = options; service.configure(config);
        QVERIFY(snapshot(service).value("diagnostic").toString().contains("missing"));
        config["enabled"] = false; service.configure(config); QVERIFY(!service.available()); QVERIFY(service.state().isEmpty());
    }
    void serializedLatestInputAndReadback() {
        QTemporaryDir dir; device(dir.path(), "backlight/intel", 40, 100); device(dir.path(), "leds/platform::kbd_backlight", 0, 2);
        BrightnessService service; service.configure(module(dir.path()));
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 50}}));
        QVERIFY(service.action("adjustBrightness", {{"kind", "screen"}, {"delta", 5}}));
        QTRY_VERIFY(service.busy()); QCOMPARE(snapshot(service).value("level").toInt(), 40);
        QVERIFY(service.action("adjustBrightness", {{"kind", "screen"}, {"delta", 5}}));
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 80}}));
        QVERIFY(service.action("adjustBrightness", {{"kind", "screen"}, {"delta", -5}}));
        QCOMPARE(snapshot(service).value("level").toInt(), 40); // no optimistic snapshot
        QTRY_VERIFY(!service.adjusting() && !service.busy());
        QCOMPARE(snapshot(service).value("level").toInt(), 75);
        QCOMPARE(contents(dir.filePath("calls")), "--class|backlight|--device|intel|set|55\n--class|backlight|--device|intel|set|75\n");
        QVERIFY(service.action("adjustBrightness", {{"kind", "keyboard"}, {"delta", 1}}));
        QTRY_VERIFY(!service.adjusting()); QCOMPARE(snapshot(service, "keyboard").value("level").toInt(), 1);
        QVERIFY(service.action("adjustBrightness", {{"kind", "keyboard"}, {"delta", 100}}));
        QTRY_VERIFY(!service.adjusting()); QCOMPARE(snapshot(service, "keyboard").value("level").toInt(), 2);
        QVERIFY(service.action("adjustBrightness", {{"kind", "screen"}, {"delta", -1000}}));
        QTRY_VERIFY(!service.adjusting()); QCOMPARE(snapshot(service).value("level").toInt(), 1);
        QVERIFY(!service.action("setBrightness", {{"kind", "keyboard"}, {"level", 0.5}}));
        QVERIFY(!service.action("adjustBrightness", {{"kind", "keyboard"}, {"delta", 0.5}}));
        QVERIFY(!service.action("setBrightness", {{"kind", "capslock"}, {"level", 1}}));
        QVERIFY(!service.action("setBrightness", {{"kind", "screen"}, {"percent", std::numeric_limits<double>::infinity()}}));
        QVERIFY(!service.action("setBrightness", {{"kind", "screen"}}));
    }
    void continuousInputAndCancellation() {
        QTemporaryDir dir; device(dir.path(), "backlight/intel", 40, 100);
        BrightnessService service; auto config = module(dir.path()); service.configure(config);
        QElapsedTimer elapsed; elapsed.start();
        while (elapsed.elapsed() < 100) { QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 55}})); QTest::qWait(5); }
        QVERIFY(contents(dir.filePath("calls")).contains("set|55")); // dragging must not starve writes
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 80}}));
        QTRY_VERIFY(!service.adjusting()); QCOMPARE(snapshot(service).value("level").toInt(), 80);
        const auto calls = contents(dir.filePath("calls"));
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 60}}));
        config["enabled"] = false; service.configure(config); QTest::qWait(180);
        QCOMPARE(contents(dir.filePath("calls")), calls); QVERIFY(!service.adjusting());
        service.configure(module(dir.path(), {{"allow_actions", false}}));
        QVERIFY(!service.action("adjustBrightness", {{"kind", "screen"}, {"delta", 5}}));
        service.configure(module(dir.path(), {{"set_command", QVariantList{}}}));
        QVERIFY(!snapshot(service).value("canSet").toBool());
    }
    void actionFailuresLimitsAndDeviceChange() {
        QTemporaryDir dir; device(dir.path(), "backlight/intel", 40, 100); device(dir.path(), "leds/platform::kbd_backlight", 1, 2);
        BrightnessService service;
        service.configure(module(dir.path(), {{"set_command", QVariantList{QString(SERVICE_FIXTURE), "brightness-denied", dir.path()}}}));
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 70}}));
        QTRY_VERIFY(!service.adjusting()); QCOMPARE(snapshot(service).value("level").toInt(), 40);
        QVERIFY(service.state().value("actionError").toString().contains("Permission denied"));
        service.configure(module(dir.path(), {{"set_command", QVariantList{"/nonexistent/alure-brightness"}}}));
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 70}})); QTRY_VERIFY(!service.adjusting());
        QVERIFY(service.state().value("actionError").toString().contains("failed"));
        service.configure(module(dir.path(), {{"set_command", QVariantList{QString(SERVICE_FIXTURE), "sleep"}}, {"timeout_ms", 100}}));
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 70}})); QTRY_VERIFY(!service.adjusting());
        QVERIFY(service.state().value("actionError").toString().contains("timed out"));
        auto config = module(dir.path(), {{"debounce_ms", 100}}); service.configure(config);
        const auto calls = contents(dir.filePath("calls"));
        QVERIFY(service.action("setBrightness", {{"kind", "screen"}, {"percent", 70}}));
        device(dir.path(), "backlight/aaa", 50, 100);
        QTRY_VERIFY(!service.adjusting()); QCOMPARE(contents(dir.filePath("calls")), calls);
        QVERIFY(service.state().value("actionError").toString().contains("changed"));
        auto options = config.value("behavior").toMap(), keyboard = options.value("keyboard").toMap();
        keyboard["min_level"] = 3; options["keyboard"] = keyboard; config["behavior"] = options; service.configure(config);
        QVERIFY(snapshot(service, "keyboard").value("available").toBool()); QVERIFY(!snapshot(service, "keyboard").value("canSet").toBool());
        keyboard["min_level"] = 0; keyboard["max_level"] = 1; options["keyboard"] = keyboard; config["behavior"] = options; service.configure(config);
        QVERIFY(service.action("setBrightness", {{"kind", "keyboard"}, {"level", 2}})); QTRY_VERIFY(!service.adjusting());
        QCOMPARE(snapshot(service, "keyboard").value("level").toInt(), 1);
    }
    void observedOsdTriggers() {
        OsdController osd; auto options = model().value("ui").toMap().value("osd").toMap(); osd.configure(options);
        QSignalSpy events(&osd, &OsdController::requested);
        QVariantMap volume{{"percent", 40}, {"muted", false}, {"backend", "pulseaudio"}, {"sinkIndex", "7"}};
        osd.observeVolume(true, volume); osd.observeVolume(true, volume); QCOMPARE(events.size(), 0);
        volume["muted"] = true; osd.observeVolume(true, volume); QCOMPARE(events.size(), 1); QVERIFY(events.last().first().toMap().value("muted").toBool());
        volume["percent"] = 50; osd.observeVolume(true, volume); QCOMPARE(events.size(), 2);
        volume["sinkIndex"] = "8"; osd.observeVolume(true, volume); QCOMPARE(events.size(), 2);
        osd.configure(options); volume["percent"] = 60; osd.observeVolume(true, volume); QCOMPARE(events.size(), 2); // reload baseline
        osd.observeVolume(false, {}); volume["percent"] = 70; osd.observeVolume(true, volume); QCOMPARE(events.size(), 2); // recovery baseline
        options["volume_enabled"] = false; osd.configure(options); osd.observeVolume(true, volume); volume["percent"] = 80; osd.observeVolume(true, volume); QCOMPARE(events.size(), 2);
        QTemporaryDir dir; device(dir.path(), "backlight/intel", 40, 100); device(dir.path(), "leds/platform::kbd_backlight", 0, 2);
        BrightnessService service;
        connect(&service, &Service::changed, &osd, [&] { osd.observeBrightness(service.state()); });
        service.configure(module(dir.path())); QCOMPARE(events.size(), 2);
        put(dir.filePath("backlight/intel/brightness"), "60"); QTRY_COMPARE(events.size(), 3);
        QCOMPARE(events.last().first().toMap().value("kind").toString(), "screen");
        put(dir.filePath("leds/platform::kbd_backlight/brightness"), "1"); QTRY_COMPARE(events.size(), 4);
        QCOMPARE(events.last().first().toMap().value("kind").toString(), "keyboard");
        QVERIFY(service.action("setBrightness", {{"kind", "keyboard"}, {"level", 2}}));
        QTRY_VERIFY(service.busy()); QCOMPARE(events.size(), 4); // request/busy do not flash
        QTRY_COMPARE(events.size(), 5); QCOMPARE(events.last().first().toMap().value("level").toInt(), 2);
        options["enabled"] = false; osd.configure(options); osd.observeBrightness(service.state());
        put(dir.filePath("backlight/intel/brightness"), "70"); service.refresh(); QCOMPARE(events.size(), 5);
    }
};
QTEST_GUILESS_MAIN(BrightnessTest)
#include "BrightnessTest.moc"

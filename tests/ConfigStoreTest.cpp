#include "ConfigStore.h"
#include <QFile>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using Alure::ConfigStore;
namespace {
void writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes), bytes.size());
}
QByteArray readFile(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
}
class ConfigStoreTest : public QObject {
    Q_OBJECT
private slots:
    void defaults() {
        QVariantMap model; QString error;
        QVERIFY2(ConfigStore::parse({}, model, error), qPrintable(error));
        QCOMPARE(model.value("settings").toMap().value("close_shortcut").toString(), "Ctrl+W");
        QCOMPARE(model.value("ui").toMap().value("popup_alignment").toString(), "center");
        QCOMPARE(model.value("ui").toMap().value("popup_direction").toString(), "inward");
        QCOMPARE(model.value("panels").toList().size(), 1);
        QCOMPARE(model.value("modules").toMap().keys(), (QStringList{"battery", "bluetooth", "brightness", "calendar", "clipboard", "media", "notifications", "taskbar", "tray", "tray_launcher", "updates", "volume", "wifi", "workspaces"}));
        QVERIFY(model.value("modules").toMap().value("clipboard").toMap().value("enabled").toBool()); // Lazy until opened.
        QCOMPARE(model.value("theme").toMap().value("palette").toMap().value("background").toString(), "#151923");
        QTemporaryDir dir; ConfigStore store(dir.filePath("missing.toml"));
        QVERIFY(store.reload()); QVERIFY(!QFile::exists(store.path()));
        QCOMPARE(store.model(), model);
    }
    void trayLauncherOptions() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse({}, model, error));
        const auto module = model.value("modules").toMap().value("tray_launcher").toMap();
        QVERIFY(!module.value("enabled").toBool());
        QVERIFY(module.value("behavior").toMap().value("explicit_launch").toBool());
        QVERIFY(!module.value("behavior").toMap().value("search_case_sensitive").toBool());
        QVERIFY(ConfigStore::parse("[modules.tray_launcher]\nenabled=true\n[modules.tray_launcher.behavior]\npopup_width=900\npopup_height=700\noutput='DP-2'\nallow_actions=false\nsearch_case_sensitive=true\nnext_shortcut='Ctrl+N'", model, error));
        QCOMPARE(model.value("modules").toMap().value("tray_launcher").toMap().value("behavior").toMap().value("popup_width").toInt(), 900);
        for (const auto *source : {"popup_width=0", "popup_height=2161", "output='*'", "output=''", "explicit_launch=1", "search_case_sensitive='yes'", "next_shortcut='Nonsense'", "close_on_activate=1"}) {
            QVERIFY(!ConfigStore::parse(QByteArray("[modules.tray_launcher.behavior]\n") + source, model, error));
            QVERIFY(error.contains("tray_launcher"));
        }
    }
    void backgroundBlurOptions() {
        QVariantMap model; QString error;
        QVERIFY2(ConfigStore::parse({}, model, error), qPrintable(error));
        QVERIFY(model.value("theme").toMap().value("blur_enabled").toBool());
        for (const auto *enabled : {"true", "false"}) {
            QVERIFY2(ConfigStore::parse(QByteArray("[theme]\nblur_enabled=") + enabled + "\nopacity=0.4", model, error), qPrintable(error));
            QCOMPARE(model.value("theme").toMap().value("blur_enabled").toBool(), QByteArray(enabled) == "true");
            QCOMPARE(model.value("theme").toMap().value("opacity").toDouble(), 0.4);
        }
        for (const auto *invalid : {"1", "0.5", "'true'", "[]", "{}"}) {
            QVERIFY(!ConfigStore::parse(QByteArray("[theme]\nblur_enabled=") + invalid, model, error));
            QVERIFY2(error.contains("theme.blur_enabled"), qPrintable(error));
        }
    }
    void panelWindowGapOptions() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse({}, model, error));
        QCOMPARE(model.value("panels").toList().first().toMap().value("window_gap").toInt(), 0);
        for (const auto *edge : {"top", "bottom", "left", "right"}) {
            for (int gap : {-256, -16, 0, 256}) {
                const auto source = QString("[[panels]]\nedge='%1'\nthickness=40\nwindow_gap=%2").arg(edge).arg(gap).toUtf8();
                QVERIFY2(ConfigStore::parse(source, model, error), qPrintable(error));
                QCOMPARE(model.value("panels").toList().first().toMap().value("window_gap").toInt(), gap);
            }
        }
        for (const auto *value : {"-257", "257", "1.5", "true", "'16'", "[]"}) {
            QVERIFY(!ConfigStore::parse(QByteArray("[[panels]]\nwindow_gap=") + value, model, error));
            QVERIFY2(error.contains("window_gap"), qPrintable(error));
        }
    }
    void panelVisibilityOptions() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse({}, model, error));
        const auto options = model.value("panels").toList().first().toMap().value("visibility").toMap();
        QCOMPARE(options.value("mode").toString(), "always");
        QCOMPARE(options.value("unknown_geometry").toString(), "hide");
        QCOMPARE(options.value("show_delay_ms").toInt(), 100);
        QCOMPARE(options.value("hide_delay_ms").toInt(), 350);
        QCOMPARE(options.value("edge_trigger_px").toInt(), 2);
        for (const auto *mode : {"always", "dodge-windows", "auto-hide"}) {
            QVERIFY2(ConfigStore::parse(QString("[[panels]]\nvisibility={mode='%1',show_delay_ms=0,hide_delay_ms=10000,edge_trigger_px=16,unknown_geometry='show'}").arg(mode).toUtf8(), model, error), qPrintable(error));
            QCOMPARE(model.value("panels").toList().first().toMap().value("visibility").toMap().value("mode").toString(), QString(mode));
        }
        for (const auto *setting : {"mode='dodge'", "mode=1", "unknown_geometry='guess'", "unknown_geometry=true", "show_delay_ms=-1", "hide_delay_ms=10001", "show_delay_ms=1.5", "edge_trigger_px=0", "edge_trigger_px=17", "edge_trigger_px='2'"}) {
            QVERIFY(!ConfigStore::parse(QByteArray("[[panels]]\n[panels.visibility]\n") + setting, model, error));
            QVERIFY2(error.contains("visibility"), qPrintable(error));
        }
        QVERIFY(!ConfigStore::parse("[[panels]]\nvisibility='always'", model, error));
    }
    void panelRespectFullscreenOptions() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse({}, model, error));
        QCOMPARE(model.value("panels").toList().first().toMap().value("visibility").toMap().value("respect_fullscreen").toBool(), true);
        for (const auto *value : {"true", "false"}) {
            QVERIFY2(ConfigStore::parse(QByteArray("[[panels]]\nvisibility.respect_fullscreen=") + value, model, error), qPrintable(error));
            QCOMPARE(model.value("panels").toList().first().toMap().value("visibility").toMap().value("respect_fullscreen").toBool(), QByteArray(value) == "true");
        }
        for (const auto *value : {"1", "0.5", "'true'", "[]", "{}"}) {
            QVERIFY(!ConfigStore::parse(QByteArray("[[panels]]\nvisibility.respect_fullscreen=") + value, model, error));
            QVERIFY2(error.contains("visibility.respect_fullscreen"), qPrintable(error));
        }
    }
    void volumeScrollOptions() {
        QVariantMap model; QString error; QVERIFY(ConfigStore::parse({}, model, error));
        const auto options = model.value("modules").toMap().value("volume").toMap().value("behavior").toMap();
        QCOMPARE(options.value("scroll_enabled").toBool(), true); QCOMPARE(options.value("scroll_step").toInt(), 5); QCOMPARE(options.value("scroll_inverted").toBool(), false);
        QVERIFY(ConfigStore::parse("[modules.volume.behavior]\nscroll_enabled=false\nscroll_step=2\nscroll_inverted=true", model, error));
        QCOMPARE(model.value("modules").toMap().value("volume").toMap().value("behavior").toMap().value("scroll_step").toInt(), 2);
        for (const auto *source : {"scroll_step=0", "scroll_step=101", "scroll_step=1.5", "scroll_enabled=1", "scroll_inverted='true'"})
            QVERIFY(!ConfigStore::parse(QByteArray("[modules.volume.behavior]\n") + source, model, error));
    }
    void audioBackendOptions() {
        QVariantMap model; QString error; QVERIFY(ConfigStore::parse({}, model, error));
        QCOMPARE(model.value("modules").toMap().value("volume").toMap().value("behavior").toMap().value("backend").toString(), "auto");
        for (const auto *backend : {"auto", "pipewire", "pulseaudio"})
            QVERIFY(ConfigStore::parse(QString("[modules.volume.behavior]\nbackend='%1'").arg(backend).toUtf8(), model, error));
        QVERIFY(ConfigStore::parse("[modules.volume.behavior]\npulse_set_volume_command=[]\npulse_mute_command=[]", model, error));
        for (const auto *source : {"[modules.volume.behavior]\nbackend='alsa'", "[modules.volume.behavior]\nbackend=1", "[modules.volume.behavior]\npulse_info_command='pactl info'", "[modules.volume.behavior]\npulse_mute_command=[1]"})
            QVERIFY(!ConfigStore::parse(source, model, error));
    }
    void moduleTooltipOptions() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse({}, model, error));
        QVERIFY(model.value("settings").toMap().value("module_tooltips").toBool());
        QCOMPARE(model.value("settings").toMap().value("module_tooltip_delay_ms").toInt(), 400);
        QVERIFY(ConfigStore::parse("[settings]\nmodule_tooltips=false\nmodule_tooltip_delay_ms=0", model, error));
        QVERIFY(!model.value("settings").toMap().value("module_tooltips").toBool());
        for (const auto *source : {"[settings]\nmodule_tooltips='true'", "[settings]\nmodule_tooltip_delay_ms=-1", "[settings]\nmodule_tooltip_delay_ms=5001", "[settings]\nmodule_tooltip_delay_ms=0.5"})
            QVERIFY(!ConfigStore::parse(source, model, error));
    }
    void moduleBehaviorDefaultsAreIsolated() {
        QVariantMap model; QString error;
        QVERIFY2(ConfigStore::parse("[modules.custom]\nenabled=false", model, error), qPrintable(error));
        const auto modules = model.value("modules").toMap();
        for (auto it = modules.begin(); it != modules.end(); ++it) {
            const auto behavior = it.value().toMap().value("behavior").toMap();
            QVERIFY(behavior.contains("interval_ms")); QVERIFY(behavior.contains("popup_enabled"));
            for (const auto *key : {"preferred_player", "control_size", "control_icon_size", "show_artwork", "artwork_remote", "artwork_height", "show_artist", "show_album", "show_progress", "show_shuffle", "show_repeat"})
                QCOMPARE(behavior.contains(key), it.key() == "media");
        }
        QVERIFY(modules.value("clipboard").toMap().value("behavior").toMap().contains("popup_width"));
        QVERIFY2(ConfigStore::parse("[modules.media.behavior]\npreferred_player='org.mpris.MediaPlayer2.musicfox'\n[modules.wifi.behavior]\npreferred_player='legacy-key'", model, error), qPrintable(error));
        QCOMPARE(model.value("modules").toMap().value("media").toMap().value("behavior").toMap().value("preferred_player").toString(), "org.mpris.MediaPlayer2.musicfox");
        // Unknown source keys remain round-trippable; the settings UI hides this foreign option.
        QCOMPARE(model.value("modules").toMap().value("wifi").toMap().value("behavior").toMap().value("preferred_player").toString(), "legacy-key");
        QVERIFY(!ConfigStore::parse("[modules.media.behavior]\npreferred_player=1", model, error));
    }
    void overridesAndExtensions() {
        QVariantMap model; QString error;
        const QByteArray source = R"(
[theme]
name = "dawn"
opacity = 1
[theme.palette]
accent = "#ff0000"
[modules.custom]
enabled = false
extra = "preserved"
[[panels]]
id = "side"
edge = "left"
output = "DP-2"
modules = ["custom", "calendar"]
[[panels]]
id = "bottom"
edge = "bottom"
)";
        QVERIFY2(ConfigStore::parse(source, model, error), qPrintable(error));
        const auto theme = model.value("theme").toMap();
        QCOMPARE(theme.value("palette").toMap().value("background").toString(), "#f5f0e8");
        QCOMPARE(theme.value("palette").toMap().value("accent").toString(), "#ff0000");
        QCOMPARE(model.value("panels").toList().size(), 2);
        QCOMPARE(model.value("panels").toList().first().toMap().value("thickness").toInt(), 44);
        QVERIFY(model.value("modules").toMap().value("custom").toMap().contains("behavior"));
        QVERIFY(ConfigStore::parse("panels = []", model, error));
        QVERIFY(model.value("panels").toList().isEmpty());
    }
    void themeCatalog() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml")); QVERIFY(store.reload());
        const auto names = store.themeNames(); QCOMPARE(names.size(), 6);
        for (const auto &name : names) {
            QVariantMap model; QString error;
            QVERIFY2(ConfigStore::parse(QString("[theme]\nname='%1'").arg(name).toUtf8(), model, error), qPrintable(error));
            QCOMPARE(model.value("theme").toMap().value("palette").toMap().size(), 6);
            QVERIFY(ConfigStore::parse(QString("[theme]\nname='%1'\n[theme.palette]\naccent='#123456'").arg(name).toUtf8(), model, error));
            QCOMPARE(model.value("theme").toMap().value("palette").toMap().value("accent").toString(), "#123456");
        }
        for (const auto *name : {"onedark", "catppuccin", "tokyonight"}) QVERIFY(names.contains(name));
    }
    void panelLayouts() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse({}, model, error));
        QCOMPARE(model.value("panels").toList()[0].toMap().value("layout").toString(), "linear");
        QVERIFY(ConfigStore::parse("[[panels]]\nlayout='three-zone'\nmodules_left=['volume','@spacer','@spacer']\nmodules_center=['calendar']\nmodules_right=['@stretch','@settings']\nspacer_size=40", model, error));
        QCOMPARE(model.value("panels").toList()[0].toMap().value("spacer_size").toInt(), 40);
        for (const auto *text : {"[[panels]]\nlayout='grid'", "[[panels]]\nlayout=1", "[[panels]]\nspacer_size=-1", "[[panels]]\nspacer_size=1.5", "[[panels]]\nmodules_left='volume'", "[[panels]]\nmodules_center=['@unknown']", "[[panels]]\nlayout='three-zone'\nmodules_left=['calendar']"}) {
            QVERIFY2(!ConfigStore::parse(text, model, error), text); QVERIFY(!error.isEmpty());
        }
    }
    void settingsCloseOverrides() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse("[settings]\nclose_shortcut='Alt+F4'", model, error));
        QVERIFY(ConfigStore::parse("[settings]\nclose_shortcut=''", model, error));
    }
    void popupOptions() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse("[ui]\ntoggle_on_click=false\n[modules.workspaces.style]\nactive_indicator='pill'", model, error));
        QVERIFY(!model.value("ui").toMap().value("toggle_on_click").toBool());
        QCOMPARE(model.value("modules").toMap().value("workspaces").toMap().value("style").toMap().value("active_indicator").toString(), "pill");
        for (const auto &alignment : {"start", "center", "end"})
            for (const auto &direction : {"inward", "top", "bottom", "left", "right"}) {
                QVERIFY(ConfigStore::parse(QString("[ui]\npopup_alignment='%1'\npopup_direction='%2'\npopup_gap=256\nclose_on_focus_loss=false\nescape_closes=false").arg(alignment, direction).toUtf8(), model, error));
                QCOMPARE(model.value("ui").toMap().value("popup_alignment").toString(), alignment);
                QCOMPARE(model.value("ui").toMap().value("popup_direction").toString(), direction);
            }
    }
    void invalid_data() {
        QTest::addColumn<QByteArray>("source");
        for (const auto &[name, text] : std::initializer_list<std::pair<const char *, const char *>>{
            {"clipboard-shortcut", "[modules.clipboard.behavior]\ncopy_shortcut='NotAKey'"},
            {"clipboard-path", "[modules.clipboard.behavior]\ndatabase_path='relative/db'"},
            {"clipboard-limit", "[modules.clipboard.behavior]\nmax_bytes=0"},
            {"clipboard-command", "[modules.clipboard.behavior]\ncliphist_command=[]"},
            {"clipboard-fallback", "[modules.clipboard.behavior]\ncursor_fallback='guess'"},
            {"clipboard-flag", "[modules.clipboard.behavior]\nallow_delete='true'"},
            {"media-width", "[modules.media.behavior]\npopup_width=100"},
            {"media-icon", "[modules.media.behavior]\ncontrol_icon_size=100"},
            {"toggle-type", "[ui]\ntoggle_on_click='yes'"},
            {"workspace-indicator", "[modules.workspaces.style]\nactive_indicator='sunken'"},
            {"popup-align", "[ui]\npopup_alignment='middle'"}, {"popup-direction", "[ui]\npopup_direction='outward'"},
            {"popup-align-type", "[ui]\npopup_alignment=3"}, {"popup-gap", "[ui]\npopup_gap=257"},
            {"syntax", "[broken"}, {"version", "version = 2"}, {"version-type", "version = '1'"},
            {"root-table", "theme = false"}, {"theme", "[theme]\nname = 'unknown'"},
            {"color", "[theme.palette]\naccent = 'no-color'"}, {"palette-type", "[theme]\npalette = 2"},
            {"opacity", "[theme]\nopacity = 1.1"}, {"nan", "[theme]\nopacity = nan"},
            {"font", "[theme]\nfont = ''"}, {"font-size-type", "[theme]\nfont_size = 12.5"},
            {"icon-mode", "[theme]\nicon_mode = 'download'"}, {"watch-type", "[runtime]\nwatch = 'yes'"},
            {"delay", "[runtime]\nreload_delay_ms = 0"}, {"settings-size", "[settings]\nwidth = 20"},
            {"close-shortcut", "[settings]\nclose_shortcut = 'nonsense'"},
            {"panel-type", "panels = [1]"}, {"edge", "[[panels]]\nedge = 'center'"},
            {"duplicate", "[[panels]]\nid = 'a'\n[[panels]]\nid = 'a'"},
            {"margin-type", "[[panels]]\nmargins = {left = '8'}"}, {"margin", "[[panels]]\nmargins = {left = -1}"},
            {"zone", "[[panels]]\nexclusive_zone = -2"}, {"module-name", "[[panels]]\nmodules = ['missing']"},
            {"module-duplicate", "[[panels]]\nmodules = ['media', 'media']"},
            {"enabled-type", "[modules.volume]\nenabled = 1"}, {"interval", "[modules.volume.behavior]\ninterval_ms = 0"},
            {"command", "[modules.volume.behavior]\ncommand = 'sh -c bad'"},
            {"argv-type", "[modules.volume.behavior]\ncommand = [1]"}, {"argv-empty", "[modules.volume.behavior]\ncommand = ['']"},
            {"module-table", "[modules]\ncustom = true"},
            {"icon-path", "[foundation]\nicon = '../file'"}, {"icon-empty", "[foundation]\nicon = ''"}
        }) QTest::newRow(name) << QByteArray(text);
    }
    void invalid() {
        QFETCH(QByteArray, source); QVariantMap model{{"sentinel", true}}; QString error;
        QVERIFY(!ConfigStore::parse(source, model, error)); QVERIFY(!error.isEmpty());
        QCOMPARE(model.value("sentinel").toBool(), true);
    }
    void reloadRetainsModelAndRepairs() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        writeFile(path, "[theme]\nname = 'forest'\n");
        ConfigStore store(path); QVERIFY(store.reload()); const auto good = store.model();
        QSignalSpy changes(&store, &ConfigStore::modelChanged);
        writeFile(path, "[broken"); QVERIFY(!store.reload()); QCOMPARE(store.model(), good);
        QCOMPARE(changes.size(), 0); QCOMPARE(store.source(), "[broken");
        QVERIFY(!store.saveText("version = false")); QCOMPARE(readFile(path), "[broken");
        QVERIFY(store.saveText("[theme]\nname = 'dawn'\n")); QVERIFY(store.diagnostic().isEmpty());
        QCOMPARE(changes.size(), 1);
    }
    void savePreservesUnknownAndDetectsConflict() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        const QByteArray source = "# keep this comment\n[future]\nkey = 42\ndate = 2025-01-01\n";
        writeFile(path, source); ConfigStore store(path); QVERIFY(store.reload());
        QVERIFY(store.saveText(store.source())); QCOMPARE(readFile(path), source);
        writeFile(path, source + "other = true\n");
        QVERIFY(!store.saveText("version = 1")); QVERIFY(store.diagnostic().contains("externally"));
        QCOMPARE(readFile(path), source + "other = true\n");
        QVERIFY(store.reload()); QVERIFY(store.saveText(store.source()));
        QVERIFY(QFile::remove(path)); QVERIFY(!store.saveText("version = 1"));
    }
    void saveMissingAndExternalCreation() {
        QTemporaryDir dir; const auto path = dir.filePath("nested/config.toml");
        ConfigStore store(path); QVERIFY(store.reload()); QVERIFY(store.saveText("version = 1\n"));
        QCOMPARE(readFile(path), "version = 1\n");
        const auto other = dir.filePath("other.toml"); ConfigStore conflict(other); QVERIFY(conflict.reload());
        writeFile(other, "# external\n"); QVERIFY(!conflict.saveText("version = 1"));
    }
    void refusesLockedSave() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        writeFile(path, "version = 1"); ConfigStore store(path); QVERIFY(store.reload());
        QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock());
        QVERIFY(!store.saveText("# edited\n")); QVERIFY(store.diagnostic().contains("locked"));
        QCOMPARE(readFile(path), "version = 1");
    }
    void rejectsNonFile() {
        QTemporaryDir dir; ConfigStore store(dir.path());
        QVERIFY(!store.reload()); QVERIFY(store.diagnostic().contains("regular file"));
    }
    void symlinkAndSize() {
        QTemporaryDir dir; const auto target = dir.filePath("target"); const auto path = dir.filePath("link");
        writeFile(target, "version = 1"); QVERIFY(QFile::link(target, path));
        ConfigStore store(path); QVERIFY(store.reload()); QVERIFY(!store.saveText("# modified\n"));
        QCOMPARE(readFile(target), "version = 1");
        QVariantMap model; QString error; QVERIFY(!ConfigStore::parse(QByteArray(1024 * 1024 + 1, ' '), model, error));
    }
    void unchangedReloadDoesNotPublish() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        writeFile(path, "[theme]\nname = 'forest'");
        ConfigStore store(path); QVERIFY(store.reload()); store.startWatching();
        QSignalSpy changes(&store, &ConfigStore::modelChanged);
        QSignalSpy sourceChanges(&store, &ConfigStore::sourceChanged);
        QVERIFY(store.reload()); QCOMPARE(changes.size(), 0); QCOMPARE(sourceChanges.size(), 0);
        writeFile(dir.filePath("unrelated-screenshot.txt"), "not config");
        QTest::qWait(350); QCOMPARE(changes.size(), 0); QCOMPARE(sourceChanges.size(), 0);
        writeFile(path, "# comment only\n[theme]\nname = 'forest'");
        QTest::qWait(350); QCOMPARE(changes.size(), 0);
    }
    void watchesAtomicReplacementAndInvalid() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        ConfigStore store(path); QVERIFY(store.reload()); store.startWatching();
        writeFile(path, "[theme]\nname = 'forest'");
        QTRY_COMPARE_WITH_TIMEOUT(store.model().value("theme").toMap().value("name").toString(), "forest", 3000);
        const auto replacement = dir.filePath("replacement"); writeFile(replacement, "[theme]\nname = 'dawn'");
        QVERIFY(QFile::remove(path)); QVERIFY(QFile::rename(replacement, path));
        QTRY_COMPARE_WITH_TIMEOUT(store.model().value("theme").toMap().value("name").toString(), "dawn", 3000);
        writeFile(path, "[broken"); QTRY_VERIFY_WITH_TIMEOUT(!store.diagnostic().isEmpty(), 3000);
        QCOMPARE(store.model().value("theme").toMap().value("name").toString(), "dawn");
    }
};
QTEST_GUILESS_MAIN(ConfigStoreTest)
#include "ConfigStoreTest.moc"

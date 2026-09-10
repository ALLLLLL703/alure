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
        QCOMPARE(model.value("panels").toList().size(), 1);
        QCOMPARE(model.value("modules").toMap().size(), 10);
        QCOMPARE(model.value("theme").toMap().value("palette").toMap().value("background").toString(), "#151923");
        QTemporaryDir dir; ConfigStore store(dir.filePath("missing.toml"));
        QVERIFY(store.reload()); QVERIFY(!QFile::exists(store.path()));
        QCOMPARE(store.model(), model);
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
    void invalid_data() {
        QTest::addColumn<QByteArray>("source");
        for (const auto &[name, text] : std::initializer_list<std::pair<const char *, const char *>>{
            {"syntax", "[broken"}, {"version", "version = 2"}, {"version-type", "version = '1'"},
            {"root-table", "theme = false"}, {"theme", "[theme]\nname = 'unknown'"},
            {"color", "[theme.palette]\naccent = 'no-color'"}, {"palette-type", "[theme]\npalette = 2"},
            {"opacity", "[theme]\nopacity = 1.1"}, {"nan", "[theme]\nopacity = nan"},
            {"font", "[theme]\nfont = ''"}, {"font-size-type", "[theme]\nfont_size = 12.5"},
            {"icon-mode", "[theme]\nicon_mode = 'download'"}, {"watch-type", "[runtime]\nwatch = 'yes'"},
            {"delay", "[runtime]\nreload_delay_ms = 0"}, {"settings-size", "[settings]\nwidth = 20"},
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

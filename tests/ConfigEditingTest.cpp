#include "ConfigStore.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using Alure::ConfigStore;
class ConfigEditingTest : public QObject {
    Q_OBJECT
private slots:
    void panelVisibilityFieldsPreserveSource() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml"));
        for (const QString &source : {QString("# inherited panel\nversion=1\n"), QString("# explicit panel\n[[panels]]\nid='main'\n"), QString::fromUtf8(ConfigStore::defaultSource())}) {
            QString edited = source;
            for (const auto &field : QList<QPair<QString, QString>>{{"respect_fullscreen", "false"}, {"mode", "'dodge-windows'"}, {"show_delay_ms", "0"}, {"hide_delay_ms", "700"}, {"edge_trigger_px", "4"}, {"unknown_geometry", "'show'"}}) {
                const auto result = store.editLiteral(edited, "panels.0.visibility." + field.first, field.second);
                QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
                edited = result.value("text").toString();
            }
            QVERIFY(edited.startsWith(source.left(source.indexOf('\n') + 1)));
            QVariantMap model; QString error; QVERIFY(ConfigStore::parse(edited.toUtf8(), model, error));
            const auto visibility = model.value("panels").toList().first().toMap().value("visibility").toMap();
            QCOMPARE(visibility.value("mode").toString(), "dodge-windows");
            QCOMPARE(visibility.value("respect_fullscreen").toBool(), false);
            QCOMPARE(visibility.value("edge_trigger_px").toInt(), 4);
            const auto invalid = store.editLiteral(edited, "panels.0.visibility.edge_trigger_px", "0");
            QVERIFY(!invalid.value("error").toString().isEmpty()); QCOMPARE(invalid.value("text").toString(), edited);
        }
    }
    void uiValidation() {
        QVariantMap model; QString error;
        const QByteArray custom = "[ui]\npopup_width = 600\nescape_closes = false\n[theme]\nicon_theme = 'Breeze'\n[modules.volume.style]\nforeground = '#aabbcc'\nmax_width = 220\n";
        QVERIFY(ConfigStore::parse(custom, model, error));
        QCOMPARE(model.value("ui").toMap().value("popup_width").toInt(), 600);
        for (const auto &invalid : {"[ui]\npopup_width = 10", "[ui]\nanimation_ms = -1", "[ui.toast]\nedge = 'left'", "[ui.toast]\nduration_ms = 0", "[settings]\ncontrols_style = 'Unknown'", "[modules.media.style]\nforeground = 'nonsense'", "[modules.media.style]\nmin_width = 100\nmax_width = 20", "[modules.media.style]\nicon = '../x'", "[modules.calendar.behavior]\nfirst_day_of_week = 2", "[modules.volume.behavior]\npopup_enabled = 1"})
            QVERIFY2(!ConfigStore::parse(invalid, model, error), invalid);
    }
    void sourceSpansAndSafeRejections() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml")); QVERIFY(store.reload());
        const QString text = QString::fromUtf8("# Unicode 漢字 😀\n[theme] # keep\nfont = '漢字 😀'; invalid");
        auto result = store.editLiteral(text, "theme.font", "'new'");
        QVERIFY(!result.value("error").toString().isEmpty()); QCOMPARE(result.value("text").toString(), text);
        const QString valid = QString::fromUtf8("# Unicode 漢字 😀\n[theme] # keep\nfont = '漢字 😀' # untouched comment\n[future]\nmultiline = \"\"\"first 😀\n[panels]\nlast\"\"\"\n");
        result = store.editLiteral(valid, "theme.font", "'New font'");
        QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        QVERIFY(result.value("text").toString().contains("# untouched comment"));
        QVERIFY(result.value("text").toString().contains(valid.mid(valid.indexOf("[future]"))));
        result = store.editLiteral(valid, "future.multiline", "'short'");
        QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        QVERIFY(result.value("text").toString().startsWith(valid.left(valid.indexOf("[future]"))));
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse(result.value("text").toString().toUtf8(), model, error));
        QCOMPARE(model.value("future").toMap().value("multiline").toString(), "short");
        result = store.editLiteral("[theme] # retained\nname = 'dawn'\n", "theme.font_size", "16");
        QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        QVERIFY(result.value("text").toString().contains("[theme] # retained"));
        for (const auto &source : {"[\"theme\"]\nname = 'dawn'\n"}) {
            result = store.editLiteral(source, "theme.name", "'forest'");
            QVERIFY(!result.value("error").toString().isEmpty()); QCOMPARE(result.value("text").toString(), source);
        }
        const auto unchanged = store.model();
        QVERIFY(store.previewText("[theme]\nname = 'dawn'")); QVERIFY(!QFile::exists(store.path()));
        QVERIFY(!store.previewText("[broken")); QCOMPARE(store.model().value("theme").toMap().value("name").toString(), "dawn");
        QVERIFY(store.reload()); QCOMPARE(store.model(), unchanged);
    }
    void existingInlineMembersPreserveSource() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml"));
        const QString source = "# retained\n[[panels]]\nid='main'\nmargins = { top = 8, left = 12, future = {value = 42} } # inline comment\nextra = 'keep'\n";
        const auto result = store.editLiteral(source, "panels.0.margins.top", "20");
        QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        auto expected = source; expected.replace("top = 8", "top = 20");
        QCOMPARE(result.value("text").toString(), expected);
        const auto missing = store.editLiteral(source, "panels.0.margins.right", "20");
        QVERIFY(!missing.value("error").toString().isEmpty()); QCOMPARE(missing.value("text").toString(), source);
    }
    void repeatedPaletteEditsUseExplicitAncestor() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml"));
        for (const QString &source : {QString("# keep\n[theme] # explicit\nname='midnight' # name\n[future]\nvalue=42\n"),
                QString("# keep\n[theme.palette] # explicit\naccent='#112233' # accent\n"), QString("# no explicit theme ancestor\nversion=1\n")}) {
            QString edited = source;
            for (const auto &path : {"theme.palette.accent", "theme.palette.background", "theme.palette.foreground", "theme.palette.accent"}) {
                const auto result = store.editLiteral(edited, path, "'#aabbcc'");
                QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
                edited = result.value("text").toString();
            }
            QVERIFY(edited.contains(source.mid(source.indexOf('#'), source.indexOf('\n') + 1)));
            if (source.contains("[future]")) QVERIFY(edited.endsWith("[future]\nvalue=42\n"));
            if (source.contains("# explicit")) QVERIFY(edited.contains("# explicit"));
            QVariantMap model; QString error; QVERIFY(ConfigStore::parse(edited.toUtf8(), model, error));
            const auto palette = model.value("theme").toMap().value("palette").toMap();
            for (const auto &key : {"accent", "background", "foreground"}) QCOMPARE(palette.value(key).toString(), "#aabbcc");
        }
        const QString inlineSource = "theme = {palette = {accent = '#123456', future = '#abcdef'}} # retained\n";
        auto result = store.editLiteral(inlineSource, "theme.palette.accent", "'#aabbcc'");
        QVERIFY(result.value("error").toString().isEmpty());
        auto expected = inlineSource; expected.replace("#123456", "#aabbcc"); QCOMPARE(result.value("text").toString(), expected);
        result = store.editLiteral(expected, "theme.palette.accent", "'#112233'");
        QVERIFY(result.value("error").toString().isEmpty()); expected.replace("#aabbcc", "#112233"); QCOMPARE(result.value("text").toString(), expected);
        result = store.editLiteral(expected, "theme.palette.background", "'#ffffff'");
        QVERIFY(!result.value("error").toString().isEmpty()); QCOMPARE(result.value("text").toString(), expected);
        const QString quoted = "[\"theme\"]\nname='midnight'\n";
        result = store.editLiteral(quoted, "theme.palette.background", "'#ffffff'");
        QVERIFY(!result.value("error").toString().isEmpty()); QCOMPARE(result.value("text").toString(), quoted);
    }
    void inheritedPanelMaterializesOnlyEditedLeaves() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml"));
        const QString source = "# preserved\n[theme]\nname='forest' # keep\n[future]\nunknown={value=42}\n";
        for (const bool marginsFirst : {false, true}) {
            QString edited = source;
            const QStringList paths = marginsFirst ? QStringList{"panels.0.margins.top", "panels.0.modules"} : QStringList{"panels.0.modules", "panels.0.margins.top"};
            for (const auto &path : paths) {
                const auto result = store.editLiteral(edited, path, path.endsWith("modules") ? "['media', 'workspaces']" : "20");
                QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
                edited = result.value("text").toString(); QVERIFY(edited.startsWith(source));
            }
            QVariantMap model; QString error; QVERIFY(ConfigStore::parse(edited.toUtf8(), model, error));
            const auto panels = model.value("panels").toList(); QCOMPARE(panels.size(), 1);
            const auto panel = panels.first().toMap(); QCOMPARE(panel.value("id").toString(), "main");
            QCOMPARE(panel.value("modules").toList(), (QVariantList{"media", "workspaces"}));
            QCOMPARE(panel.value("margins").toMap().value("top").toInt(), 20);
            QCOMPARE(panel.value("margins").toMap().value("left").toInt(), 8);
            QVERIFY(!edited.contains("thickness"));
        }
        for (const auto &text : {source, QString("panels=[]\n")}) {
            const auto result = store.editLiteral(text, text == source ? "panels.1.thickness" : "panels.0.thickness", "60");
            QVERIFY(!result.value("error").toString().isEmpty()); QCOMPARE(result.value("text").toString(), text);
        }
    }
    void unsupportedPanelHeadersLeaveSourceIntact() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml"));
        for (const auto &text : {"[[\"panels\"]]\nid = 'a'\n", "[[panels]]\nid = 'a'\n[\"future\"]\nkeep = 42\n"}) {
            const auto result = store.editPanels(text, "add", 0);
            QVERIFY(!result.value("error").toString().isEmpty()); QCOMPARE(result.value("text").toString(), text);
        }
    }
    void nestedPanelInsertionTargetsSelectedPanel() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml"));
        const QString text = "[[panels]]\nid = 'a'\n[panels.extra]\nexisting = 1\n[[panels]]\nid = 'b'\n[panels.extra]\nexisting = 2\n";
        const auto result = store.editLiteral(text, "panels.1.extra.added", "42");
        QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        QVariantMap model; QString error; QVERIFY(ConfigStore::parse(result.value("text").toString().toUtf8(), model, error));
        const auto panels = model.value("panels").toList();
        QVERIFY(!panels[0].toMap().value("extra").toMap().contains("added"));
        QCOMPARE(panels[1].toMap().value("extra").toMap().value("added").toInt(), 42);
    }
    void panelDraftPreservesUnknown() {
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.toml")); QVERIFY(store.reload());
        const QString source = "# root keep\n[future]\nvalue = '''\n[[panels]]\nnot a header\n'''\n# panel comment\n[[panels]]\nid = 'a'\n[panels.extension]\nvalue = 42\ndate = 2025-01-01\n[[panels]]\nid = 'b'\nextra = { nested = [1, 2] }\n# unrelated theme comment\n[theme]\nname = 'dawn' # keep me\n";
        auto result = store.editPanels(source, "down", 0);
        QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        const auto changed = result.value("text").toString();
        QVERIFY(changed.contains("# unrelated theme comment\n[theme]\nname = 'dawn' # keep me\n"));
        QVERIFY(changed.startsWith(source.left(source.indexOf("# panel comment"))));
        QVariantMap model; QString error; QVERIFY(ConfigStore::parse(changed.toUtf8(), model, error));
        auto panels = model.value("panels").toList(); QCOMPARE(panels.size(), 2);
        QCOMPARE(panels[0].toMap().value("id").toString(), "b");
        QCOMPARE(panels[0].toMap().value("extra").toMap().value("nested").toList().size(), 2);
        QCOMPARE(panels[1].toMap().value("extension").toMap().value("value").toInt(), 42);
        QVERIFY(changed.contains("2025-01-01"));
        result = store.editPanels(changed, "remove", 0); QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        result = store.editPanels(result.value("text").toString(), "remove", 0); QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        QVERIFY(ConfigStore::parse(result.value("text").toString().toUtf8(), model, error)); QVERIFY(model.value("panels").toList().isEmpty());
        result = store.editPanels(result.value("text").toString(), "add", 0); QVERIFY2(result.value("error").toString().isEmpty(), qPrintable(result.value("error").toString()));
        QVERIFY(ConfigStore::parse(result.value("text").toString().toUtf8(), model, error)); QCOMPARE(model.value("panels").toList().size(), 1);
        QVERIFY(store.saveText(source));
        const auto draft = store.editPanels(store.source(), "add", 0);
        QFile file(store.path()); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(source.toUtf8() + "# external\n"); file.close();
        QVERIFY(!store.saveText(draft.value("text").toString())); QVERIFY(store.diagnostic().contains("externally"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), source.toUtf8() + "# external\n");
    }
};
QTEST_GUILESS_MAIN(ConfigEditingTest)
#include "ConfigEditingTest.moc"

#include "ConfigStore.h"
#include "PanelHost.h"
#include "IconProvider.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtTest>

class FixtureService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available MEMBER available NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(QString diagnostic MEMBER diagnostic NOTIFY changed)
    Q_PROPERTY(QVariantMap state MEMBER state NOTIFY changed)
    Q_PROPERTY(QVariantList items MEMBER items NOTIFY changed)
public:
    bool available = true, busy = false;
    QString diagnostic;
    QVariantMap state;
    QVariantList items;
    QString lastAction;
    QVariantMap lastArguments;
    Q_INVOKABLE bool action(const QString &name, const QVariantMap &arguments) { lastAction = name; lastArguments = arguments; return true; }
    Q_INVOKABLE void refresh() {}
signals:
    void changed();
};
class FixtureShell : public QObject {
    Q_OBJECT
public:
    int closes = 0;
    Q_INVOKABLE void closePopup() { ++closes; }
    Q_INVOKABLE void closeToast() { ++closes; }
    Q_INVOKABLE bool openSettings() { return true; }
    Q_INVOKABLE void openModule(const QString &, const QString &, const QString &) {}
};
class UiTest : public QObject {
    Q_OBJECT
private slots:
    void surfaces() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service; FixtureShell shell;
        service.state = {{"percent", 52}, {"muted", false}, {"powered", true}, {"connected", true}, {"ssid", "Fixture Wi-Fi"}, {"count", 1}, {"dnd", false}, {"connectedCount", 1},
            {"savedConnections", QVariantList{QVariantMap{{"uuid", "fixture"}, {"name", "Saved fixture"}}}},
            {"adapters", QVariantList{QVariantMap{{"path", "/fixture"}, {"Alias", "Fixture adapter"}, {"Powered", true}}}}};
        const QVariantMap row{{"id", "20"}, {"idx", 1}, {"name", "Fixture"}, {"output", "DP-2"}, {"is_active", true},
            {"title", "A long observed fixture title <b>plain text</b>"}, {"artist", QStringList{"Fixture artist"}}, {"album", "Fixture album"}, {"service", "fixture.player"}, {"playbackStatus", "Playing"},
            {"CanControl", true}, {"CanPlay", true}, {"CanPause", true}, {"CanGoNext", true}, {"CanGoPrevious", true},
            {"Title", "Fixture tray"}, {"IconName", "volume"}, {"iconUrl", ""}, {"current", "1"}, {"next", "2"},
            {"ssid", "Fixture Wi-Fi"}, {"signal", 80}, {"active", true}, {"path", "/fixture"}, {"Alias", "Fixture device"}, {"Connected", true}, {"Paired", true},
            {"summary", "Fixture notification"}, {"body", "<b>Must stay plain</b>"}, {"appName", "Fixture app"}, {"createdAt", 1700000000000LL}, {"suppressed", false},
            {"actions", QVariantList{QVariantMap{{"key", "default"}, {"label", "Open fixture"}}}}, {"percent", 52}, {"status", "Discharging"}};
        service.items = {row};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Shell", &shell);
        QVariantMap services;
        for (const auto &name : {"workspaces", "media", "tray", "volume", "updates", "wifi", "bluetooth", "notifications", "battery"}) services[name] = QVariant::fromValue(&service);
        engine.rootContext()->setContextProperty("Services", services);
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const auto &list) { warnings += list; });
        for (const auto &name : config.model().value("modules").toMap().keys()) {
            QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(440, 560);
            view.setInitialProperties({{"moduleName", name}}); view.setSource(QUrl("qrc:/qml/Popup.qml"));
            QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty() ? "unknown load failure" : view.errors().first().toString()));
            view.show(); QTest::qWait(20); QVERIFY(view.rootObject()->width() > 0);
            if (name == "volume") {
                QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "act", Q_ARG(QVariant, "setVolume"), Q_ARG(QVariant, (QVariantMap{{"percent", 25}}))));
                QCOMPARE(service.lastAction, "setVolume"); QCOMPARE(service.lastArguments.value("percent").toInt(), 25);
            }
            service.available = false; service.diagnostic = "Fixture unavailable"; emit service.changed(); QTest::qWait(5);
            service.available = true; service.diagnostic.clear(); emit service.changed();
        }
        for (const bool vertical : {false, true}) {
            QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(vertical ? 44 : 900, vertical ? 700 : 44);
            view.setInitialProperties({{"panel", config.model().value("panels").toList().first()}, {"vertical", vertical}, {"outputName", "fixture"}});
            view.setSource(QUrl("qrc:/qml/Panel.qml")); QVERIFY(view.status() == QQuickView::Ready); view.show(); QTest::qWait(20);
        }
        {
            QQuickView view(&engine, nullptr); view.resize(380, 150); view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.setInitialProperties({{"notification", row}}); view.setSource(QUrl("qrc:/qml/Toast.qml")); QVERIFY(view.status() == QQuickView::Ready); view.show(); QTest::qWait(20);
        }
        QStringList diagnostics; for (const auto &warning : warnings) diagnostics << warning.toString();
        QVERIFY2(warnings.isEmpty(), qPrintable(diagnostics.join('\n')));
        QVERIFY(!QFile::exists(config.path()));
    }
    void moduleStripGeometryAndIcons_data() {
        QTest::addColumn<QString>("moduleName");
        QTest::addColumn<bool>("vertical");
        QTest::addColumn<bool>("showIcon");
        for (const auto &name : {QString("workspaces"), QString("tray")})
            for (const bool vertical : {false, true})
                for (const bool showIcon : {false, true})
                    QTest::newRow(qPrintable(name + (vertical ? "-vertical" : "-horizontal") + (showIcon ? "-icon" : "-no-icon"))) << name << vertical << showIcon;
    }
    void moduleStripGeometryAndIcons() {
        QFETCH(QString, moduleName); QFETCH(bool, vertical); QFETCH(bool, showIcon);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[ui]\nmodule_height=64\n[modules." + moduleName + ".style]\nshow_label=false\nshow_icon=" + (showIcon ? "true" : "false") + "\nicon='calendar'\n"));
        FixtureService service;
        QImage image(1, 1, QImage::Format_ARGB32); image.fill(Qt::red);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        const QString pixmapUrl = "data:image/png;base64," + QString::fromLatin1(png.toBase64());
        service.items = {QVariantMap{{"id", "20"}, {"idx", 1}, {"name", "Fixture"}, {"output", "DP-1"}, {"is_active", true},
            {"Title", "Fixture tray"}, {"Status", "NeedsAttention"}, {"IconName", "volume"}, {"AttentionIconName", "battery"}, {"iconUrl", pixmapUrl}}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{moduleName, QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView);
        const auto panel = config.model().value("panels").toList().first().toMap();
        const int crossSize = panel.value("thickness").toInt() - 2 * config.model().value("ui").toMap().value("panel_padding").toInt();
        QCOMPARE(crossSize, 32);
        view.resize(vertical ? crossSize : 240, vertical ? 240 : crossSize);
        view.setInitialProperties({{"moduleName", moduleName}, {"vertical", vertical}, {"crossSize", crossSize}});
        view.setSource(QUrl("qrc:/qml/ModuleStrip.qml")); QVERIFY(view.status() == QQuickView::Ready); view.show();
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name) return item;
            for (auto *child : item->childItems()) if (auto *found = findItem(child, name)) return found;
            return nullptr;
        };
        QQuickItem *entry = nullptr;
        QTRY_VERIFY(entry = findItem(view.rootObject(), moduleName + "-entry-20"));
        QCOMPARE(entry->height(), vertical ? 64 : crossSize);
        QVERIFY(entry->isVisible()); QCOMPARE(entry->property("text").toString(), QString());
        QCOMPARE(entry->property("iconName").toString(), showIcon ? QString("calendar") : QString());
        QCOMPARE(entry->property("iconSource").toString(), moduleName == "tray" ? pixmapUrl : QString());
        std::function<QQuickItem *(QQuickItem *)> findImage = [&](QQuickItem *item) -> QQuickItem * {
            if (item->property("source").isValid()) return item;
            for (auto *child : item->childItems()) if (auto *found = findImage(child)) return found;
            return nullptr;
        };
        auto *icon = findImage(entry); QVERIFY(icon);
        QCOMPARE(icon->isVisible(), showIcon);
        const QString expectedSource = !showIcon ? QString() : moduleName == "tray" ? pixmapUrl : "image://icons/builtin/calendar";
        QCOMPARE(icon->property("source").toUrl().toString(), expectedSource);
        if (moduleName == "tray") {
            auto row = service.items.first().toMap(); row["iconUrl"] = ""; service.items = {row}; emit service.changed();
            QTRY_VERIFY(entry = findItem(view.rootObject(), moduleName + "-entry-20"));
            QCOMPARE(entry->property("iconSource").toString(), "image://icons/theme/battery");
        }
        QCOMPARE(warnings.size(), 0);
    }
    void panelClickOpensPopup() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[[panels]]\nmodules = ['calendar']\n"));
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        FixtureService service;
        QVariantMap services; for (const auto &name : {"workspaces", "media", "tray", "volume", "updates", "wifi", "bluetooth", "notifications", "battery"}) services[name] = QVariant::fromValue(&service);
        engine.rootContext()->setContextProperty("Services", services);
        Alure::PanelHost host(config, engine, true);
        QQuickView *bar = nullptr;
        for (auto *window : QGuiApplication::topLevelWindows()) if (window->title() == "Alure · main") bar = qobject_cast<QQuickView *>(window);
        QVERIFY(bar); QTest::qWait(30);
        std::function<QQuickItem *(QQuickItem *)> findButton = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "calendar-button") return item;
            for (auto *child : item->childItems()) if (auto *found = findButton(child)) return found;
            return nullptr;
        };
        auto *button = findButton(bar->rootObject());
        QVERIFY(button); QVERIFY(button->width() > 0); QVERIFY(button->height() > 0);
        const auto point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        QTest::mouseMove(bar, point.toPoint()); QTest::qWait(550); // Hover before clicking: overlays must not steal the press.
        QSignalSpy clicked(button, SIGNAL(clicked()));
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, point.toPoint()); QTest::qWait(30);
        QVERIFY(button->property("pressed").toBool());
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, point.toPoint()); QTest::qWait(30);
        QCOMPARE(clicked.size(), 1);
        QQuickView *popup = nullptr;
        for (auto *window : QGuiApplication::topLevelWindows()) if (window->title() == "Alure details" && window->isVisible()) popup = qobject_cast<QQuickView *>(window);
        QVERIFY2(popup, "A real calendar button click must create a visible details window through PanelHost");
        QCOMPARE(popup->rootObject()->property("moduleName").toString(), "calendar");
        QTest::keyClick(popup, Qt::Key_Escape); QTest::qWait(10); QVERIFY(!popup->isVisible());
    }
    void settingsDraft() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *root = engine.rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root, "editField", Q_ARG(QVariant, "theme.name"), Q_ARG(QVariant, "\"dawn\"")));
        QCOMPARE(root->property("dirty").toBool(), true);
        for (int section = 0; section < 5; ++section) { root->setProperty("section", section); QTest::qWait(10); }
        QVERIFY(!QFile::exists(config.path()));
        QVERIFY(QMetaObject::invokeMethod(root, "save"));
        QCOMPARE(root->property("dirty").toBool(), false);
        QCOMPARE(config.model().value("theme").toMap().value("name").toString(), "dawn");
        QCOMPARE(warnings.size(), 0);
    }
    void calendarMath() {
        QQmlEngine engine; QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport \"qrc:/qml/Ui.js\" as Ui\nQtObject { property var cells: Ui.monthCells(2024, 1, 1); property bool equal: Ui.sameDay(new Date(2024,1,29), cells[31]) }", QUrl());
        std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("cells").value<QJSValue>().property("length").toInt(), 42);
        QVERIFY(object->property("equal").toBool());
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv); QQuickStyle::setStyle("Basic"); QQuickWindow::setDefaultAlphaBuffer(true);
    UiTest test; return QTest::qExec(&test, argc, argv);
}
#include "UiTest.moc"

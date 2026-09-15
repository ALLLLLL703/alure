#include "BackgroundBlur.h"
#include "ConfigStore.h"
#include "IconProvider.h"
#include "TrayLauncherHost.h"
#include "TrayService.h"
#include "TrayMenuFixture.h"
#include <QDBusMetaType>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtTest>

class UiRegistry : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ items)
public:
    QStringList items() const { return {"org.alure.LauncherFixture/StatusNotifierItem"}; }
};
class UiItem : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)
public:
    QString title() const { return "Synthetic Music"; }
    QDBusObjectPath menu() const { return QDBusObjectPath("/Menu"); }
};
class TrayLauncherUiTest : public QObject {
    Q_OBJECT
    UiRegistry registry;
    UiItem item;
    FakeTrayMenu menu;
    static QQuickItem *find(QQuickItem *root, const QString &name) {
        if (root->objectName() == name) return root;
        for (auto *child : root->childItems()) if (auto *result = find(child, name)) return result;
        return nullptr;
    }
private slots:
    void initTestCase() {
        qDBusRegisterMetaType<Alure::MenuLayout>();
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "launcher-fixture");
        QVERIFY(bus.registerService("org.kde.StatusNotifierWatcher"));
        QVERIFY(bus.registerService("org.alure.LauncherFixture"));
        QVERIFY(bus.registerObject("/StatusNotifierWatcher", &registry, QDBusConnection::ExportAllProperties));
        QVERIFY(bus.registerObject("/StatusNotifierItem", &item, QDBusConnection::ExportAllProperties));
        QVERIFY(bus.registerObject("/Menu", &menu, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
    }
    void staticPanelEntry() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("missing")); QVERIFY(config.reload());
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport \"qrc:/qml\"\nModuleStrip { moduleName: 'tray_launcher'; vertical: false; crossSize: 40 }", QUrl());
        std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("summary").toString(), "Tray apps");
        QVERIFY(object->property("service").isNull()); QCOMPARE(warnings.size(), 0);
    }
    void searchNavigationLifetime() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("missing")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[theme]\nopacity=0.45\n[modules.tray_launcher.style]\nbackground='#123456'\nforeground='#abcdef'\n"));
        Alure::TrayService tray(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
        auto module = config.model().value("modules").toMap().value("tray_launcher").toMap(); module["enabled"] = true;
        tray.configure(module); QTRY_VERIFY(tray.available());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Tray", &tray);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        {
            Alure::TrayLauncherHost host(config, engine, true); QVERIFY(host.ready());
            QQuickView *view = nullptr;
            for (auto *window : QGuiApplication::topLevelWindows()) if (window->title() == "Alure tray launcher") view = qobject_cast<QQuickView *>(window);
            QVERIFY(view); QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
            auto *root = view->rootObject(); auto *search = find(root, "tray-launcher-search"); QVERIFY(search);
            QTRY_VERIFY(search->hasActiveFocus());
            search->setProperty("text", "mUsIc"); QTRY_VERIFY(!root->property("selectedId").toString().isEmpty());
            const auto selected = root->property("selectedId");
            auto *selectedLabel = find(root, "tray-launcher-row-label-" + selected.toString()); QVERIFY(selectedLabel);
            QCOMPARE(selectedLabel->property("color").value<QColor>(), QColor("#abcdef"));
            auto *selectedBackground = find(root, "tray-launcher-row-background-" + selected.toString()); QVERIFY(selectedBackground);
            QVERIFY(qAbs(selectedBackground->property("color").value<QColor>().alphaF() - .16) < .01);

            tray.refresh(); QTRY_VERIFY(!tray.busy());
            QCOMPARE(root->property("selectedId"), selected); QCOMPARE(search->property("text").toString(), "mUsIc"); QVERIFY(search->hasActiveFocus());
            QTest::keyClick(view, Qt::Key_Return);
            QTRY_VERIFY(!root->property("registryPage").toBool()); QTRY_VERIFY(!tray.menu()->loading());
            QCOMPARE(search->property("text").toString(), "");
            QCOMPARE(view->rootObject(), root); QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
            QTRY_VERIFY(find(root, "tray-launcher-entry-2")); QVERIFY(!find(root, "tray-launcher-entry-2")->isEnabled());
            QVERIFY(!find(root, "tray-launcher-entry-3")->isEnabled()); QVERIFY(!find(root, "tray-launcher-entry-6"));
            search->setProperty("text", "mOrE"); QTRY_COMPARE(root->property("selectedId").toString(), "5");
            QTest::keyClick(view, Qt::Key_Return); QTRY_VERIFY(tray.menu()->canGoBack()); QTRY_VERIFY(!tray.menu()->loading());
            QCOMPARE(menu.shown, 5); QVERIFY(search->hasActiveFocus());
            QTest::keyClick(view, Qt::Key_Escape); QTRY_VERIFY(!tray.menu()->canGoBack()); QTRY_VERIFY(!tray.menu()->loading());
            QTRY_COMPARE(root->property("selectedId").toString(), "1");
            QTest::keyClick(view, Qt::Key_Down); QTRY_COMPARE(root->property("selectedId").toString(), "4"); // disabled/separator skipped
            QTest::keyClick(view, Qt::Key_Up); QTRY_COMPARE(root->property("selectedId").toString(), "1");
            search->setProperty("text", "check"); QTRY_COMPARE(root->property("selectedId").toString(), "4");
            menu.checked = false; emit menu.LayoutUpdated(2, 0); QTest::qWait(30);
            QCOMPARE(root->property("selectedId").toString(), "4"); QCOMPARE(search->property("text").toString(), "check"); QVERIFY(search->hasActiveFocus());
            auto *background = find(root, "tray-launcher-background"); QVERIFY(background);
            const auto color = background->property("color").value<QColor>(); QCOMPARE(color.red(), 0x12); QVERIFY(qAbs(color.alphaF() - .45) < .01);
            auto *blur = background->findChild<Alure::BackgroundBlur *>(); QVERIFY(blur); QVERIFY(blur->blurEnabled());
            QTest::keyClick(view, Qt::Key_Escape); QTRY_VERIFY(root->property("registryPage").toBool());
            QSignalSpy finished(&host, &Alure::TrayLauncherHost::finished);
            QTest::keyClick(view, Qt::Key_Escape); QTRY_COMPARE(finished.size(), 1); QVERIFY(!view->isVisible());
        }
        QTest::qWait(30); QCOMPARE(QGuiApplication::topLevelWindows().size(), 0); QCOMPARE(warnings.size(), 0);
    }
    void mouseAcknowledgement() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("missing")); QVERIFY(config.reload());
        Alure::TrayService tray(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
        auto module = config.model().value("modules").toMap().value("tray_launcher").toMap(); module["enabled"] = true; tray.configure(module); QTRY_VERIFY(tray.available());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Tray", &tray);
        Alure::TrayLauncherHost host(config, engine, true); QVERIFY(host.ready());
        QQuickView *view = nullptr; for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure tray launcher") view = qobject_cast<QQuickView *>(w);
        QVERIFY(view); auto *root = view->rootObject();
        const auto click = [&](const QString &name) { auto *entry = find(root, name); if (!entry) return false; QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, entry->mapToScene(QPointF(entry->width()/2, entry->height()/2)).toPoint()); return true; };
        QTRY_VERIFY(find(root, "tray-launcher-search")->hasActiveFocus());
        auto *label = find(root, "tray-launcher-row-label-org.alure.LauncherFixture/StatusNotifierItem"); QVERIFY(label);
        const auto theme = config.model().value("theme").toMap().value("palette").toMap();
        QCOMPARE(label->property("color").value<QColor>(), QColor(theme.value("foreground").toString()));
        QVERIFY(label->property("color").value<QColor>() != QColor(theme.value("background").toString()));
        QVERIFY(click("tray-launcher-entry-org.alure.LauncherFixture/StatusNotifierItem")); QTRY_VERIFY(!root->property("registryPage").toBool()); QTRY_VERIFY(!tray.menu()->loading());
        QTRY_VERIFY(find(root, "tray-launcher-entry-5")); QVERIFY(click("tray-launcher-entry-5")); QTRY_VERIFY(tray.menu()->canGoBack()); QTRY_VERIFY(!tray.menu()->loading());
        QSignalSpy finished(&host, &Alure::TrayLauncherHost::finished); menu.clicked = -1;
        QTRY_VERIFY(find(root, "tray-launcher-entry-7")); QVERIFY(click("tray-launcher-entry-7")); QTRY_COMPARE(menu.clicked, 7); QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!view->isVisible()); QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
    }
    void errorsBusyAndSettings() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("missing")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[modules.tray_launcher.behavior]\nallow_actions=false\n"));
        Alure::TrayService tray(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
        auto module = config.model().value("modules").toMap().value("tray_launcher").toMap(); module["enabled"] = true; tray.configure(module); QTRY_VERIFY(tray.available());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Tray", &tray);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        Alure::TrayLauncherHost host(config, engine, true); QVERIFY(host.ready());
        QQuickView *view = nullptr; for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure tray launcher") view = qobject_cast<QQuickView *>(w);
        QVERIFY(view); auto *root = view->rootObject(); QTRY_VERIFY(find(root, "tray-launcher-search")->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_Return); QTRY_VERIFY(!root->property("registryPage").toBool()); QTRY_VERIFY(!tray.menu()->loading());
        menu.clicked = -1; QTest::keyClick(view, Qt::Key_Return); QCOMPARE(menu.clicked, -1); QVERIFY(root->property("statusText").toString().contains("disabled"));
        tray.menu()->clear("Synthetic provider failure"); QTRY_VERIFY(find(root, "tray-launcher-status")->property("text").toString().contains("Synthetic provider failure"));
        QTest::keyClick(view, Qt::Key_Escape); QTRY_VERIFY(root->property("registryPage").toBool());
        // Back recovers from a failed lazy submenu and cancels an outstanding read.
        QTRY_VERIFY(!tray.busy()); QTRY_VERIFY(!root->property("selectedId").toString().isEmpty());
        QTest::keyClick(view, Qt::Key_Return); QTRY_VERIFY(!root->property("registryPage").toBool()); QTRY_VERIFY(!tray.menu()->loading());
        auto *search = find(root, "tray-launcher-search");
        search->setProperty("text", "More"); QTRY_COMPARE(root->property("selectedId").toString(), "5");
        menu.rejectSubmenu = true; QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(tray.menu()->error().contains("SubmenuFailure"));
        QTest::keyClick(view, Qt::Key_Escape); QTRY_VERIFY(!tray.menu()->loading()); QVERIFY(tray.menu()->error().isEmpty());
        menu.rejectSubmenu = false; menu.holdSubmenu = true;
        search->setProperty("text", "More"); QTRY_COMPARE(root->property("selectedId").toString(), "5");
        QTest::keyClick(view, Qt::Key_Return); QTRY_VERIFY(tray.menu()->loading());
        QTRY_COMPARE(menu.shown, 5); QVERIFY(search->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_Escape); QTRY_VERIFY(!tray.menu()->loading()); QVERIFY(!tray.menu()->canGoBack());
        menu.holdSubmenu = false;
        QQmlComponent fields(&engine); fields.setData("import QtQml\nimport \"qrc:/qml/SettingsFields.js\" as F\nQtObject { property var options: F.fields(Config.model.modules.tray_launcher, 'modules.tray_launcher') }", QUrl());
        std::unique_ptr<QObject> specs(fields.create()); QVERIFY2(specs, qPrintable(fields.errorString()));
        const auto options = specs->property("options").value<QJSValue>().toVariant().toList();
        bool hasSearch = false, hasExplicit = false;
        for (const auto &entry : options) { const auto map = entry.toMap(); if (map.value("path") == "modules.tray_launcher.behavior.search_case_sensitive") { hasSearch = true; QCOMPARE(map.value("kind").toString(), "boolean"); } if (map.value("path") == "modules.tray_launcher.behavior.explicit_launch") hasExplicit = true; }
        QVERIFY(hasSearch); QVERIFY(hasExplicit); QCOMPARE(warnings.size(), 0);
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv); QQuickStyle::setStyle("Basic"); Alure::BackgroundBlur::registerQmlType();
    TrayLauncherUiTest test; return QTest::qExec(&test, argc, argv);
}
#include "TrayLauncherUiTest.moc"

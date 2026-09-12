#include "BackgroundBlur.h"
#include "ConfigStore.h"
#include "PanelHost.h"
#include "PopupPlacement.h"
#include "NotificationService.h"
#include "ClipboardHost.h"
#include "TaskbarService.h"
#include <QLocalServer>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMessage>
#include <QScreen>
#include <QCloseEvent>
#include <QEnterEvent>
#include <QWheelEvent>
#include "IconProvider.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickStyle>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>
#include <optional>

class FixtureMenu : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items MEMBER items NOTIFY changed)
    Q_PROPERTY(bool loading MEMBER loading NOTIFY changed)
    Q_PROPERTY(QString error MEMBER error NOTIFY changed)
    Q_PROPERTY(bool canGoBack MEMBER canGoBack NOTIFY changed)
public:
    QVariantList items{QVariantMap{{"id", 1}, {"label", "_Open"}, {"enabled", true}},
                       QVariantMap{{"id", 2}, {"label", "Disabled"}, {"enabled", false}}};
    bool loading = false, canGoBack = false;
    QString error;
    int selected = -1;
    Q_INVOKABLE void select(int id) { selected = id; emit activated(); }
    Q_INVOKABLE void back() {}
    Q_INVOKABLE void close() {}
signals:
    void changed();
    void activated();
};
class FixtureService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *menu READ menu CONSTANT)
    Q_PROPERTY(bool available MEMBER available NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(bool adjusting MEMBER adjusting NOTIFY changed)
    Q_PROPERTY(QVariant pendingPercent MEMBER pendingPercent NOTIFY changed)
    Q_PROPERTY(QString diagnostic MEMBER diagnostic NOTIFY changed)
    Q_PROPERTY(QVariantMap state MEMBER state NOTIFY changed)
    Q_PROPERTY(QVariantList items MEMBER items NOTIFY changed)
public:
    FixtureMenu menuModel;
    QObject *menu() { return &menuModel; }
    QString openedMenu;
    Q_INVOKABLE void openMenu(const QString &id) { openedMenu = id; }
    bool available = true, busy = false, adjusting = false;
    QVariant pendingPercent;
    QString diagnostic;
    QVariantMap state;
    QVariantList items;
    QString lastAction;
    QVariantMap lastArguments;
    int actionCount = 0;
    Q_INVOKABLE bool action(const QString &name, const QVariantMap &arguments) { ++actionCount; lastAction = name; lastArguments = arguments; return true; }
    Q_INVOKABLE void refresh() {}
signals:
    void changed();
};
// Exercise the production notifier contract rather than FixtureService's broad
// changed signal, which intentionally remains for old UI fixture tests.
class RefreshFixture : public Alure::Service {
public:
    using Alure::Service::publish;
    using Alure::Service::fail;
    using Alure::Service::setBusy;
    QString lastAction;
    QVariantMap lastArguments;
    int actionCount = 0;
protected:
    void poll() override {}
    bool act(const QString &name, const QVariantMap &args) override {
        lastAction = name; lastArguments = args; ++actionCount; return true;
    }
};
class FixtureClipboard : public FixtureService {
    Q_OBJECT
    Q_PROPERTY(QVariantMap preview MEMBER preview NOTIFY previewChanged)
public:
    QVariantMap preview;
    QString imageUrl;
    int opened = 0, closed = 0;
    Q_INVOKABLE void openView() { ++opened; emit changed(); }
    Q_INVOKABLE void closeView() { ++closed; preview.clear(); emit previewChanged(); }
    Q_INVOKABLE void previewItem(const QString &id) {
        if (preview.value("id") == id) return;
        busy = true; emit changed();
        preview = {{"id", id}, {"kind", id == "2" ? "image" : "text"}, {"text", "<b>literal text</b>"}, {"bytes", 40}, {"width", 20}, {"height", 10}, {"imageUrl", imageUrl}};
        emit previewChanged(); busy = false; emit changed();
    }
signals:
    void previewChanged();
    void copied();
};
class FixtureShell : public QObject {
    Q_OBJECT
public:
    int closes = 0;
    Q_INVOKABLE void closePopup() { ++closes; }
    Q_INVOKABLE void closeToast() { ++closes; }
    Q_INVOKABLE bool openSettings() { return true; }
    Q_INVOKABLE void openModule(const QString &, const QString &, const QString &, QQuickItem *) {}
};
namespace {
// Lifecycle tests drive hover explicitly; queued platform Enter events from
// mapping/mask changes must not overwrite that synthetic pointer state.
class IgnoreSpontaneousHover : public QObject {
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        return event->spontaneous() && (event->type() == QEvent::Enter || event->type() == QEvent::Leave);
    }
};
QQuickItem *itemNamed(QQuickItem *item, const QString &name) {
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems()) if (auto *found = itemNamed(child, name)) return found;
    return nullptr;
}
void clickItem(QQuickWindow *window, QQuickItem *item) {
    QVERIFY(item); QVERIFY(item->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
}
void clickWithoutProcessing(QQuickWindow *window, QQuickItem *item) {
    const auto pos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QMouseEvent press(QEvent::MouseButtonPress, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, pos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
}
void revealItem(QQuickItem *item) {
    QVERIFY(item);
    for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->property("contentY").isValid()) continue;
        const auto y = item->mapToItem(parent, QPointF()).y();
        const auto maximum = qMax(0., parent->property("contentHeight").toDouble() - parent->height());
        parent->setProperty("contentY", qBound(0., parent->property("contentY").toDouble() + y - 30., maximum));
        QTest::qWait(10);
        return;
    }
}
void replaceText(QQuickWindow *window, QQuickItem *item, const QString &text) {
    QVERIFY(item);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(10, 10)).toPoint());
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window, &release);
}
}
class UiTest : public QObject {
    Q_OBJECT
private slots:
    void popupStableRefresh_data() {
        QTest::addColumn<QString>("moduleName");
        for (const auto *name : {"updates", "notifications", "wifi", "bluetooth"}) QTest::newRow(name) << QString(name);
    }
    void popupStableRefresh() {
        QFETCH(QString, moduleName);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        RefreshFixture service;
        auto module = config.model().value("modules").toMap().value(moduleName).toMap();
        auto behavior = module.value("behavior").toMap(); behavior["interval_ms"] = 60000;
        module["behavior"] = behavior; service.configure(module);
        QVariantList rows;
        for (int i = 0; i < 30; ++i) rows << QVariantMap{{"id", QString::number(i)}, {"name", "package"}, {"ssid", "Fixture"}, {"Alias", "Device"}, {"summary", "Notification"}, {"body", "Observed fixture"}, {"active", true}, {"actions", QVariantList{}}};
        QVariantMap state{{"adapters", QVariantList{QVariantMap{{"path", "/adapter"}, {"Alias", "Adapter"}, {"Powered", true}}}},
                          {"savedConnections", QVariantList{QVariantMap{{"uuid", "saved"}, {"name", "Saved"}}}}};
        service.publish(state, rows);
        QSignalSpy itemsChanged(&service, &Alure::Service::itemsChanged), stateChanged(&service, &Alure::Service::stateChanged);
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{moduleName, QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(440, 560);
        view.setInitialProperties({{"moduleName", moduleName}}); view.setSource(QUrl("qrc:/qml/Popup.qml")); QCOMPARE(view.status(), QQuickView::Ready); view.show();
        QPointer<QQuickItem> row = itemNamed(view.rootObject(), moduleName + "-text-0"); QVERIFY(row);
        auto *status = itemNamed(view.rootObject(), "provider-status"); QVERIFY(status);
        const auto text = status->property("text");
        auto *scroll = itemNamed(view.rootObject(), "popup-scroll"); QVERIFY(scroll);
        auto *flickable = scroll->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
        QTest::qWait(20);
        QVERIFY(flickable->property("contentHeight").toDouble() > flickable->height());
        const auto wheelPoint = flickable->mapToScene(QPointF(flickable->width() / 2, flickable->height() / 2)).toPoint();
        QTest::mouseMove(&view, wheelPoint);
        QWheelEvent wheel(wheelPoint, view.mapToGlobal(wheelPoint), {}, {0, -360}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &wheel);
        QTRY_VERIFY(flickable->property("contentY").toDouble() > 0.);
        QTRY_VERIFY(!flickable->property("moving").toBool());
        const auto scrollY = flickable->property("contentY").toDouble();
        QPointer<QQuickItem> nested;
        if (moduleName == "wifi") nested = itemNamed(view.rootObject(), "wifi-saved-saved");
        if (moduleName == "bluetooth") nested = itemNamed(view.rootObject(), "bluetooth-adapter-/adapter");
        if (nested) { nested->forceActiveFocus(); QVERIFY(nested->hasActiveFocus()); }
        for (int i = 0; i < 10; ++i) {
            service.setBusy(true); QCoreApplication::processEvents();
            QVERIFY(row); if (nested) { QVERIFY(nested->isEnabled()); QVERIFY(nested->hasActiveFocus()); }
            QCOMPARE(status->property("text"), text);
            service.setBusy(false); service.publish(state, rows); QCoreApplication::processEvents();
        }
        QCOMPARE(itemsChanged.size(), 0); QCOMPARE(stateChanged.size(), 0);
        QVERIFY(row); QCOMPARE(row.data(), itemNamed(view.rootObject(), moduleName + "-text-0"));
        QCOMPARE(flickable->property("contentY").toDouble(), scrollY);
        // A changed summary/state must not invalidate the independent item list.
        state["count"] = 30; service.publish(state, rows); QCoreApplication::processEvents();
        QCOMPARE(itemsChanged.size(), 0); QCOMPARE(stateChanged.size(), 1); QVERIFY(row);
        if (moduleName == "wifi" || moduleName == "bluetooth") { QTest::qWait(10); QVERIFY(nested); }
        service.setBusy(true);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "act", Q_ARG(QVariant, "fixtureAction"), Q_ARG(QVariant, (QVariantMap{{"id", "0"}}))));
        QCOMPARE(service.actionCount, 0);
        service.setBusy(false); QCOMPARE(service.actionCount, 0); // callback has not returned yet
        QTRY_COMPARE(service.actionCount, 1); QCOMPARE(service.lastArguments.value("id").toString(), "0");
        service.setBusy(true);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "act", Q_ARG(QVariant, "fixtureAction"), Q_ARG(QVariant, QVariantMap{})));
        view.hide(); service.setBusy(false); QTest::qWait(10); QCOMPARE(service.actionCount, 1);
        view.show(); service.setBusy(true);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "act", Q_ARG(QVariant, "fixtureAction"), Q_ARG(QVariant, QVariantMap{})));
        QVERIFY(config.previewText("[theme]\nopacity=0.7"));
        service.setBusy(false); QTest::qWait(10); QCOMPARE(service.actionCount, 1);
        service.fail("Fixture unavailable"); QTRY_VERIFY(!row);
        QVERIFY2(warnings.isEmpty(), "No QML warnings during refresh");
    }
    void backgroundBlurGeometryAndLifecycle() {
        QQuickWindow window; window.resize(400, 240);
        QQuickItem parent(window.contentItem()); parent.setPosition({20, 30});
        Alure::BackgroundBlur blur(&parent);
        blur.setSize({100, 60}); blur.setBlurEnabled(true); blur.setRadius(12);
        QVERIFY(blur.requestedRegion().isEmpty()); // hidden window
        window.show();
        QTRY_VERIFY(!blur.requestedRegion().isEmpty());
        QCOMPARE(blur.requestedRegion().boundingRect(), QRect(20, 30, 100, 60));
        QVERIFY(blur.requestedRegion().contains(QPoint(70, 60)));
        QVERIFY(!blur.requestedRegion().contains(QPoint(20, 30)));
        blur.setRadius(0);
        QTRY_COMPARE(blur.requestedRegion(), QRegion(20, 30, 100, 60));
        parent.setPosition({300, 220});
        QTRY_COMPARE(blur.requestedRegion(), QRegion(300, 220, 100, 20));
        window.resize(320, 230);
        QTRY_COMPARE(blur.requestedRegion(), QRegion(300, 220, 20, 10));
        blur.setWidth(0); QTRY_VERIFY(blur.requestedRegion().isEmpty());
        blur.setSize({60, 40}); parent.setPosition({10, 10});
        QTRY_COMPARE(blur.requestedRegion(), QRegion(10, 10, 60, 40));
        parent.setVisible(false); QTRY_VERIFY(blur.requestedRegion().isEmpty());
        parent.setVisible(true); QTRY_VERIFY(!blur.requestedRegion().isEmpty());
        parent.setOpacity(0); QTRY_VERIFY(blur.requestedRegion().isEmpty());
        parent.setOpacity(0.4); QTRY_VERIFY(!blur.requestedRegion().isEmpty());
        blur.setBlurEnabled(false); QTRY_VERIFY(blur.requestedRegion().isEmpty());
        blur.setBlurEnabled(true); QTRY_VERIFY(!blur.requestedRegion().isEmpty());
        window.hide(); QTRY_VERIFY(blur.requestedRegion().isEmpty());
        window.destroy(); window.show(); QTRY_VERIFY(!blur.requestedRegion().isEmpty());
        QQuickWindow other; other.resize(200, 100); other.show();
        parent.setParentItem(other.contentItem());
        QTRY_COMPARE(blur.window(), &other);
        QTRY_COMPARE(blur.requestedRegion(), QRegion(10, 10, 60, 40));
        parent.setParentItem(nullptr); QTRY_VERIFY(blur.requestedRegion().isEmpty());
    }
    void backgroundBlurReusedWindowLifetime() {
        // std::optional guarantees the same QWindow address on each emplace.
        // A stale effect keyed by that address is a fatal error on real Wayland,
        // not detectable from QML geometry assertions alone.
        std::optional<QQuickWindow> window;
        for (int iteration = 0; iteration < 32; ++iteration) {
            window.emplace(); window->resize(240, 120); window->setColor(Qt::transparent);
            auto *blur = new Alure::BackgroundBlur(window->contentItem()); // Qt owns it
            blur->setSize({200, 100}); blur->setRadius(12); blur->setBlurEnabled(true);
            window->show(); QTRY_VERIFY(!blur->requestedRegion().isEmpty());
            switch (iteration % 4) {
            case 0:
                blur->setBlurEnabled(false); QTRY_VERIFY(blur->requestedRegion().isEmpty());
                break;
            case 1:
                blur->setOpacity(0); QTRY_VERIFY(blur->requestedRegion().isEmpty());
                blur->setOpacity(1); QTRY_VERIFY(!blur->requestedRegion().isEmpty());
                blur->setWidth(0); QTRY_VERIFY(blur->requestedRegion().isEmpty());
                break;
            case 2:
                // The helper disappears while its window is still live.
                delete blur;
                break;
            case 3:
                window->destroy(); window->show(); QTRY_VERIFY(!blur->requestedRegion().isEmpty());
                window->hide(); QTRY_VERIFY(blur->requestedRegion().isEmpty());
                window->show(); QTRY_VERIFY(!blur->requestedRegion().isEmpty());
                break;
            }
            QTest::qWait(5); // Flush requests/replies before and after destruction.
            window.reset(); QTest::qWait(5);
        }
    }
    void volumeOsdBlurChurn() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.saveText("[[panels]]\nenabled=false\n[ui.osd]\nduration_ms=100\n[theme]\nblur_enabled=true"));
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        const bool native = QGuiApplication::platformName().startsWith("wayland");
        Alure::PanelHost host(config, engine, !native);
        for (int iteration = 0; iteration < 120; ++iteration) {
            // Only observed fixture values: never create an audio/system service.
            host.showOsd({{"kind", "volume"}, {"percent", iteration % 2 ? 20 : 80}, {"muted", iteration % 3 == 0}});
            QQuickView *osd = nullptr;
            for (auto *candidate : QGuiApplication::topLevelWindows())
                if (candidate->title() == "Alure OSD" && candidate->isVisible()) osd = qobject_cast<QQuickView *>(candidate);
            QVERIFY(osd);
            auto *blur = osd->rootObject()->findChild<Alure::BackgroundBlur *>(); QVERIFY(blur);
            QTRY_VERIFY(!blur->requestedRegion().isEmpty());
            QTest::qWait(5);
            if (iteration % 10 == 0) { host.closeOsd(); QTest::qWait(5); }
        }
        host.closeOsd(); QTest::qWait(20);
        QCOMPARE(warnings.size(), 0);
    }
    void backgroundBlurSurfaces_data() {
        QTest::addColumn<QString>("surface");
        for (const auto *surface : {"Panel", "calendar", "media", "clipboard", "TrayMenu", "Toast", "Osd", "ClipboardOverlay"})
            QTest::newRow(surface) << QString(surface);
    }
    void backgroundBlurSurfaces() {
        QFETCH(QString, surface);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        const QString source = "[theme]\nopacity=0.4\n[[panels]]\nmodules=['calendar']";
        QVERIFY(config.previewText(source));
        FixtureService service; FixtureClipboard clipboard; FixtureShell shell;
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"tray", QVariant::fromValue(&service)}, {"media", QVariant::fromValue(&service)}, {"notifications", QVariant::fromValue(&service)}, {"clipboard", QVariant::fromValue(&clipboard)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(800, 600);
        QVariantMap properties;
        QString file = surface;
        if (surface == "Panel") properties = {{"panel", config.model().value("panels").toList().first()}, {"vertical", false}, {"outputName", "fixture"}};
        else if (surface == "calendar" || surface == "media" || surface == "clipboard") {
            file = "ModulePopup"; properties = {{"moduleName", surface}, {"popupPadding", 24}};
        } else if (surface == "TrayMenu") properties = {{"trayItem", "fixture"}, {"popupPadding", 24}};
        else if (surface == "Toast") properties = {{"notification", QVariantMap{{"id", "1"}, {"appName", "Fixture"}, {"summary", "Summary"}, {"body", "Body"}}}};
        else if (surface == "Osd") properties = {{"snapshot", QVariantMap{{"kind", "volume"}, {"percent", 40}}}};
        view.setInitialProperties(properties);
        view.setSource(QUrl("qrc:/qml/" + file + ".qml"));
        QCOMPARE(view.status(), QQuickView::Ready); view.show();
        auto *blur = view.rootObject()->findChild<Alure::BackgroundBlur *>(); QVERIFY(blur);
        if (surface == "ClipboardOverlay") {
            QTest::qWait(20); QVERIFY(blur->requestedRegion().isEmpty()); // full-screen cursor probe
            view.rootObject()->setProperty("anchorPosition", QPointF(50, 60));
            view.rootObject()->setProperty("cardVisible", true);
        }
        QTRY_VERIFY(!blur->requestedRegion().isEmpty());
        const auto rect = blur->mapRectToScene(blur->boundingRect()).toAlignedRect();
        QCOMPARE(blur->requestedRegion().boundingRect(), rect);
        QVERIFY(!blur->requestedRegion().contains(rect.topLeft()));
        if (file == "ModulePopup" || file == "TrayMenu") QCOMPARE(rect.topLeft(), QPoint(24, 24));
        if (file == "ClipboardOverlay") QVERIFY(rect.size() != view.size());
        const qreal originalOpacity = blur->parentItem()->opacity();
        QVERIFY(config.previewText(source + "\n[theme]" ) == false); // invalid draft retains the valid request
        QVERIFY(blur->blurEnabled());
        QVERIFY(config.previewText("[theme]\nblur_enabled=false\nopacity=0.4\n[[panels]]\nmodules=['calendar']"));
        QTRY_VERIFY(blur->requestedRegion().isEmpty());
        QCOMPARE(blur->parentItem()->opacity(), originalOpacity);
        QVERIFY(config.previewText(source)); QTRY_VERIFY(!blur->requestedRegion().isEmpty());
        const auto previous = blur->requestedRegion();
        view.resize(720, 520); QTRY_VERIFY(blur->requestedRegion() != previous);
        view.rootObject()->setVisible(false); QTRY_VERIFY(blur->requestedRegion().isEmpty());
        view.rootObject()->setVisible(true); QTRY_VERIFY(!blur->requestedRegion().isEmpty());
        view.hide(); QTRY_VERIFY(blur->requestedRegion().isEmpty());
        QCOMPARE(warnings.size(), 0);
    }
    void backgroundBlurSettings() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QQmlEngine engine; engine.rootContext()->setContextProperty("Config", &config);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nimport \"qrc:/qml/SettingsFields.js\" as Fields\nQtObject { property var field: Fields.describe('theme.blur_enabled', Config.model.theme.blur_enabled) }", QUrl());
        std::unique_ptr<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
        const auto field = object->property("field").value<QJSValue>();
        QCOMPARE(field.property("kind").toString(), "boolean");
        QCOMPARE(field.property("label").toString(), "Frosted glass background");
        engine.addImageProvider("icons", new Alure::IconProvider);
        QQmlComponent settings(&engine, QUrl("qrc:/qml/Settings.qml"));
        std::unique_ptr<QObject> form(settings.create()); QVERIFY2(form, qPrintable(settings.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(form.get()); QVERIFY(window);
        QQuickItem *toggle = nullptr;
        QTRY_VERIFY(toggle = itemNamed(window->contentItem(), "field-theme.blur_enabled-switch"));
        QVERIFY(toggle->property("checked").toBool());
        revealItem(toggle); clickItem(window, toggle);
        QVERIFY(form->property("dirty").toBool());
        QVERIFY(config.model().value("theme").toMap().value("blur_enabled").toBool()); // still a draft
        QVERIFY(QMetaObject::invokeMethod(form.get(), "save"));
        QVERIFY(config.reload()); QVERIFY(!config.model().value("theme").toMap().value("blur_enabled").toBool());
    }
    void panelGapSettings() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nimport \"qrc:/qml/SettingsFields.js\" as Fields\nQtObject { property var spec: Fields.describe('panels.0.window_gap', -16); property string main: Fields.panelGapHelp({id:'top-main',edge:'top',output:'primary',exclusive_zone:-1,window_gap:-16,visibility:{mode:'always'}}); property string hidden: Fields.panelGapHelp({id:'bottom-taskbar',edge:'bottom',output:'primary',visibility:{mode:'auto-hide'}}) }", QUrl());
        std::unique_ptr<QObject> fields(component.create()); QVERIFY2(fields, qPrintable(component.errorString()));
        const auto spec = fields->property("spec").value<QJSValue>();
        QCOMPARE(spec.property("low").toInt(), -256); QCOMPARE(spec.property("high").toInt(), 256);
        QVERIFY(spec.property("help").toString().contains("THIS panel"));
        QVERIFY(fields->property("main").toString().contains("top-main"));
        QVERIFY(fields->property("main").toString().contains("-16 px"));
        QVERIFY(fields->property("hidden").toString().contains("reserves no space"));
        QQmlComponent settings(&engine, QUrl("qrc:/qml/Settings.qml"));
        std::unique_ptr<QObject> form(settings.create()); QVERIFY2(form, qPrintable(settings.errorString()));
        form->setProperty("section", 1);
        auto *window = qobject_cast<QQuickWindow *>(form.get()); QVERIFY(window);
        auto *help = itemNamed(window->contentItem(), "panel-gap-help"); QVERIFY(help);
        QTRY_VERIFY(help->isVisible()); QVERIFY(help->property("text").toString().contains("main"));
    }
    void panelAutoHideLifecycle_data() {
        QTest::addColumn<QString>("edge");
        QTest::addColumn<int>("margin");
        for (const auto *edge : {"top", "bottom", "left", "right"})
            for (int margin : {0, 4, 24})
                QTest::newRow(qPrintable(QString("%1-margin-%2").arg(edge).arg(margin))) << QString(edge) << margin;
    }
    void panelAutoHideLifecycle() {
        QFETCH(QString, edge);
        QFETCH(int, margin);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        const QString source = QString("[ui]\nshow_settings=false\n[[panels]]\nedge='%1'\nlayer='top'\nthickness=16\nlength=300\nmodules=['calendar']\nmargins={top=%2,bottom=%2,left=%2,right=%2}\nvisibility={mode='auto-hide',show_delay_ms=10,hide_delay_ms=120,edge_trigger_px=16}").arg(edge).arg(margin);
        QVERIFY(config.previewText(source));
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{});
        Alure::PanelHost host(config, engine, true);
        QPointer<QQuickView> body, trigger;
        for (auto *window : QGuiApplication::topLevelWindows()) {
            if (window->title() == "Alure · main") body = qobject_cast<QQuickView *>(window);
            if (window->title() == "Alure edge · main") trigger = qobject_cast<QQuickView *>(window);
        }
        QVERIFY(body); QVERIFY(trigger);
        IgnoreSpontaneousHover hover;
        body->installEventFilter(&hover); trigger->installEventFilter(&hover);
        const auto size = body->size();
        const bool vertical = edge == "left" || edge == "right";
        const bool farEdge = edge == "bottom" || edge == "right";
        const int cross = vertical ? trigger->width() : trigger->height();
        const QRegion bridge(margin == 0 ? QRect(-1, -1, 1, 1)
            : vertical ? QRect(farEdge ? cross - margin : 0, 0, margin, trigger->height())
                       : QRect(0, farEdge ? cross - margin : 0, trigger->width(), margin));
        QCOMPARE(trigger->mask(), bridge); // Initial revealed mask excludes the body too.
        const auto effectiveBridge = bridge.intersected(QRegion(QRect(QPoint(), trigger->size())));
        QVERIFY(effectiveBridge.translated(trigger->position()).intersected(QRegion(body->geometry())).isEmpty());
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(body, &leave); QCoreApplication::sendEvent(trigger, &leave);
        QTRY_VERIFY(!body->property("panelBodyVisible").toBool());
        QVERIFY(body->isVisible()); // Mapped size stays stable; only scene/input changes.
        QVERIFY(!body->rootObject()->isVisible());
        QVERIFY(body->mask().intersected(QRegion(QRect(QPoint(), size))).isEmpty());
        QCOMPARE(vertical ? trigger->mask().boundingRect().width() : trigger->mask().boundingRect().height(), 16);
        const QPointF edgePoint = vertical ? QPointF(farEdge ? cross - 1 : 1, 1) : QPointF(1, farEdge ? cross - 1 : 1);
        QEnterEvent enter(edgePoint, edgePoint, edgePoint);
        QCoreApplication::sendEvent(trigger, &enter); QCoreApplication::sendEvent(trigger, &leave);
        QTest::qWait(30); QVERIFY(!body->property("panelBodyVisible").toBool()); // Short edge pass cancels reveal.
        QCoreApplication::sendEvent(trigger, &enter);
        QTRY_VERIFY(body->property("panelBodyVisible").toBool());
        QCOMPARE(body->size(), size); QVERIFY(body->rootObject()->isVisible());
        QCOMPARE(body->mask(), QRegion(QRect(QPoint(), size)));
        QCOMPARE(trigger->mask(), bridge);
        QEnterEvent bodyEnter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
        // The stationary edge point is now over the body (zero margin), or
        // still over the bridge. A mask shrink is not a physical pointer leave.
        QTest::qWait(180); QVERIFY(body->property("panelBodyVisible").toBool());
        QCoreApplication::sendEvent(trigger, &leave);
        if (margin == 0) {
            // Ownership-induced trigger Leave must not undo the transfer before
            // the new body's native Enter has arrived (possibly delayed).
            QTest::qWait(180); QVERIFY(body->property("panelBodyVisible").toBool());
            QCoreApplication::sendEvent(body, &bodyEnter);
            QCoreApplication::sendEvent(body, &leave);
        }
        QTRY_VERIFY(!body->property("panelBodyVisible").toBool());
        QCoreApplication::sendEvent(trigger, &enter); QTRY_VERIFY(body->property("panelBodyVisible").toBool());
        QCoreApplication::sendEvent(body, &bodyEnter); QCoreApplication::sendEvent(trigger, &leave);
        QMouseEvent outsideMove(QEvent::MouseMove, QPointF(-5, -5), QPointF(-5, -5), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(body, &outsideMove); // Implicit grab, no Leave.
        QTRY_VERIFY(!body->property("panelBodyVisible").toBool());
        QCoreApplication::sendEvent(trigger, &enter); QTRY_VERIFY(body->property("panelBodyVisible").toBool());
        QCoreApplication::sendEvent(body, &bodyEnter); QCoreApplication::sendEvent(trigger, &leave);
        auto *anchor = itemNamed(body->rootObject(), "calendar-button"); QVERIFY(anchor);
        host.openModule("calendar", "main", body->screen()->name(), anchor);
        // Deliberately omit parent Leave: native popup grabs can consume it.
        QTest::qWait(180); QVERIFY(body->property("panelBodyVisible").toBool()); // Queued popup and open popup pin it.
        host.closePopup(); QTest::qWait(180);
        QVERIFY(body->property("panelBodyVisible").toBool()); // Latest local evidence is still inside.
        QCoreApplication::sendEvent(body, &leave); QTRY_VERIFY(!body->property("panelBodyVisible").toBool());
        QCoreApplication::sendEvent(trigger, &enter); QTRY_VERIFY(body->property("panelBodyVisible").toBool());
        QCoreApplication::sendEvent(body, &bodyEnter); QCoreApplication::sendEvent(trigger, &leave);
        host.openModule("calendar", "main", body->screen()->name(), anchor);
        QCoreApplication::sendEvent(body, &outsideMove); // Latest known point is outside, popup still pins.
        QTest::qWait(180); QVERIFY(body->property("panelBodyVisible").toBool());
        host.closePopup(); QTRY_VERIFY(!body->property("panelBodyVisible").toBool());
        QVERIFY(!config.previewText(source + "\nvisibility.mode='always'")); // TOML duplicate is rejected without rebuilding.
        QVERIFY(trigger); QVERIFY(body);
        QString always = source; always.replace("mode='auto-hide'", "mode='always'");
        QVERIFY(config.previewText(always));
        QTRY_VERIFY(trigger.isNull()); QTRY_VERIFY(body.isNull());
        for (auto *window : QGuiApplication::topLevelWindows()) QVERIFY(window->title() != "Alure edge · main");
        QVERIFY(config.previewText("panels=[]"));
        QTRY_VERIFY([&] { for (auto *window : QGuiApplication::topLevelWindows()) if (window->title() == "Alure · main") return false; return true; }());
    }
    void taskbarLayoutPreservesDelegates() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        // Default panels remain always-visible: layout traffic must not churn
        // the taskbar even when no dodge panel is consuming the shared stream.
        QLocalServer server; QVERIFY(server.listen(dir.filePath("niri.sock")));
        QPointer<QLocalSocket> stream;
        connect(&server, &QLocalServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, &server, [&, socket] {
                if (!socket->canReadLine()) return;
                QCOMPARE(socket->readLine().trimmed(), QByteArray("\"EventStream\""));
                stream = socket;
                socket->write(R"({"WorkspacesChanged":{"workspaces":[{"id":10,"output":"A","is_active":true,"is_focused":true}]}}
{"WindowsChanged":{"windows":[{"id":2,"title":"Stable task","app_id":"org.test.App","workspace_id":10,"is_focused":true}]}}
)");
            });
        });
        Alure::TaskbarService service; FixtureShell shell;
        auto module = config.model().value("modules").toMap().value("taskbar").toMap();
        auto behavior = module.value("behavior").toMap();
        behavior["socket_path"] = server.fullServerName(); module["behavior"] = behavior;
        service.configure(module); QTRY_VERIFY(service.available()); QVERIFY(stream);
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"taskbar", QVariant::fromValue(&service)}});
        engine.rootContext()->setContextProperty("Shell", &shell);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"moduleName", "taskbar"}, {"vertical", false}, {"crossSize", 36}, {"outputName", "A"}});
        view.setSource(QUrl("qrc:/qml/ModuleStrip.qml")); QVERIFY(view.status() == QQuickView::Ready);
        view.resize(220, 36); view.show();
        QTRY_VERIFY(itemNamed(view.rootObject(), "taskbar-entry-2"));
        QPointer<QQuickItem> entry = itemNamed(view.rootObject(), "taskbar-entry-2");
        const auto items = service.items();
        QSignalSpy changed(&service, &Alure::Service::changed);
        QSignalSpy geometry(&service, &Alure::TaskbarService::panelWindowsChanged);
        stream->write(R"({"WindowLayoutsChanged":{"changes":[[2,{"tile_pos_in_workspace_view":[10,20],"window_size":[400,260],"window_offset_in_tile":[0,0]}]]}}
)"); stream->flush();
        QTRY_COMPARE(geometry.count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(changed.count(), 0); QCOMPARE(service.items(), items);
        QVERIFY(service.panelWindows().first().toMap().value("layout").toMap().contains("window_size"));
        QVERIFY(entry); QCOMPARE(itemNamed(view.rootObject(), "taskbar-entry-2"), entry.data());
    }
    void taskbarStripScopesAndActions() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[modules.workspaces]\nenabled=false\n[modules.taskbar.style]\nshow_label=true\ntask_width=140\nlabel_size=18\n[modules.taskbar.behavior]\nworkspace_scope='active'\noutput_scope='panel'"));
        FixtureService service; FixtureShell shell;
        service.items = {QVariantMap{{"id", "9007199254740993"}, {"title", "Observed title"}, {"app_id", "org.test.App"}, {"output", "A"}, {"workspace_active", true}, {"workspace_focused", true}, {"output_focused", true}, {"is_focused", true}},
                         QVariantMap{{"id", "2"}, {"title", "Other output"}, {"output", "B"}, {"workspace_active", true}},
                         QVariantMap{{"id", "3"}, {"title", "Hidden workspace"}, {"output", "A"}, {"workspace_active", false}}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"taskbar", QVariant::fromValue(&service)}});
        engine.rootContext()->setContextProperty("Shell", &shell);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"moduleName", "taskbar"}, {"vertical", false}, {"crossSize", 36}, {"outputName", "A"}});
        view.setSource(QUrl("qrc:/qml/ModuleStrip.qml")); QVERIFY(view.status() == QQuickView::Ready); view.resize(220, 36); view.show(); QTest::qWait(30);
        auto *entry = itemNamed(view.rootObject(), "taskbar-entry-9007199254740993"); QVERIFY(entry);
        QCOMPARE(entry->width(), 140.); QCOMPARE(entry->property("text").toString(), "Observed title"); QVERIFY(entry->property("accent").toBool());
        QVERIFY(!itemNamed(view.rootObject(), "taskbar-entry-2")); QVERIFY(!itemNamed(view.rootObject(), "taskbar-entry-3"));
        clickItem(&view, entry); QCOMPARE(service.lastAction, "activate"); QCOMPARE(service.lastArguments.value("id").toString(), "9007199254740993");
        QVERIFY(config.previewText("[modules.workspaces]\nenabled=false\n[modules.taskbar.behavior]\noutput_scope='all'\nfocus_on_click=false")); QTest::qWait(30);
        entry = itemNamed(view.rootObject(), "taskbar-entry-9007199254740993"); QVERIFY(entry); QVERIFY(!entry->isEnabled()); QCOMPARE(entry->property("text").toString(), "");
        QVERIFY(itemNamed(view.rootObject(), "taskbar-entry-2")); QVERIFY(itemNamed(view.rootObject(), "taskbar-entry-3"));
        view.rootObject()->setProperty("vertical", true); view.resize(40, 180); QTest::qWait(20);
        QCOMPARE(entry->width(), 40.); QVERIFY(entry->height() > 0);
        QVERIFY(config.previewText("[modules.taskbar.behavior]\noutput_scope='focused'\nworkspace_scope='focused'")); QTest::qWait(20);
        QVERIFY(!itemNamed(view.rootObject(), "taskbar-entry-2")); QVERIFY(!itemNamed(view.rootObject(), "taskbar-entry-3"));
        service.items.clear(); emit service.changed(); QTest::qWait(20);
        QVERIFY(!itemNamed(view.rootObject(), "taskbar-entry-9007199254740993"));
        QVERIFY(view.rootObject()->property("listMode").toBool()); // Empty live taskbar is not an unavailable/launcher button.
        service.available = false; emit service.changed(); QTest::qWait(20);
        QVERIFY(itemNamed(view.rootObject(), "taskbar-button")->isVisible());
        QQmlComponent metadata(&engine); metadata.setData(R"(import QtQml
import "qrc:/qml/SettingsFields.js" as Fields
import "qrc:/qml/Ui.js" as Ui
QtObject { property var order: Fields.describe("modules.taskbar.behavior.ordering", "id"); property var scope: Fields.describe("modules.taskbar.behavior.output_scope", "panel"); property var size: Fields.describe("modules.taskbar.style.task_width", 160); property string title: Ui.title("taskbar") })", QUrl());
        QScopedPointer<QObject> fields(metadata.create()); QVERIFY2(fields, qPrintable(metadata.errorString()));
        QCOMPARE(fields->property("order").value<QJSValue>().toVariant().toMap().value("options").toStringList(), (QStringList{"id", "app-id", "title"}));
        QCOMPARE(fields->property("scope").value<QJSValue>().toVariant().toMap().value("kind").toString(), "enum");
        QCOMPARE(fields->property("size").value<QJSValue>().toVariant().toMap().value("high").toInt(), 512);
        QCOMPARE(fields->property("title").toString(), "Task Manager");
    }
    void rightSidePopupActions() {
        for (const auto &name : {QString("notifications"), QString("workspaces"), QString("taskbar")}) for (int width : {240, 480}) {
            QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
            FixtureService service; FixtureShell shell;
            service.state = {{"dnd", false}};
            service.items = {QVariantMap{{"id", "7"}, {"summary", "Long notification text wraps without covering actions"}, {"body", "Body with additional details"}, {"appName", "Fixture"}, {"createdAt", 1700000000000LL}, {"active", true}, {"name", "Development workspace"}, {"idx", 1}, {"output", "A"}, {"is_active", false}}};
            QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
            engine.rootContext()->setContextProperty("Config", &config);
            engine.rootContext()->setContextProperty("Services", QVariantMap{{name, QVariant::fromValue(&service)}});
            engine.rootContext()->setContextProperty("Shell", &shell);
            QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.setInitialProperties({{"moduleName", name}, {"outputName", "A"}}); view.setSource(QUrl("qrc:/qml/Popup.qml"));
            QVERIFY(view.status() == QQuickView::Ready); view.resize(width, 720); view.show(); QTest::qWait(40);
            auto *text = itemNamed(view.rootObject(), name + "-text-7"); auto *activate = itemNamed(view.rootObject(), name + "-activate-7");
            QVERIFY(text); QVERIFY(activate); QVERIFY(text->width() > 0);
            QVERIFY(activate->mapToScene(QPointF()).x() >= text->mapToScene(QPointF(text->width(), 0)).x());
            QVERIFY(activate->mapToScene(QPointF(activate->width(), 0)).x() <= width);
            QCOMPARE(activate->property("text").toString(), ""); QCOMPARE(activate->property("iconSource").toString(), "image://icons/builtin/next");
            QCOMPARE(QQmlProperty(activate, "Accessible.name", qmlContext(activate)).read().toString(), name == "notifications" ? "Open sender" : name == "taskbar" ? "Focus window" : "Switch here");
            revealItem(activate); clickItem(&view, activate); QCOMPARE(service.lastAction, "activate"); QCOMPARE(service.lastArguments.value("id").toString(), "7");
            if (name == "notifications") {
                auto *dismiss = itemNamed(view.rootObject(), "notifications-dismiss-7"); QVERIFY(dismiss);
                QVERIFY(dismiss->mapToScene(QPointF(dismiss->width(), 0)).x() <= width);
                QCOMPARE(QQmlProperty(dismiss, "Accessible.name", qmlContext(dismiss)).read().toString(), "Dismiss");
                clickItem(&view, dismiss); QCOMPARE(service.lastAction, "dismiss");
            }
            QVERIFY(config.previewText("[modules." + name + ".behavior]\nallow_actions=false")); QTest::qWait(20); QVERIFY(!activate->isEnabled());
        }
    }
    void taskbarDesktopIcons() {
        QTemporaryDir data; const auto old = qgetenv("XDG_DATA_HOME"); qputenv("XDG_DATA_HOME", data.path().toUtf8());
        QDir().mkpath(data.filePath("applications"));
        QImage image(24, 24, QImage::Format_ARGB32); image.fill(Qt::red); QVERIFY(image.save(data.filePath("app.png")));
        QFile desktop(data.filePath("applications/org.alure.IconFixture.desktop")); QVERIFY(desktop.open(QIODevice::WriteOnly));
        desktop.write(("[Desktop Entry]\nType=Application\nName=Fixture\nIcon=" + data.filePath("app.png") + "\nExec=never-run\n").toUtf8()); desktop.close();
        Alure::IconProvider provider; QSize size;
        const auto icon = provider.requestPixmap("app/org.alure.IconFixture/taskbar", &size, QSize(24,24));
        QCOMPARE(icon.toImage().pixelColor(12,12), QColor(Qt::red));
        const auto fallback = provider.requestPixmap("app/org.alure.DoesNotExist/taskbar", &size, QSize(24,24)); QVERIFY(!fallback.isNull()); QVERIFY(fallback.toImage() != icon.toImage());
        const auto unsafe = provider.requestPixmap("app/..%2Foutside/taskbar", &size, QSize(24,24)); QCOMPARE(unsafe.toImage(), fallback.toImage());
        if (old.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", old);
    }
    void brightnessPopupContinuousDrag() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service; FixtureShell shell;
        QVariantMap screen{{"available", true}, {"canSet", true}, {"device", "intel"}, {"level", 40}, {"maximum", 100}, {"percent", 40}, {"minimum", 1}, {"limit", 100}};
        QVariantMap keyboard{{"available", true}, {"canSet", true}, {"device", "platform::kbd_backlight"}, {"level", 1}, {"maximum", 2}, {"percent", 50}, {"minimum", 0}, {"limit", 2}};
        service.state = {{"screen", screen}, {"keyboard", keyboard}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"brightness", QVariant::fromValue(&service)}});
        QStringList warnings; connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings << error.toString(); });
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(440, 560);
        view.setInitialProperties({{"moduleName", "brightness"}}); view.setSource(QUrl("qrc:/qml/Popup.qml")); QCOMPARE(view.status(), QQuickView::Ready);
        view.show(); QTest::qWait(40);
        auto *slider = itemNamed(view.rootObject(), "screen-brightness-slider"); QVERIFY(slider); QVERIFY(slider->isEnabled());
        auto *kbd = itemNamed(view.rootObject(), "keyboard-brightness-slider"); QVERIFY(kbd); QCOMPARE(kbd->property("to").toInt(), 2); QCOMPARE(kbd->property("stepSize").toInt(), 1);
        clickItem(&view, itemNamed(view.rootObject(), "keyboard-brightness-up")); QCOMPARE(service.lastAction, "adjustBrightness"); QCOMPARE(service.lastArguments.value("kind").toString(), "keyboard"); QCOMPARE(service.lastArguments.value("delta").toInt(), 1);
        auto *handle = slider->property("handle").value<QQuickItem *>(); QVERIFY(handle);
        const auto start = handle->mapToScene(QPointF(handle->width()/2, handle->height()/2)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, start); QTest::mouseMove(&view, start + QPoint(30, 0));
        const auto held = slider->property("value").toDouble();
        screen["percent"] = 10; service.state["screen"] = screen; service.busy = true; service.adjusting = true; emit service.changed();
        QCOMPARE(slider->property("value").toDouble(), held); QVERIFY(slider->property("pressed").toBool()); QVERIFY(slider->isEnabled());
        QTest::mouseMove(&view, start + QPoint(70, 0)); const auto desired = slider->property("value").toDouble();
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, start + QPoint(70, 0));
        QCOMPARE(service.lastAction, "setBrightness"); QCOMPARE(service.lastArguments.value("kind").toString(), "screen"); QVERIFY(desired > held);
        service.busy = false; emit service.changed(); QCOMPARE(slider->property("value").toDouble(), desired);
        service.adjusting = false; emit service.changed(); QCOMPARE(slider->property("value").toDouble(), 10.);
        keyboard["canSet"] = false; service.state["keyboard"] = keyboard; emit service.changed(); QVERIFY(!kbd->isEnabled());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void brightnessWheelAndSettings() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service; service.state = {{"screen", QVariantMap{{"canSet", true}}}, {"keyboard", QVariantMap{{"canSet", true}}}, {"percent", 40}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"brightness", QVariant::fromValue(&service)}});
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(180, 48);
        view.setInitialProperties({{"moduleName", "brightness"}, {"vertical", false}, {"crossSize", 48}});
        view.setSource(QUrl("qrc:/qml/ModuleStrip.qml")); QCOMPARE(view.status(), QQuickView::Ready); view.show(); QTest::qWait(30);
        auto *button = itemNamed(view.rootObject(), "brightness-button"); QVERIFY(button);
        const auto pos = button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint(); QTest::mouseMove(&view, pos);
        const auto wheel = [&](int delta) { QWheelEvent event(pos, view.mapToGlobal(pos), {}, {0, delta}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false); QCoreApplication::sendEvent(&view, &event); };
        wheel(120); QCOMPARE(service.lastAction, "adjustBrightness"); QCOMPARE(service.lastArguments.value("kind").toString(), "screen"); QCOMPARE(service.lastArguments.value("delta").toDouble(), 5.);
        QVERIFY(config.saveText("[modules.brightness.behavior]\nscroll_target='keyboard'\nscroll_inverted=true"));
        const int count = service.actionCount; wheel(60); QCOMPARE(service.actionCount, count); wheel(60); QCOMPARE(service.actionCount, count + 1);
        QCOMPARE(service.lastArguments.value("kind").toString(), "keyboard"); QCOMPARE(service.lastArguments.value("delta").toInt(), -1);
        QVERIFY(config.saveText("[modules.brightness.behavior]\nscroll_enabled=false")); wheel(120); QCOMPARE(service.actionCount, count + 1);
        QQmlComponent fields(&engine);
        fields.setData("import QtQml\nimport \"qrc:/qml/SettingsFields.js\" as Fields\nQtObject { property var level: Fields.describe('modules.brightness.behavior.keyboard.max_level', -1); property var color: Fields.describe('ui.osd.accent', ''); property var osd: Fields.fields(Config.model.ui.osd, 'ui.osd'); property var devices: Fields.fields(Config.model.modules.brightness.behavior, 'modules.brightness.behavior') }", QUrl());
        std::unique_ptr<QObject> object(fields.create()); QVERIFY2(object, qPrintable(fields.errorString()));
        QCOMPARE(object->property("level").value<QJSValue>().property("low").toInt(), -1);
        QCOMPARE(object->property("color").value<QJSValue>().property("kind").toString(), "color");
        QCOMPARE(object->property("osd").value<QJSValue>().property("length").toInt(), config.model().value("ui").toMap().value("osd").toMap().size());
        QVERIFY(object->property("devices").value<QJSValue>().property("length").toInt() > 20);
    }
    void osdPassiveWindowAndDuration() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload()); QVERIFY(config.saveText("[[panels]]\nenabled=false\n[ui.osd]\nduration_ms=150"));
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QStringList warnings; connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) { for (const auto &error : errors) warnings << error.toString(); });
        Alure::PanelHost host(config, engine, true);
        const auto osds = [] { QList<QWindow *> result; for (auto *w : QGuiApplication::allWindows()) if (w->title() == "Alure OSD" && w->isVisible()) result << w; return result; };
        host.showOsd({{"kind", "volume"}, {"percent", 40}, {"muted", true}});
        QCOMPARE(osds().size(), 1); auto *window = osds().first();
        QVERIFY(window->flags().testFlag(Qt::WindowDoesNotAcceptFocus)); QVERIFY(window->flags().testFlag(Qt::WindowTransparentForInput));
        auto *view = qobject_cast<QQuickView *>(window); QVERIFY(view); QCOMPARE(view->status(), QQuickView::Ready);
        QVERIFY(view->rootObject()->property("muted").toBool()); QCOMPARE(view->rootObject()->property("label").toString(), "Muted");
        QPointer<QQuickView> original = view;
        QPointer<QQuickItem> originalRoot = view->rootObject();
        QTest::qWait(90);
        host.showOsd({{"kind", "volume"}, {"percent", 75}, {"muted", false}});
        QCOMPARE(osds().first(), original.data()); QCOMPARE(original->rootObject(), originalRoot.data());
        QCOMPARE(itemNamed(originalRoot, "osd-value")->property("text").toString(), "75%");
        QTest::qWait(90); QVERIFY(original); QVERIFY(original->isVisible()); // original deadline has passed
        QTRY_VERIFY(osds().isEmpty()); QVERIFY(!original); QVERIFY(!originalRoot);
        host.showOsd({{"kind", "keyboard"}, {"percent", 50}, {"level", 1}, {"maximum", 2}});
        QCOMPARE(osds().size(), 1); view = qobject_cast<QQuickView *>(osds().first()); QVERIFY(view);
        QCOMPARE(itemNamed(view->rootObject(), "osd-value")->property("text").toString(), "1 / 2");
        original = view; originalRoot = view->rootObject();
        host.showOsd({{"kind", "screen"}, {"percent", 80}}); QCOMPARE(osds().size(), 1);
        QCOMPARE(osds().first(), original.data()); QCOMPARE(original->rootObject(), originalRoot.data());
        QCOMPARE(itemNamed(originalRoot, "osd-value")->property("text").toString(), "80%");
        QVERIFY(config.saveText("[[panels]]\nenabled=false\n[ui.osd]\nenabled=false")); QTest::qWait(20); QVERIFY(osds().isEmpty());
        host.showOsd({{"kind", "screen"}, {"percent", 30}}); QVERIFY(osds().isEmpty());
        QVERIFY(config.saveText("[[panels]]\nenabled=false\n[ui.osd]\noutput='nonexistent-output'")); QTest::qWait(20);
        host.showOsd({{"kind", "screen"}, {"percent", 30}}); QVERIFY(osds().isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void volumeWheelInput() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service;
        service.state = {{"percent", 40}, {"canSetVolume", true}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"volume", QVariant::fromValue(&service)}, {"media", QVariant::fromValue(&service)}});
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(180, 48);
        view.setInitialProperties({{"moduleName", "volume"}, {"vertical", false}, {"crossSize", 48}});
        view.setSource(QUrl("qrc:/qml/ModuleStrip.qml")); QCOMPARE(view.status(), QQuickView::Ready); view.show(); QTest::qWait(30);
        auto *button = itemNamed(view.rootObject(), "volume-button"); QVERIFY(button);
        QSignalSpy requested(view.rootObject(), SIGNAL(requested(QQuickItem*))); QVERIFY(requested.isValid());
        const auto pos = button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint();
        QTest::mouseMove(&view, pos);
        const auto wheel = [&](QPoint delta, QPoint position) {
            QWheelEvent event(position, view.mapToGlobal(position), {}, delta, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&view, &event);
        };
        wheel({0, 120}, pos); QCOMPARE(service.lastAction, "adjustVolume"); QCOMPARE(service.lastArguments.value("delta").toDouble(), 5.);
        wheel({0, -120}, pos); QCOMPARE(service.lastArguments.value("delta").toDouble(), -5.);
        wheel({0, 60}, pos); QCOMPARE(service.lastArguments.value("delta").toDouble(), 2.5);
        QCOMPARE(service.actionCount, 3); QCOMPARE(requested.count(), 0);
        wheel({120, 0}, pos); wheel({0, 120}, {-10, -10}); QCOMPARE(service.actionCount, 3);
        clickItem(&view, button); QCOMPARE(requested.count(), 1); // wheel handling must not steal clicks
        QVERIFY(config.saveText("[modules.volume.behavior]\nscroll_step=2\nscroll_inverted=true"));
        wheel({0, 120}, pos); QCOMPARE(service.lastArguments.value("delta").toDouble(), -2.);
        const auto count = service.actionCount;
        for (const auto &source : {"[modules.volume.behavior]\nscroll_enabled=false", "[modules.volume.behavior]\nallow_actions=false", "[modules.volume]\nenabled=false"}) {
            QVERIFY(config.saveText(source)); wheel({0, 120}, pos); QCOMPARE(service.actionCount, count);
        }
        QVERIFY(config.saveText("[modules.volume.behavior]\nscroll_enabled=true"));
        service.busy = true; emit service.changed(); wheel({0, 120}, pos); QCOMPARE(service.actionCount, count + 1);
        service.available = false; emit service.changed(); wheel({0, 120}, pos); QCOMPARE(service.actionCount, count + 1);
        service.available = true; service.state["canSetVolume"] = false; emit service.changed(); wheel({0, 120}, pos); QCOMPARE(service.actionCount, count + 1);
        service.state["canSetVolume"] = true; emit service.changed(); view.rootObject()->setProperty("moduleName", "media");
        wheel({0, 120}, pos); QCOMPARE(service.actionCount, count + 1);
    }
    void volumeContinuousDrag_data() {
        QTest::addColumn<int>("width"); QTest::newRow("normal") << 440; QTest::newRow("narrow") << 240;
    }
    void volumeContinuousDrag() {
        QFETCH(int, width);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service; FixtureShell shell;
        service.state = {{"backend", "pulseaudio"}, {"percent", 40}, {"canSetVolume", true}, {"canMute", true}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"volume", QVariant::fromValue(&service)}});
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(width, 560);
        view.setInitialProperties({{"moduleName", "volume"}}); view.setSource(QUrl("qrc:/qml/Popup.qml")); QCOMPARE(view.status(), QQuickView::Ready);
        view.show(); QTest::qWait(30);
        auto *slider = itemNamed(view.rootObject(), "volume-slider"); QVERIFY(slider);
        auto *handle = slider->property("handle").value<QQuickItem *>(); QVERIFY(handle);
        const auto start = handle->mapToScene(QPointF(handle->width()/2, handle->height()/2)).toPoint();
        const auto initialY = slider->mapToScene(QPointF()).y();
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&view, start + QPoint(40, 0)); QTest::qWait(20);
        const auto dragged = slider->property("value").toDouble();
        service.state["percent"] = 10; emit service.changed();
        const bool heldLocalValue = slider->property("value").toDouble() == dragged;
        service.busy = true; service.adjusting = true; emit service.changed();
        const bool heldGrab = slider->isEnabled() && slider->property("pressed").toBool();
        const auto before = service.actionCount;
        QTest::mouseMove(&view, start + QPoint(80, 0)); QTest::qWait(20);
        const auto desired = slider->property("value").toDouble();
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, start + QPoint(80, 0));
        service.busy = false; emit service.changed();
        const bool heldPendingValue = slider->property("value").toDouble() == desired;
        service.state["percent"] = desired; service.adjusting = false; emit service.changed();
        service.state["percent"] = 23; emit service.changed();
        QVERIFY2(heldLocalValue, "Polling must not overwrite a pressed slider");
        QVERIFY2(heldGrab, "An audio command must not disable/cancel the ongoing drag");
        QVERIFY(service.actionCount > before); QVERIFY(desired > dragged); QVERIFY(heldPendingValue);
        QCOMPARE(slider->property("value").toDouble(), 23.);
        QCOMPARE(slider->mapToScene(QPointF()).y(), initialY);
    }
    void volumePendingFeedbackIsNotObserved() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service; FixtureShell shell;
        service.state = {{"backend", "pulseaudio"}, {"percent", 40}, {"canSetVolume", true}, {"canMute", true}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"volume", QVariant::fromValue(&service)}});
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(440, 560);
        view.setInitialProperties({{"moduleName", "volume"}}); view.setSource(QUrl("qrc:/qml/Popup.qml")); QCOMPARE(view.status(), QQuickView::Ready);
        view.show(); QTest::qWait(30);
        auto *status = itemNamed(view.rootObject(), "provider-status"), *slider = itemNamed(view.rootObject(), "volume-slider");
        QVERIFY(status); QVERIFY(slider);
        const auto initialY = slider->mapToScene(QPointF()).y();
        service.pendingPercent = 75.; service.adjusting = true; service.busy = true; emit service.changed();
        QCOMPARE(status->property("text").toString(), "Pending volume: 75%");
        QCOMPARE(service.state.value("percent").toInt(), 40);
        QCOMPARE(slider->property("value").toDouble(), 40.); // no invented observation
        QVERIFY(slider->isEnabled()); QCOMPARE(slider->mapToScene(QPointF()).y(), initialY);
        service.pendingPercent = QVariant{}; service.adjusting = false; service.busy = false; service.state["percent"] = 73; emit service.changed();
        QCOMPARE(slider->property("value").toDouble(), 73.);
        QVERIFY(!status->property("text").toString().contains("Pending"));
        service.diagnostic = "Audio action failed: Fixture rejection"; service.available = false; emit service.changed();
        QCOMPARE(status->property("text").toString(), service.diagnostic);
    }
    void volumeBackendCapabilities() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service; FixtureShell shell;
        service.state = {{"backend", "pulseaudio"}, {"percent", 40}, {"muted", false}, {"canSetVolume", true}, {"canMute", false}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"volume", QVariant::fromValue(&service)}});
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(440, 560);
        view.setInitialProperties({{"moduleName", "volume"}}); view.setSource(QUrl("qrc:/qml/Popup.qml")); QCOMPARE(view.status(), QQuickView::Ready);
        view.show(); QTest::qWait(30);
        auto *slider = itemNamed(view.rootObject(), "volume-slider"), *mute = itemNamed(view.rootObject(), "volume-mute"); QVERIFY(slider); QVERIFY(mute);
        QVERIFY(slider->isEnabled()); QVERIFY(!mute->isEnabled());
        service.state["canSetVolume"] = false; service.state["canMute"] = true; emit service.changed();
        QVERIFY(!slider->isEnabled()); QVERIFY(mute->isEnabled());
        clickItem(&view, mute); QCOMPARE(service.lastAction, "toggleMute");
    }
    void moduleIconTooltips() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.saveText("[settings]\nmodule_tooltip_delay_ms=10"));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(1000, 800); window->setProperty("section", 1); QTest::qWait(40);
        QObject *tooltip = nullptr;
        for (const auto *name : {"palette-module-media--1", "panels.0.modules-module-media-1"}) {
            auto *tile = itemNamed(window->contentItem(), name); QVERIFY(tile); revealItem(tile);
            auto *block = itemNamed(tile, "module-tile-block"); QVERIFY(block);
            tooltip = QQmlProperty::read(block, "ToolTip.toolTip", qmlContext(block)).value<QObject *>(); QVERIFY(tooltip);
            const auto point = tile->mapToScene(QPointF(tile->width()/2, tile->height()/2)).toPoint();
            QTest::mouseMove(window, point);
            QTRY_VERIFY(tooltip->property("visible").toBool());
            QVERIFY(tooltip->property("text").toString().contains("Now playing"));
            QVERIFY(tooltip->property("text").toString().contains("playback"));
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_VERIFY(!tooltip->property("visible").toBool());
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseMove(window, QPoint(5, 5));
            QTRY_VERIFY(!tooltip->property("visible").toBool());
        }
        QVERIFY(config.previewText("[settings]\nmodule_tooltips=false\nmodule_tooltip_delay_ms=0"));
        auto *tile = itemNamed(window->contentItem(), "palette-module-media--1"); QVERIFY(tile); revealItem(tile);
        QTest::mouseMove(window, tile->mapToScene(QPointF(tile->width()/2, tile->height()/2)).toPoint());
        QTest::qWait(50); QVERIFY(!tooltip->property("visible").toBool());
        QVERIFY(warnings.isEmpty()); QVERIFY(window->close());
    }
    void moduleSlotIconsAndPlayerFields() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.saveText("[modules.media.style]\nicon='calendar'\n[modules.wifi.behavior]\npreferred_player='legacy'\n[[panels]]\nlayout='three-zone'\nmodules_left=['media','@settings']\nmodules_center=['calendar']\nmodules_right=['clipboard','@spacer']"));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->setProperty("section", 1); QTest::qWait(60);
        const auto find = [&](const QString &name) { return itemNamed(window->contentItem(), name); };
        auto *tile = find("panels.0.modules_left-module-media-0"); QVERIFY(tile);
        QCOMPARE(tile->width(), tile->height());
        QVERIFY(!itemNamed(tile, "module-tile-label"));
        auto *icon = itemNamed(tile, "module-tile-icon"); QVERIFY(icon); QVERIFY(icon->isVisible());
        QCOMPARE(icon->property("source").toUrl(), QUrl("image://icons/builtin/calendar"));
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        auto *palette = find("palette-module-media--1"); QVERIFY(palette);
        QCOMPARE(palette->width(), palette->height()); QVERIFY(!itemNamed(palette, "module-tile-label"));
        QCOMPARE(itemNamed(palette, "module-tile-icon")->property("source").toUrl(), QUrl("image://icons/builtin/calendar"));
        for (const auto &token : {"@spacer", "@stretch", "@settings"}) {
            auto *special = find(QString("palette-module-%1--1").arg(token)); QVERIFY(special);
            QCOMPARE(special->width(), special->height()); QVERIFY(!itemNamed(special, "module-tile-label"));
            auto *specialIcon = itemNamed(special, "module-tile-icon"); QVERIFY(specialIcon);
            QTRY_COMPARE(specialIcon->property("status").toInt(), 1);
        }
        for (const auto &name : config.model().value("modules").toMap().keys()) {
            window->setProperty("moduleName", name); window->setProperty("section", 2); QTest::qWait(20);
            QCOMPARE(find("setting-field-modules." + name + ".behavior.preferred_player") != nullptr, name == "media");
        }
        QVERIFY(warnings.isEmpty()); QVERIFY(window->close());
    }
    void settingsLayoutSettles_data() {
        QTest::addColumn<QSize>("extent");
        QTest::newRow("default") << QSize(900, 680);
        QTest::newRow("narrow") << QSize(400, 500);
        QTest::newRow("wide") << QSize(1280, 800);
    }
    void settingsLayoutSettles() {
        QFETCH(QSize, extent);
        QTest::failOnWarning(QRegularExpression(".*(recursive rearrange|Binding loop|polish loop).*"));
        QTemporaryDir dir;
        Alure::ConfigStore config(dir.filePath("settings.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText(QString("[settings]\nwidth=%1\nheight=%2\n").arg(extent.width()).arg(extent.height())));
        QQmlApplicationEngine engine;
        engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/Settings.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QTimer heartbeat; heartbeat.setInterval(10); QSignalSpy ticks(&heartbeat, &QTimer::timeout); heartbeat.start();
        QTest::qWait(60); QVERIFY(!ticks.isEmpty()); QVERIFY(window->isVisible());
        for (int group = 0; group < 3; ++group) {
            window->setProperty("appearanceGroup", group); QTest::qWait(30);
        }
        window->setProperty("section", 1); QTest::qWait(40);
        for (const auto &name : config.model().value("modules").toMap().keys()) {
            window->setProperty("moduleName", name); window->setProperty("section", 2); QTest::qWait(25);
        }
        window->setProperty("section", 4); QTest::qWait(30);
        window->setProperty("section", 0); window->setProperty("appearanceGroup", 0);
        window->resize(extent + QSize(50, 30)); QTest::qWait(50);
        auto *choice = itemNamed(window->contentItem(), "field-theme.name-choice"); QVERIFY(choice);
        auto *field = itemNamed(window->contentItem(), "setting-field-theme.name"); QVERIFY(field);
        QVERIFY(choice->width() > 0); QVERIFY(choice->width() < field->width() * 0.6);
        QVERIFY(choice->mapToItem(field, QPointF{}).x() > field->width() * 0.4);
        const int before = ticks.count(); QTest::qWait(80); QVERIFY(ticks.count() > before);
        QVERIFY(warnings.isEmpty()); QVERIFY(window->close());
    }
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
            if (name != "media") view.setInitialProperties({{"moduleName", name}});
            view.setSource(QUrl(name == "media" ? "qrc:/qml/MediaPopup.qml" : "qrc:/qml/Popup.qml"));
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
        QCOMPARE(entry->property("iconName").toString(), showIcon && moduleName == "tray" ? QString("calendar") : QString());
        if (moduleName == "workspaces") {
            auto *shared = itemNamed(view.rootObject(), "workspaces-shared-icon"); QVERIFY(shared);
            QCOMPARE(shared->isVisible(), showIcon);
            QCOMPARE(shared->property("iconName").toString(), "calendar");
            QCOMPARE(entry->property("highlightBackground").toBool(), false);
        }
        QCOMPARE(entry->property("iconSource").toString(), moduleName == "tray" ? pixmapUrl : QString());
        std::function<QQuickItem *(QQuickItem *)> findImage = [&](QQuickItem *item) -> QQuickItem * {
            if (item->property("source").isValid()) return item;
            for (auto *child : item->childItems()) if (auto *found = findImage(child)) return found;
            return nullptr;
        };
        auto *icon = findImage(entry); QVERIFY(icon);
        QCOMPARE(icon->isVisible(), showIcon && moduleName == "tray");
        const QString expectedSource = showIcon && moduleName == "tray" ? pixmapUrl : QString();
        QCOMPARE(icon->property("source").toUrl().toString(), expectedSource);
        if (moduleName == "tray") {
            auto row = service.items.first().toMap(); row["iconUrl"] = ""; service.items = {row}; emit service.changed();
            QTRY_VERIFY(entry = findItem(view.rootObject(), moduleName + "-entry-20"));
            QCOMPARE(entry->property("iconSource").toString(), "image://icons/theme/battery");
        }
        QCOMPARE(warnings.size(), 0);
    }
    void clipboardCursorHost() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureClipboard service;
        QQmlEngine engine; engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"clipboard", QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        Alure::ClipboardHost host(config, engine, true); QSignalSpy finished(&host, &Alure::ClipboardHost::finished);
        QQuickView *view = nullptr;
        for (auto *window : QGuiApplication::topLevelWindows()) if (window->title() == "Alure clipboard") view = qobject_cast<QQuickView *>(window);
        QVERIFY(view);
        QEnterEvent enter(QPointF(321, 210), QPointF(321, 210), QPointF(9999, 9999)); QCoreApplication::sendEvent(view, &enter);
        QTRY_VERIFY(view->rootObject()->property("cardVisible").toBool());
        QCOMPARE(view->rootObject()->property("anchorPosition").toPointF(), QPointF(321, 210));
        QTest::keyClick(view, Qt::Key_Escape); QCOMPARE(finished.size(), 1); QVERIFY(!view->isVisible());
        QCOMPARE(warnings.size(), 0);
    }
    void clipboardView() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureClipboard service; FixtureShell shell;
        QImage image(20, 10, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(dir.filePath("image.png")));
        service.imageUrl = QUrl::fromLocalFile(dir.filePath("image.png")).toString();
        service.items = {QVariantMap{{"id", "1"}, {"label", "Text fixture"}}, QVariantMap{{"id", "2"}, {"label", "Image fixture"}}};
        QQmlEngine engine; engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"clipboard", QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(800, 600);
        view.setInitialProperties({{"cardVisible", true}, {"anchorPosition", QPointF(760, 580)}});
        view.setSource(QUrl("qrc:/qml/ClipboardOverlay.qml")); QVERIFY(view.status() == QQuickView::Ready); view.show(); QTest::qWait(30);
        const auto find = [&](const QString &name) { return itemNamed(view.rootObject(), name); };
        auto *card = find("clipboard-card"); QVERIFY(card);
        QVERIFY(card->x() >= 0 && card->x() + card->width() <= view.width());
        QVERIFY(card->y() >= 0 && card->y() + card->height() <= view.height());
        QCOMPARE(service.opened, 1); QCOMPARE(find("clipboard-text")->property("text").toString(), "<b>literal text</b>");
        replaceText(&view, find("clipboard-search"), "Image"); QTRY_COMPARE(find("clipboard-history")->property("count").toInt(), 1);
        QTRY_COMPARE(find("clipboard-image")->property("status").toInt(), 1); QVERIFY(find("clipboard-image")->isVisible());
        clickItem(&view, find("clipboard-copy")); QCOMPARE(service.lastAction, "copy"); QCOMPARE(service.lastArguments.value("id").toString(), "2");
        view.requestActivate(); QTest::qWait(10); service.lastAction.clear();
        QTest::keyClick(&view, Qt::Key_Return); QCOMPARE(service.lastAction, "copy");
        const int before = service.actionCount; clickItem(&view, find("clipboard-delete")); QCOMPARE(service.actionCount, before);
        replaceText(&view, find("clipboard-search"), "Text"); QTRY_COMPARE(service.preview.value("id").toString(), "1");
        QVERIFY(!find("clipboard-confirm")->isVisible());
        clickItem(&view, find("clipboard-delete"));
        QTest::qWait(20); clickItem(&view, find("clipboard-confirm")); QCOMPARE(service.lastAction, "delete"); QVERIFY(service.lastArguments.value("confirmed").toBool());
        view.hide(); QCOMPARE(service.closed, 1); QVERIFY(service.preview.isEmpty());
        QCOMPARE(warnings.size(), 0);
    }
    void notificationDndToToast() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.saveText("[ui]\nshow_settings=false\n[ui.toast]\nduration_ms=300\n[modules.notifications.behavior]\nserver_enabled=true\ndnd=true\n[[panels]]\nmodules=['notifications']"));
        Alure::NotificationService service;
        const auto notificationConfig = [&] { return config.model().value("modules").toMap().value("notifications").toMap(); };
        connect(&config, &Alure::ConfigStore::modelChanged, &service, [&] { service.configure(notificationConfig()); });
        service.configure(notificationConfig()); QTRY_VERIFY(service.available()); QVERIFY(service.state().value("dnd").toBool());
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"notifications", QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        Alure::PanelHost host(config, engine, true);
        connect(&service, &Alure::Service::changed, &host, [&] { host.syncNotifications(service.items()); });
        const auto window = [](const QString &title) -> QQuickView * {
            for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == title && w->isVisible()) return qobject_cast<QQuickView *>(w);
            return nullptr;
        };
        const auto notify = [](const QString &summary) {
            auto message = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");
            message.setArguments({"Fixture", uint(0), "", summary, "Body", QStringList{}, QVariantMap{}, 0});
            return QDBusConnection::sessionBus().asyncCall(message, 1000);
        };
        QDBusPendingCallWatcher suppressed(notify("Suppressed")); QTRY_VERIFY(suppressed.isFinished());
        QVERIFY(!QDBusPendingReply<uint>(suppressed).isError()); QCOMPARE(service.items().size(), 1);
        QVERIFY(!window("Alure notification"));
        QTRY_VERIFY(window("Alure · main")); auto *bar = window("Alure · main");
        clickItem(bar, itemNamed(bar->rootObject(), "notifications-button")); QTRY_VERIFY(window("Alure details"));
        auto *details = window("Alure details"); clickItem(details, itemNamed(details->rootObject(), "notification-dnd"));
        QTRY_VERIFY(!service.state().value("dnd").toBool());
        QCOMPARE(service.items().size(), 1); QVERIFY(service.items().first().toMap().value("active").toBool());
        QVERIFY(!window("Alure notification")); // no replay of earlier DND history
        Alure::ConfigStore disk(config.path()); QVERIFY(disk.reload());
        QVERIFY(!disk.model().value("modules").toMap().value("notifications").toMap().value("behavior").toMap().value("dnd").toBool());
        QTest::qWait(20); // allow deferred panel rebuild after the saved DND setting
        QDBusPendingCallWatcher fresh(notify("Fresh after DND")); QTRY_VERIFY(fresh.isFinished());
        QTRY_VERIFY(window("Alure notification"));
        QCOMPARE(window("Alure notification")->rootObject()->property("notification").toMap().value("summary").toString(), "Fresh after DND");
        QTRY_VERIFY(!window("Alure notification")); QCOMPARE(service.state().value("activeCount").toInt(), 2);
        QVERIFY(config.previewText(config.editLiteral(config.source(), "modules.notifications.behavior.persist_dnd", "false").value("text").toString()));
        const auto sessionOnlySource = config.source(); QTest::qWait(20);
        bar = window("Alure · main"); QVERIFY(bar); clickItem(bar, itemNamed(bar->rootObject(), "notifications-button"));
        QTRY_VERIFY(window("Alure details")); details = window("Alure details");
        clickItem(details, itemNamed(details->rootObject(), "notification-dnd"));
        QTRY_VERIFY(service.state().value("dnd").toBool()); QCOMPARE(config.source(), sessionOnlySource);
        QVERIFY(disk.reload()); QVERIFY(!disk.model().value("modules").toMap().value("notifications").toMap().value("behavior").toMap().value("dnd").toBool());
        auto disabled = notificationConfig(); disabled["enabled"] = false; service.configure(disabled);
        service.configure(disk.model().value("modules").toMap().value("notifications").toMap()); QTRY_VERIFY(service.available());
        QVERIFY(!service.state().value("dnd").toBool());
        QDBusPendingCallWatcher restarted(notify("Fresh after restart")); QTRY_VERIFY(restarted.isFinished());
        QTRY_VERIFY(window("Alure notification"));
        QCOMPARE(warnings.size(), 0);
    }
    void mediaCardControls() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[modules.media.behavior]\nartwork_remote=false"));
        FixtureService service;
        QVariantMap row{{"service", "fixture.player"}, {"identity", "Fixture player"}, {"title", "Fixture track"}, {"artist", QStringList{"Artist A", "Artist B"}}, {"album", "Fixture album"},
                        {"artUrl", "https://example.invalid/cover.png"}, {"trackId", "/fixture/track"}, {"lengthUs", 150000000LL}, {"positionUs", 10000000LL},
                        {"playbackStatus", "Playing"}, {"CanControl", true}, {"CanPlay", true}, {"CanPause", true}, {"CanSeek", true}, {"CanGoPrevious", true}, {"CanGoNext", false},
                        {"hasShuffle", true}, {"shuffle", false}, {"hasLoopStatus", true}, {"loopStatus", "None"}};
        service.items = {row}; FixtureShell shell;
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"media", QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(360, 460);
        view.setSource(QUrl("qrc:/qml/MediaPopup.qml")); QVERIFY(view.status() == QQuickView::Ready); view.show(); QTest::qWait(40);
        const auto find = [&](const QString &name) { return itemNamed(view.rootObject(), name); };
        QCOMPARE(find("media-title")->property("text").toString(), "Fixture track");
        QCOMPARE(find("media-artist")->property("text").toString(), "Artist A, Artist B");
        QVERIFY(find("media-artwork")->property("source").toUrl().isEmpty());
        auto *play = find("media-play-pause");
        const auto queuePlay = [&] {
            service.busy = true; emit service.changed();
            QVERIFY(play->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatch", Q_ARG(QVariant, "playPause"), Q_ARG(QVariant, QVariantMap{})));
        };
        queuePlay(); QCOMPARE(service.actionCount, 0);
        service.busy = false; emit service.changed(); QCOMPARE(service.actionCount, 0);
        QTRY_COMPARE(service.actionCount, 1);
        queuePlay(); view.hide(); service.busy = false; emit service.changed(); QTest::qWait(10); QCOMPARE(service.actionCount, 1);
        view.show(); queuePlay();
        row["service"] = "different.player"; service.items = {row}; emit service.changed();
        service.busy = false; emit service.changed(); QTest::qWait(10); QCOMPARE(service.actionCount, 1);
        row["service"] = "fixture.player"; service.items = {row}; emit service.changed();
        queuePlay(); QVERIFY(config.previewText("[theme]\nopacity=0.6\n[modules.media.behavior]\nartwork_remote=false"));
        service.busy = false; emit service.changed(); QTest::qWait(10); QCOMPARE(service.actionCount, 1);
        QVERIFY(find("media-shuffle-glyph")->scale() >= 1.);
        for (const auto &theme : {"midnight", "dawn"}) {
            QVERIFY(config.previewText(QString("[theme]\nname='%1'\n[modules.media.behavior]\nartwork_remote=false").arg(theme)));
            clickItem(&view, find("media-player-choice")); QTest::qWait(20);
            auto *choice = itemNamed(view.contentItem(), "media-source-0"); QVERIFY(choice);
            QTest::mouseMove(&view, choice->mapToScene(QPointF(choice->width()/2, choice->height()/2)).toPoint());
            QTRY_VERIFY(choice->property("hovered").toBool());
            const auto textColor = itemNamed(choice, "media-source-label-0")->property("color").value<QColor>();
            const auto fill = itemNamed(choice, "media-source-background-0")->property("color").value<QColor>();
            QVERIFY(textColor != fill);
            clickItem(&view, choice);
        }
        revealItem(play); clickItem(&view, play); QCOMPARE(service.lastAction, "playPause");
        QVERIFY(!find("media-next")->isEnabled());
        clickItem(&view, find("media-shuffle")); QCOMPARE(service.lastArguments.value("shuffle").toBool(), true);
        clickItem(&view, find("media-repeat")); QCOMPARE(service.lastArguments.value("loopStatus").toString(), "Playlist");
        auto *slider = find("media-progress"); revealItem(slider);
        const auto start = slider->mapToScene(QPointF(slider->width() * .6, slider->height()/2)).toPoint();
        const auto end = slider->mapToScene(QPointF(slider->width() * .75, slider->height()/2)).toPoint();
        const int before = service.actionCount;
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, start); QTest::mouseMove(&view, end);
        row["positionUs"] = 2000000LL; service.items = {row}; emit service.changed(); QTest::qWait(10);
        QVERIFY(slider->property("pressed").toBool()); QVERIFY(slider->property("value").toDouble() > 80.);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(service.actionCount, before + 1); QCOMPARE(service.lastAction, "setPosition");
        QCOMPARE(service.lastArguments.value("trackId").toString(), "/fixture/track");
        QVERIFY(service.lastArguments.value("positionUs").toDouble() > 80000000.);
        QVERIFY(config.previewText("[modules.media.behavior]\nshow_artwork=false\nshow_progress=false\nshow_shuffle=false\nshow_repeat=false"));
        QVERIFY(!slider->isVisible()); QVERIFY(!find("media-shuffle")->isVisible());
        QCOMPARE(warnings.size(), 0);
    }
    void workspaceStripLabels() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        FixtureService service;
        for (int i = 1; i <= 5; ++i) service.items << QVariantMap{{"id", QString::number(i)}, {"idx", i}, {"name", ""}, {"output", "fixture"}, {"is_active", i == 1}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"workspaces", QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(240, 32);
        view.setInitialProperties({{"moduleName", "workspaces"}, {"vertical", false}, {"crossSize", 32}});
        view.setSource(QUrl("qrc:/qml/ModuleStrip.qml")); view.show(); QTest::qWait(30);
        auto *list = itemNamed(view.rootObject(), "workspaces-list"); QVERIFY(list);
        QVERIFY(list->property("contentWidth").toDouble() <= list->width());
        QVERIFY(itemNamed(view.rootObject(), "workspaces-shared-icon")->isVisible());
        for (int i = 1; i <= 5; ++i) {
            auto *entry = itemNamed(view.rootObject(), "workspaces-entry-" + QString::number(i)); QVERIFY(entry);
            QCOMPARE(entry->property("text").toString(), QString::number(i));
            QVERIFY(entry->property("iconName").toString().isEmpty());
        }
        QCOMPARE(warnings.size(), 0);
    }
    void panelLayoutGeometry_data() {
        QTest::addColumn<bool>("vertical"); QTest::addColumn<bool>("zoned"); QTest::addColumn<int>("length");
        for (const bool vertical : {false, true}) for (const bool zoned : {false, true}) for (int length : {900, 180})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(vertical).arg(zoned).arg(length))) << vertical << zoned << length;
    }
    void panelLayoutGeometry() {
        QFETCH(bool, vertical); QFETCH(bool, zoned); QFETCH(int, length);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText(QString("[ui]\nshow_settings=false\n[[panels]]\nlayout='%1'\nmodules=['@stretch','calendar','@spacer','volume','@stretch']\nmodules_left=['workspaces']\nmodules_center=['calendar']\nmodules_right=['volume']\nspacer_size=40").arg(zoned ? "three-zone" : "linear")));
        FixtureService service; FixtureShell shell; service.available = false;
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Shell", &shell);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"volume", QVariant::fromValue(&service)}, {"workspaces", QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickView view(&engine, nullptr); view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(vertical ? 44 : length, vertical ? length : 44);
        view.setInitialProperties({{"panel", config.model().value("panels").toList().first()}, {"vertical", vertical}, {"outputName", "fixture"}});
        view.setSource(QUrl("qrc:/qml/Panel.qml")); QVERIFY(view.status() == QQuickView::Ready); view.show(); QTest::qWait(40);
        const auto find = [&](const QString &name) { return itemNamed(view.rootObject(), name); };
        auto *scroll = find("panel-modules"); auto *left = find("panel-zone-left"); auto *center = find("panel-zone-center"); auto *right = find("panel-zone-right");
        const auto position = [vertical](QQuickItem *item) { return vertical ? item->y() : item->x(); };
        const auto size = [vertical](QQuickItem *item) { return vertical ? item->height() : item->width(); };
        const auto total = scroll->property(vertical ? "contentHeight" : "contentWidth").toDouble();
        if (zoned) {
            QCOMPARE(position(left), 0.); QVERIFY(qAbs(position(center) + size(center)/2 - total/2) < .01);
            QVERIFY(qAbs(position(right) + size(right) - total) < .01);
            QVERIFY(size(left) <= position(center)); QVERIFY(position(center) + size(center) <= position(right));
        } else {
            auto *first = find("panel-slot-@stretch-0"); auto *last = find("panel-slot-@stretch-4"); auto *spacer = find("panel-slot-@spacer-2");
            QVERIFY(first); QVERIFY(last); QVERIFY(spacer); QCOMPARE(size(spacer), 40.); QCOMPARE(size(first), size(last));
            QVERIFY(size(first) >= 0.); if (length == 900) QVERIFY(size(first) > 0.);
        }
        QCOMPARE(warnings.size(), 0);
    }
    void settingsZoneMoves() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.saveText("# preserved\n[[panels]]\nlayout='three-zone'\nmodules_left=['media']\nmodules_center=['calendar']\nmodules_right=['battery']\n"));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings); engine.load(QUrl("qrc:/qml/Settings.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); QTest::qWait(30);
        const auto find = [&](const QString &name) { return itemNamed(window->contentItem(), name); };
        clickItem(window, find("settings-section-1")); QTest::qWait(20);
        QVERIFY(!find("setting-field-panels.0.modules"));
        auto *center = find("setting-field-panels.0.modules_center"); QVERIFY(center);
        auto *battery = itemNamed(center, "panel-module-battery"); revealItem(battery); clickItem(window, battery);
        auto *right = find("setting-field-panels.0.modules_right"); QVERIFY(right);
        auto *media = itemNamed(right, "panel-module-media"); revealItem(media); clickItem(window, media);
        auto *add = find("field-panels.0.modules_right-add-spacer"); revealItem(add); clickItem(window, add);
        clickItem(window, find("settings-save")); QTRY_VERIFY(!window->property("dirty").toBool());
        const auto panel = config.model().value("panels").toList()[0].toMap();
        QCOMPARE(panel.value("modules_left").toList(), QVariantList{});
        QCOMPARE(panel.value("modules_center").toList(), (QVariantList{"calendar", "battery"}));
        QCOMPARE(panel.value("modules_right").toList(), (QVariantList{"media", "@spacer"}));
        QVERIFY(config.source().startsWith("# preserved")); QCOMPARE(warnings.size(), 0);
    }
    void trayRightClickMenu() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[ui]\nshow_settings=false\n[[panels]]\nmodules=['tray']"));
        FixtureService service;
        service.items = {QVariantMap{{"id", "fixture"}, {"Title", "Fixture tray"}}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{"tray", QVariant::fromValue(&service)}});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        Alure::PanelHost host(config, engine, true);
        QQuickView *bar = nullptr;
        for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure · main") bar = qobject_cast<QQuickView *>(w);
        QVERIFY(bar); QTest::qWait(20);
        auto *entry = itemNamed(bar->rootObject(), "tray-entry-fixture"); QVERIFY(entry);
        QTest::mouseClick(bar, Qt::RightButton, Qt::NoModifier, entry->mapToScene(QPointF(entry->width()/2, entry->height()/2)).toPoint());
        QQuickView *popup = nullptr;
        QTRY_VERIFY(([&] { for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure tray menu" && w->isVisible()) popup = qobject_cast<QQuickView *>(w); return popup; })());
        QCOMPARE(popup->type(), Qt::Popup); QCOMPARE(popup->transientParent(), bar);
        QCOMPARE(service.openedMenu, "fixture"); QVERIFY(service.lastAction.isEmpty());
        QTest::qWait(20);
        auto *disabled = itemNamed(popup->rootObject(), "tray-menu-item-2"); QVERIFY(disabled); QVERIFY(!disabled->isEnabled());
        auto *open = itemNamed(popup->rootObject(), "tray-menu-item-1"); QVERIFY(open); QCOMPARE(open->property("text").toString(), "Open");
        clickItem(popup, open); QTRY_VERIFY(!popup->isVisible()); QCOMPARE(service.menuModel.selected, 1);
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
    void popupEvents_data() {
        QTest::addColumn<QString>("edge");
        for (const auto &edge : {"top", "bottom", "left", "right"}) QTest::newRow(edge) << QString(edge);
    }
    void popupEvents() {
        QFETCH(QString, edge);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        const auto text = QString("[ui]\nshow_settings=false\nclose_on_focus_loss=false\n[[panels]]\nedge='%1'\nlength=700\nmodules=['calendar','volume','media']\n").arg(edge);
        QVERIFY(config.previewText(text));
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        FixtureService service; service.available = false;
        QVariantMap services; for (const auto &name : {"volume", "media"}) services[name] = QVariant::fromValue(&service);
        engine.rootContext()->setContextProperty("Services", services);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QSignalSpy lastClosed(qGuiApp, &QGuiApplication::lastWindowClosed);
        Alure::PanelHost host(config, engine, true);
        const auto barWindow = []() -> QQuickView * {
            for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure · main") return qobject_cast<QQuickView *>(w);
            return nullptr;
        };
        const auto popupWindow = []() -> QQuickView * {
            for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure details" && w->isVisible()) return qobject_cast<QQuickView *>(w);
            return nullptr;
        };
        auto *bar = barWindow(); QVERIFY(bar); QTest::qWait(30);
        const bool vertical = edge == "left" || edge == "right";
        QList<int> positions;
        for (const auto &name : {"calendar", "volume", "media"}) {
            auto *button = itemNamed(bar->rootObject(), QString(name) + "-button"); QVERIFY(button);
            const QRect source = Alure::visiblePopupAnchor(button);
            clickItem(bar, button);
            QTRY_VERIFY(popupWindow()); auto *popup = popupWindow();
            QCOMPARE(popup->transientParent(), bar); QCOMPARE(popup->type(), Qt::Popup);
            const auto p = Alure::popupPlacement(source, bar->size(), bar->screen()->size(), edge, config.model().value("ui").toMap());
            QCOMPARE(popup->property("_q_waylandPopupAnchorRect").toRect(), p.anchorRect);
            QCOMPARE(popup->property("_q_waylandPopupAnchor").value<Qt::Edges>(), p.anchor);
            QCOMPARE(popup->property("_q_waylandPopupGravity").value<Qt::Edges>(), p.gravity);
            QCOMPARE(popup->property("_q_waylandPopupConstraintAdjustment").toUInt(), 15u);
            positions << (vertical ? p.anchorRect.y() : p.anchorRect.x());
            QTest::keyClick(popup, Qt::Key_Escape); QTRY_VERIFY(!popupWindow());
            QVERIFY(bar->isVisible()); QCOMPARE(lastClosed.size(), 0);
        }
        QVERIFY(positions[0] < positions[1]); QVERIFY(positions[1] < positions[2]);
        // The same source button toggles closed, and reopening is a new click.
        auto *toggle = itemNamed(bar->rootObject(), "calendar-button");
        clickItem(bar, toggle); QTRY_VERIFY(popupWindow());
        clickItem(bar, toggle); QTRY_VERIFY(!popupWindow());
        // Escape also works after a child control takes focus.
        clickItem(bar, toggle); QTRY_VERIFY(popupWindow());
        itemNamed(popupWindow()->rootObject(), "popup-close")->forceActiveFocus();
        QTest::keyClick(popupWindow(), Qt::Key_Escape); QTRY_VERIFY(!popupWindow());
        // Reopen and close via actual widget and native close event, without closing the shell's bar.
        auto *button = itemNamed(bar->rootObject(), "calendar-button");
        clickItem(bar, button); QTRY_VERIFY(popupWindow());
        clickItem(popupWindow(), itemNamed(popupWindow()->rootObject(), "popup-close")); QTRY_VERIFY(!popupWindow());
        clickItem(bar, button); QTRY_VERIFY(popupWindow());
        QCloseEvent close; QCoreApplication::sendEvent(popupWindow(), &close); QTRY_VERIFY(!popupWindow());
        QCOMPARE(lastClosed.size(), 0);
        // Pending mouse-triggered open is invalidated before its queued callback can see stale objects.
        QPointer<QQuickView> oldBar = bar;
        clickWithoutProcessing(bar, button); QVERIFY(!popupWindow()); host.rebuild();
        QVERIFY(!oldBar); QTest::qWait(20); QVERIFY(!popupWindow());
        bar = barWindow(); QVERIFY(bar); button = itemNamed(bar->rootObject(), "calendar-button");
        clickWithoutProcessing(bar, button); QVERIFY(!popupWindow());
        QVERIFY(config.previewText(text + "[theme]\nname='dawn'\n"));
        QTest::qWait(30); QVERIFY(!popupWindow());
        bar = barWindow(); QVERIFY(bar); button = itemNamed(bar->rootObject(), "calendar-button");
        clickWithoutProcessing(bar, button); QVERIFY(!popupWindow());
        // Offscreen has no real hotplug. Exercise the application's actual screen-removal signal path.
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "screenRemoved", Qt::DirectConnection, Q_ARG(QScreen *, bar->screen())));
        QTest::qWait(30); QVERIFY(!popupWindow());
        bar = barWindow(); QVERIFY(bar); button = itemNamed(bar->rootObject(), "calendar-button");
        // Unload through the owner: deleting Loader.item manually leaves Qt's
        // delegate bookkeeping stale rather than simulating a supported removal.
        auto *loader = button->parentItem()->parentItem(); QVERIFY(loader->property("active").isValid());
        clickWithoutProcessing(bar, button); QVERIFY(!popupWindow()); loader->setProperty("active", false);
        QTest::qWait(20); QVERIFY(!popupWindow());
        // Legacy/mismatched-source requests must not fall back to a wrong panel/output.
        host.openModule("calendar", "main", bar->screen()->name());
        host.openModule("calendar", "wrong-panel", bar->screen()->name(), bar->rootObject());
        QTest::qWait(20); QVERIFY(!popupWindow());
        auto custom = text; custom.replace("show_settings=false", "show_settings=false\nescape_closes=false\npopup_gap=256\npopup_width=1920\npopup_height=2160");
        QVERIFY(config.previewText(custom)); QTest::qWait(30);
        bar = barWindow(); button = itemNamed(bar->rootObject(), "calendar-button");
        clickItem(bar, button); QTRY_VERIFY(popupWindow());
        QTest::keyClick(popupWindow(), Qt::Key_Escape); QTest::qWait(10); QVERIFY(popupWindow());
        QVERIFY(popupWindow()->width() <= bar->screen()->size().width());
        QVERIFY(popupWindow()->height() <= bar->screen()->size().height());
        clickItem(popupWindow(), itemNamed(popupWindow()->rootObject(), "popup-close")); QTRY_VERIFY(!popupWindow());
        QCOMPARE(warnings.size(), 0);
    }
    void scrolledPopupSource_data() {
        QTest::addColumn<bool>("vertical"); QTest::addColumn<QString>("module");
        for (bool vertical : {false, true}) for (const auto &module : {"workspaces", "tray"})
            QTest::newRow(qPrintable(QString("%1-%2").arg(module).arg(vertical))) << vertical << QString(module);
    }
    void scrolledPopupSource() {
        QFETCH(bool, vertical); QFETCH(QString, module);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QVERIFY(config.previewText(QString("[ui]\nshow_settings=false\n[modules.%1.behavior]\nallow_actions=false\n[modules.%1.style]\nmax_width=400\n[[panels]]\nedge='%2'\nlength=220\nmodules=['calendar','%1']").arg(module, vertical ? "left" : "top")));
        FixtureService service;
        for (int i = 0; i < 10; ++i) service.items << QVariantMap{{"id", QString::number(i)}, {"idx", i}, {"name", QString("Space %1").arg(i)}, {"Title", QString("Tray %1").arg(i)}};
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{{module, QVariant::fromValue(&service)}});
        Alure::PanelHost host(config, engine, true);
        QQuickView *bar = nullptr;
        for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure · main") bar = qobject_cast<QQuickView *>(w);
        QVERIFY(bar); QTest::qWait(30);
        auto *outer = itemNamed(bar->rootObject(), "panel-modules");
        auto *inner = itemNamed(bar->rootObject(), module + "-list"); QVERIFY(outer); QVERIFY(inner);
        outer->setProperty(vertical ? "contentY" : "contentX", vertical ? 32 : 120);
        inner->setProperty(vertical ? "contentY" : "contentX", vertical ? 170 : 300);
        QTest::qWait(20);
        QQuickItem *entry = nullptr; QRect clipped;
        for (int i = 0; i < 10; ++i) {
            auto *candidate = itemNamed(bar->rootObject(), module + "-entry-" + QString::number(i));
            if (!candidate) continue;
            const auto rect = Alure::visiblePopupAnchor(candidate);
            if (rect.width() > 10 && rect.height() > 10) { entry = candidate; clipped = rect; break; }
        }
        QVERIFY(entry); QVERIFY(QRect(QPoint(), bar->size()).contains(clipped));
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, clipped.center()); QTest::qWait(30);
        QQuickView *popup = nullptr;
        for (auto *w : QGuiApplication::topLevelWindows()) if (w->title() == "Alure details" && w->isVisible()) popup = qobject_cast<QQuickView *>(w);
        QVERIFY(popup);
        const auto p = Alure::popupPlacement(clipped, bar->size(), bar->screen()->size(), vertical ? "left" : "top", config.model().value("ui").toMap());
        QCOMPARE(popup->property("_q_waylandPopupAnchorRect").toRect(), p.anchorRect);
        QCOMPARE(popup->transientParent(), bar);
        QVERIFY(service.lastAction.isEmpty());
        QTest::keyClick(popup, Qt::Key_Escape); QTRY_VERIFY(!popup->isVisible());
    }
    void settingsClose_data() {
        QTest::addColumn<QString>("draftKind"); QTest::addColumn<QString>("action");
        for (const auto &kind : {"clean", "raw", "pending", "invalid", "conflict"})
            for (const auto &action : {"save", "discard", "cancel"})
                QTest::newRow(qPrintable(QString(kind) + "-" + action)) << QString(kind) << QString(action);
    }
    void settingsClose() {
        QFETCH(QString, draftKind); QFETCH(QString, action);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->requestActivate(); QTest::qWait(30);
        const auto find = [&](const QString &name) { return itemNamed(window->contentItem(), name); };
        const bool dirty = draftKind != "clean";
        if (dirty && draftKind != "pending") {
            clickItem(window, find("settings-section-4")); QTest::qWait(20);
            replaceText(window, find("settings-raw-editor"), draftKind == "invalid" ? "[broken" : "# retained\n[theme]\nname = 'dawn'\n");
            QTRY_VERIFY(window->property("dirty").toBool());
            if (draftKind == "conflict") {
                QFile file(config.path()); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("# external\nversion=1\n"); file.close();
            }
        }
        if (draftKind == "pending") {
            auto *number = find("field-theme.font_size-number"); QVERIFY(number); revealItem(number);
            const auto before = find("settings-raw-editor")->property("text").toString();
            replaceText(window, number, "19");
            QTRY_VERIFY(window->property("dirty").toBool());
            QCOMPARE(find("settings-raw-editor")->property("text").toString(), before);
        }
        QSignalSpy lastClosed(qGuiApp, &QGuiApplication::lastWindowClosed);
        if (action == "cancel") QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);
        else if (action == "discard") window->close(); // Native close event, as used by the compositor.
        else window->close();
        if (!dirty) { QTRY_VERIFY(!window->isVisible()); return; }
        auto *dialog = window->findChild<QObject *>("settings-unsaved"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        clickItem(window, find("unsaved-" + action));
        const bool refused = action == "save" && (draftKind == "invalid" || draftKind == "conflict");
        if (action == "cancel" || refused) {
            QTest::qWait(50); QVERIFY(window->isVisible()); QCOMPARE(lastClosed.size(), 0);
            QCOMPARE(dialog->property("visible").toBool(), refused);
            QVERIFY(window->property("dirty").toBool());
            if (refused) clickItem(window, find("unsaved-cancel"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
            clickItem(window, find("settings-section-4")); QTest::qWait(10);
            replaceText(window, find("settings-raw-editor"), "# continued editing\nversion = 1\n");
            QVERIFY(find("settings-raw-editor")->property("text").toString().contains("continued editing"));
        } else {
            QTRY_VERIFY(!window->isVisible());
            if (action == "save") { QVERIFY(QFile::exists(config.path())); QCOMPARE(config.model().value("theme").toMap().value("name").toString(), draftKind == "pending" ? "midnight" : "dawn");
                if (draftKind == "pending") QCOMPARE(config.model().value("theme").toMap().value("font_size").toInt(), 19); }
        }
        if (action != "save" && draftKind != "conflict") QVERIFY(!QFile::exists(config.path()));
        QCOMPARE(warnings.size(), 0);
    }
    void settingsCloseConfiguration() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml"));
        QVERIFY(config.reload());
        QVERIFY(config.saveText("[settings]\nclose_shortcut='Alt+X'\n"));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTest::qWait(30);
        QVERIFY(!itemNamed(window->contentItem(), "settings-close"));
        QVERIFY(itemNamed(window->contentItem(), "settings-save")->isVisible());
        QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier); QTest::qWait(20); QVERIFY(window->isVisible());
        QTest::keyClick(window, Qt::Key_X, Qt::AltModifier); QTRY_VERIFY(!window->isVisible());
    }
    void settingsWidgets() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings); engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTest::qWait(30);
        const auto find = [&](const QString &name) { return itemNamed(window->contentItem(), name); };
        window->resize(QSize(1024, 740)); QTest::qWait(20);
        auto *fontChoice = find("field-theme.font-choice"); QVERIFY(fontChoice); revealItem(fontChoice);
        replaceText(window, fontChoice, "Serif");
        auto *group = find("appearance-group"); QVERIFY(group); group->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("appearanceGroup").toInt(), 1);
        QCOMPARE(config.inspectText(find("settings-raw-editor")->property("text").toString()).value("model").toMap().value("theme").toMap().value("font").toString(), "Serif");
        group->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Up); QTest::qWait(10);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("appearanceGroup").toInt(), 0);
        for (const auto &name : config.themeNames()) QVERIFY(find("theme-choice-" + name));
        const auto beforeTheme = find("settings-raw-editor")->property("text").toString();
        revealItem(find("theme-choice-forest")); clickItem(window, find("theme-choice-forest"));
        QCOMPARE(find("settings-raw-editor")->property("text").toString(), beforeTheme);
        QVERIFY(!QFile::exists(config.path()));
        QPointer<QQuickItem> slider = find("field-theme.opacity-slider"); QVERIFY(slider); revealItem(slider);
        QCOMPARE(QQmlProperty::read(slider, "palette.dark").value<QColor>(), QColor(config.model().value("theme").toMap().value("palette").toMap().value("accent").toString()));
        QSignalSpy moved(slider, SIGNAL(moved()));
        auto point = slider->mapToScene(QPointF(slider->width() * .3, slider->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(window, point + QPoint(40,0)); QTest::qWait(350);
        QVERIFY(slider); QVERIFY(slider->property("pressed").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point + QPoint(40,0));
        QVERIFY(moved.size() > 0);
        const auto mouseValue = slider->property("value").toDouble();
        slider->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Right);
        QVERIFY(slider->property("value").toDouble() > mouseValue);
        auto *number = find("field-theme.spacing-number"); revealItem(number); replaceText(window, number, "27");
        // Preview must flush a still-focused numeric editor and keep the exact dragged opacity.
        clickItem(window, find("settings-preview")); QTest::qWait(30);
        QCOMPARE(window->size(), QSize(1024, 740));
        QCOMPARE(config.model().value("theme").toMap().value("name").toString(), "forest");
        QCOMPARE(config.model().value("theme").toMap().value("spacing").toInt(), 27);
        QVERIFY(config.model().value("theme").toMap().value("opacity").toDouble() < .88);
        QVERIFY(!QFile::exists(config.path()));
        auto *choice = find("field-theme.icon_mode-choice"); QVERIFY(choice); revealItem(choice);
        choice->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Down);
        auto *text = find("field-theme.icon_theme-text"); revealItem(text); const QString escaped = "A \"quoted\" \\ icon"; replaceText(window, text, escaped);
        clickItem(window, find("settings-section-4")); QTest::qWait(20);
        const auto inspection = config.inspectText(find("settings-raw-editor")->property("text").toString());
        QVERIFY2(inspection.value("error").toString().isEmpty(), qPrintable(inspection.value("error").toString()));
        const auto theme = inspection.value("model").toMap().value("theme").toMap();
        QCOMPARE(theme.value("icon_mode").toString(), "theme"); QCOMPARE(theme.value("icon_theme").toString(), escaped);
        clickItem(window, find("settings-save")); QVERIFY(!window->property("dirty").toBool());
        QVERIFY(QFile::exists(config.path())); QVERIFY(config.reload()); QCOMPARE(config.model().value("theme").toMap().value("icon_theme").toString(), escaped);
        QCOMPARE(warnings.size(), 0);
    }
    void settingsFieldOrdering_data() {
        QTest::addColumn<QString>("source");
        QTest::newRow("inherited-panel") << QString("[theme]\nname='forest'\n");
        QTest::newRow("explicit-panel") << QString("[[panels]]\nthickness=48\nmodules=['calendar','volume']\nmargins={top=21,right=22,bottom=23,left=24}\nid='ordered'\nedge='left'\n");
    }
    void settingsFieldOrdering() {
        QFETCH(QString, source);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload()); QVERIFY(config.saveText(source));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings); engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTest::qWait(30);
        const auto find = [&](const QString &name) { return itemNamed(window->contentItem(), name); };
        // Inspect real delegates, not just the helper's return value. Navigation clicks
        // recreate them and must retain both theme priority and contiguous panel groups.
        const auto fieldPaths = [&] {
            QStringList paths;
            std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
                const auto spec = item->property("spec").toMap();
                if (spec.contains("path")) paths << spec.value("path").toString();
                for (auto *child : item->childItems()) visit(child);
            };
            visit(find("settings-form-scroll"));
            return paths;
        };
        const QStringList themePriority{"theme.name", "theme.font", "theme.font_size", "theme.opacity", "theme.spacing", "theme.padding", "theme.radius", "theme.icon_size", "theme.icon_mode", "theme.icon_theme"};
        const QStringList expectedPanel{"panels.0.edge", "panels.0.enabled", "panels.0.exclusive_zone", "panels.0.id", "panels.0.layer", "panels.0.layout", "panels.0.length", "panels.0.margins.bottom", "panels.0.margins.left", "panels.0.margins.right", "panels.0.margins.top", "panels.0.output", "panels.0.spacer_size", "panels.0.thickness", "panels.0.visibility.edge_trigger_px", "panels.0.visibility.hide_delay_ms", "panels.0.visibility.mode", "panels.0.visibility.show_delay_ms", "panels.0.visibility.unknown_geometry", "panels.0.window_gap"};
        for (int pass = 0; pass < 2; ++pass) {
            QCOMPARE(fieldPaths().mid(0, themePriority.size()), themePriority);
            const auto remaining = fieldPaths().mid(themePriority.size()); auto sorted = remaining; sorted.sort();
            clickItem(window, find("settings-section-1")); QTest::qWait(20);
            QCOMPARE(window->property("section").toInt(), 1);
            QCOMPARE(fieldPaths(), expectedPanel);
            int headings = 0;
            std::function<void(QQuickItem *)> countHeadings = [&](QQuickItem *item) {
                if (item->isVisible() && item->property("text").toString() == "Panel margins") ++headings;
                for (auto *child : item->childItems()) countHeadings(child);
            };
            countHeadings(find("settings-form-scroll")); QCOMPARE(headings, 1);
            QCOMPARE(remaining, sorted);
            clickItem(window, find("settings-section-0")); QTest::qWait(20);
        }
        QVERIFY(!window->property("dirty").toBool()); QCOMPARE(config.source(), source);
        QCOMPARE(warnings.size(), 0);
    }
    void settingsInheritedPanel() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        const QString source = "# minimal config\n[theme]\nname='forest' # retained\n"; QVERIFY(config.saveText(source));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings); engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTest::qWait(30);
        const auto find = [&](const QString &name) { return itemNamed(window->contentItem(), name); };
        clickItem(window, find("settings-section-1")); QTest::qWait(20);
        auto *up = find("panel-module-media-up"); QVERIFY(up); revealItem(up); clickItem(window, up);
        auto *margin = find("field-panels.0.margins.top-number"); QVERIFY(margin); revealItem(margin); replaceText(window, margin, "20");
        clickItem(window, find("settings-save")); QTRY_VERIFY(!window->property("dirty").toBool());
        QVERIFY(config.source().startsWith(source)); QVERIFY(config.source().contains("[[panels]]"));
        const auto panels = config.model().value("panels").toList(); QCOMPARE(panels.size(), 1);
        QCOMPARE(panels.first().toMap().value("modules").toList().first().toString(), "media");
        QCOMPARE(panels.first().toMap().value("margins").toMap().value("top").toInt(), 20);
        QVERIFY(config.reload()); QCOMPARE(warnings.size(), 0);
    }
    void settingsColorsAndPanelModules() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml"));
        QFile file(config.path()); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray source = "# retained\n[theme]\nname='midnight'\n[[panels]]\nid='main'\nmodules=['calendar', 'custom', 'volume'] # ordered\nextra={nested=[1,2]}\n[modules.custom]\nenabled=true\n";
        file.write(source); file.close(); QVERIFY(config.reload());
        QVERIFY(config.previewText(QString::fromUtf8(source).replace("midnight", "forest")));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings); engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTest::qWait(30);
        const auto find = [&](const QString &name) { return itemNamed(window->contentItem(), name); };
        auto *color = find("field-theme.palette.accent-color"); QVERIFY(color); revealItem(color); clickItem(window, color); QTest::qWait(50);
        auto *picker = window->findChild<QObject *>("settings-color-picker"); QVERIFY(picker); QTRY_VERIFY(picker->property("visible").toBool());
        auto *focusWindow = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow()); QVERIFY(focusWindow);
        QTest::keyClick(focusWindow, Qt::Key_Escape); QTRY_VERIFY(!picker->property("visible").toBool());
        QVERIFY(!window->property("dirty").toBool()); QCOMPARE(config.source().toUtf8(), source);
        window->requestActivate(); QTest::qWait(20);
        clickItem(window, color); QTRY_VERIFY(picker->property("visible").toBool()); QTest::qWait(30);
        focusWindow = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow()); QVERIFY(focusWindow);
        auto *hue = itemNamed(focusWindow->contentItem(), "hueSlider"); QVERIFY(hue); clickItem(focusWindow, hue);
        std::function<QQuickItem *(QQuickItem *)> acceptButton = [&](QQuickItem *item) -> QQuickItem * {
            // Qt's nonnative picker highlights its default OK button, independent of locale.
            if (item->metaObject()->indexOfSignal("clicked()") >= 0 && item->property("highlighted").toBool()) return item;
            for (auto *child : item->childItems()) if (auto *found = acceptButton(child)) return found;
            return nullptr;
        };
        auto *ok = acceptButton(focusWindow->contentItem()); QVERIFY(ok);
        auto *label = ok->property("contentItem").value<QQuickItem *>(); QVERIFY(label);
        QCOMPARE(label->property("color").value<QColor>(), QColor(config.model().value("theme").toMap().value("palette").toMap().value("foreground").toString()));
        clickItem(focusWindow, ok);
        QTRY_VERIFY(!picker->property("visible").toBool());
        QVERIFY(window->property("dirty").toBool()); QCOMPARE(config.source().toUtf8(), source);
        window->requestActivate(); QTest::qWait(20);
        auto *text = find("field-theme.palette.accent-text"); revealItem(text); replaceText(window, text, "#12zz00"); clickItem(window, find("settings-save"));
        QVERIFY(window->property("dirty").toBool()); QCOMPARE(config.source().toUtf8(), source);
        replaceText(window, text, "#12aa00"); clickItem(window, find("settings-apply-fields"));
        clickItem(window, find("settings-section-1")); QTest::qWait(20);
        auto *scrollContent = find("settings-form-scroll")->property("contentItem").value<QQuickItem *>(); QVERIFY(scrollContent);
        QCOMPARE(scrollContent->property("contentY").toDouble(), 0.);
        auto *up = find("panel-module-custom-up"); QVERIFY2(up, qPrintable(window->property("statusText").toString() + " section=" + window->property("section").toString() + " raw=" + find("settings-raw-editor")->property("text").toString())); revealItem(up); clickItem(window, up);
        auto *toggle = find("panel-module-volume"); QVERIFY(toggle);
        QCOMPARE(QQmlProperty::read(toggle, "palette.dark").value<QColor>(), QColor(config.model().value("theme").toMap().value("palette").toMap().value("accent").toString()));
        revealItem(toggle); clickItem(window, toggle);
        clickItem(window, find("settings-save")); QTest::qWait(20); QVERIFY(!window->property("dirty").toBool());
        const auto panel = config.model().value("panels").toList().first().toMap();
        QCOMPARE(panel.value("modules").toList(), (QVariantList{"custom", "calendar"}));
        QCOMPARE(panel.value("extra").toMap().value("nested").toList(), (QVariantList{1LL,2LL}));
        QVERIFY(config.source().contains("# ordered")); QVERIFY(config.source().contains("extra={nested=[1,2]}"));
        QCOMPARE(config.model().value("theme").toMap().value("palette").toMap().value("accent").toString(), "#12aa00");
        clickItem(window, find("settings-section-4")); QTest::qWait(10);
        const auto saved = config.source(); replaceText(window, find("settings-raw-editor"), "# unsaved\nversion=1\n");
        clickItem(window, find("settings-reload")); QTest::qWait(30); clickItem(window, find("unsaved-cancel"));
        QTest::qWait(30); QVERIFY(window->property("dirty").toBool());
        clickItem(window, find("settings-reload")); QTest::qWait(30); clickItem(window, find("unsaved-discard"));
        QTRY_VERIFY(!window->property("dirty").toBool()); QCOMPARE(find("settings-raw-editor")->property("text").toString(), saved);
        QCOMPARE(warnings.size(), 0);
    }
    void settingsDraft() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("config.toml")); QVERIFY(config.reload());
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root); QVERIFY(window); QTest::qWait(20);
        clickItem(window, itemNamed(window->contentItem(), "theme-choice-dawn"));
        QCOMPARE(root->property("dirty").toBool(), true);
        for (int section = 0; section < 5; ++section) { root->setProperty("section", section); QTest::qWait(10); }
        QVERIFY(!QFile::exists(config.path()));
        clickItem(window, itemNamed(window->contentItem(), "settings-save"));
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
    // Native lifecycle probes must be explicitly launched on an isolated
    // Wayland display; default CTest never uses the user's desktop.
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIntValue("ALURE_TEST_NATIVE_WAYLAND") == 1 ? "wayland" : "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv); QQuickStyle::setStyle("Basic"); QQuickWindow::setDefaultAlphaBuffer(true);
    Alure::BackgroundBlur::registerQmlType();
    UiTest test; return QTest::qExec(&test, argc, argv);
}
#include "UiTest.moc"

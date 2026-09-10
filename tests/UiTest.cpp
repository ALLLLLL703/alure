#include "ConfigStore.h"
#include "PanelHost.h"
#include "PopupPlacement.h"
#include <QScreen>
#include <QCloseEvent>
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
    Q_INVOKABLE void openModule(const QString &, const QString &, const QString &, QQuickItem *) {}
};
namespace {
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
        clickWithoutProcessing(bar, button); QVERIFY(!popupWindow()); delete button->parentItem();
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
        else clickItem(window, find("settings-close"));
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
        QVERIFY(config.saveText("[settings]\nshow_close_button=false\nclose_shortcut='Alt+X'\n"));
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider); engine.rootContext()->setContextProperty("Config", &config);
        engine.load(QUrl("qrc:/qml/Settings.qml")); QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); window->requestActivate(); QTest::qWait(30);
        auto *button = itemNamed(window->contentItem(), "settings-close"); QVERIFY(button); QVERIFY(!button->isVisible());
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
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv); QQuickStyle::setStyle("Basic"); QQuickWindow::setDefaultAlphaBuffer(true);
    UiTest test; return QTest::qExec(&test, argc, argv);
}
#include "UiTest.moc"

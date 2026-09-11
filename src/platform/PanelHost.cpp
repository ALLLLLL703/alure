#include "PanelHost.h"
#include "PopupPlacement.h"
#include "ConfigStore.h"
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
#include <QQmlContext>
#include <QQuickItem>
#include <QProcess>
#include <QIcon>
#include <QPointer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextStream>
#include <algorithm>
namespace Alure {
PanelPlacement panelPlacement(const QVariantMap &panel, QSize screenSize) {
    using W = LayerShellQt::Window;
    const auto edgeName = panel.value("edge").toString();
    const bool vertical = edgeName == "left" || edgeName == "right";
    const auto margins = panel.value("margins").toMap();
    const QMargins m(margins.value("left").toInt(), margins.value("top").toInt(), margins.value("right").toInt(), margins.value("bottom").toInt());
    const auto edge = edgeName == "top" ? W::AnchorTop : edgeName == "bottom" ? W::AnchorBottom : edgeName == "left" ? W::AnchorLeft : W::AnchorRight;
    W::Anchors anchors(edge);
    const int available = std::max(1, vertical ? screenSize.height() - m.top() - m.bottom() : screenSize.width() - m.left() - m.right());
    const int configuredLength = panel.value("length").toInt();
    const int length = configuredLength == 0 ? available : std::min(configuredLength, available);
    if (configuredLength == 0) anchors |= vertical ? W::Anchors(W::AnchorTop) | W::AnchorBottom : W::Anchors(W::AnchorLeft) | W::AnchorRight;
    const int thickness = std::min(panel.value("thickness").toInt(), std::max(1, vertical ? screenSize.width() - m.left() - m.right() : screenSize.height() - m.top() - m.bottom()));
    const auto layerName = panel.value("layer").toString();
    const auto layer = layerName == "background" ? W::LayerBackground : layerName == "bottom" ? W::LayerBottom : layerName == "overlay" ? W::LayerOverlay : W::LayerTop;
    int zone = panel.value("exclusive_zone").toInt();
    // The compositor adds the anchored margin to a positive protocol zone.
    if (zone == -1) zone = thickness;
    if (zone > 0) zone = std::max(0, zone + panel.value("window_gap").toInt());
    return {vertical ? QSize(thickness, length) : QSize(length, thickness), m, anchors, edge, layer, zone, vertical};
}
OsdPlacement osdPlacement(const QVariantMap &options, QSize screenSize) {
    const int width = std::max(1, screenSize.width()), height = std::max(1, screenSize.height());
    const int horizontal = std::clamp(options.value("margin_horizontal").toInt(), 0, (width - 1) / 2);
    const int bottom = std::clamp(options.value("margin_bottom").toInt(), 0, height - 1);
    const QSize size(std::clamp(options.value("width").toInt(), 1, width - 2 * horizontal),
                     std::clamp(options.value("height").toInt(), 1, height - bottom));
    return {size, QMargins(horizontal, 0, horizontal, bottom), QPoint((width - size.width()) / 2, height - bottom - size.height())};
}
PanelHost::PanelHost(ConfigStore &config, QQmlEngine &engine, bool preview, QObject *parent)
    : QObject(parent), m_config(config), m_engine(engine), m_preview(preview) {
    m_osdTimer.setSingleShot(true);
    connect(&m_osdTimer, &QTimer::timeout, this, &PanelHost::closeOsd);
    connect(&config, &ConfigStore::modelChanged, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &PanelHost::scheduleRebuild);
    m_engine.rootContext()->setContextProperty("Shell", this);
    rebuild();
}
PanelHost::~PanelHost() {
    // Window destruction emits focus/visibility signals. Tear down windows while
    // the popup's borrowed parent/anchor and keyboard-policy state are still alive.
    closePopup();
    closeOsd();
    m_toast.reset(); m_popup.reset(); m_windows.clear();
}
void PanelHost::scheduleRebuild() {
    if (m_rebuildPending) return;
    closePopup();
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] { m_rebuildPending = false; rebuild(); });
}
void PanelHost::trace(const QString &message) const {
    if (m_config.model().value("runtime").toMap().value("trace_windows").toBool()) QTextStream(stderr) << "Alure windows: " << message << '\n';
}
void PanelHost::rebuild() {
    trace("rebuilding panels after config/output change");
    closePopup(); closeToast(); closeOsd();
    const auto iconTheme = m_config.model().value("theme").toMap().value("icon_theme").toString();
    static const QString desktopIconTheme = QIcon::themeName();
    QIcon::setThemeName(iconTheme.isEmpty() ? desktopIconTheme : iconTheme);
    m_popup.reset(); // Destroy transient children before their layer parents.
    m_windows.clear();
    const auto screens = QGuiApplication::screens();
    for (auto *screen : screens) {
        connect(screen, &QScreen::geometryChanged, this, &PanelHost::scheduleRebuild, Qt::UniqueConnection);
    }
    for (const auto &entry : m_config.model().value("panels").toList()) {
        const auto panel = entry.toMap();
        if (!panel.value("enabled").toBool()) continue;
        const auto output = panel.value("output").toString();
        bool matched = false;
        for (auto *screen : screens) {
            if (output != "*" && !(output == "primary" && screen == QGuiApplication::primaryScreen()) && output != screen->name()) continue;
            matched = true;
            const auto placement = panelPlacement(panel, screen->size());
            auto view = std::make_unique<QQuickView>(&m_engine, nullptr);
            view->setScreen(screen);
            view->setProperty("panelId", panel.value("id"));
            view->setTitle("Alure · " + panel.value("id").toString());
            view->installEventFilter(this);
            view->setColor(Qt::transparent);
            view->setResizeMode(QQuickView::SizeRootObjectToView);
            view->resize(placement.size);
            if (!m_preview) {
                view->setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
                auto *layer = LayerShellQt::Window::get(view.get()); // QObject owned by QWindow
                layer->setScope("alure-" + panel.value("id").toString());
                layer->setScreen(screen);
                layer->setAnchors(placement.anchors);
                layer->setMargins(placement.margins);
                layer->setDesiredSize(placement.size);
                layer->setLayer(placement.layer);
                layer->setExclusiveEdge(placement.edge);
                layer->setExclusiveZone(placement.exclusiveZone);
                layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
                layer->setActivateOnShow(false);
            }
            view->setInitialProperties({{"panel", panel}, {"vertical", placement.vertical}, {"outputName", screen->name()}});
            view->setSource(QUrl("qrc:/qml/Panel.qml"));
            if (view->status() == QQuickView::Error) {
                qCritical() << "Panel QML failed:" << view->errors();
                QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(1); });
                continue;
            }
            view->show();
            m_windows.push_back(std::move(view));
        }
        if (!matched) qWarning().noquote() << "Panel" << panel.value("id").toString() << "waiting for output" << output;
    }
}
void PanelHost::releasePopupKeyboard() {
    if (!m_preview && m_popupParent) {
        LayerShellQt::Window::get(m_popupParent)->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        m_popupParent->update();
    }
    m_popupParent.clear();
    m_popupHadFocus = false;
}
void PanelHost::closePopup() {
    ++m_popupRequest; trace("hide details");
    if (m_popup) m_popup->hide();
    releasePopupKeyboard();
}
bool PanelHost::eventFilter(QObject *watched, QEvent *event) {
    if (!m_popup || !m_popup->isVisible()) return QObject::eventFilter(watched, event);
    if (watched != m_popup.get() && watched != m_popupParent) return QObject::eventFilter(watched, event);
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && m_config.model().value("ui").toMap().value("escape_closes").toBool()) {
        event->accept();
        if (event->type() == QEvent::KeyPress) closePopup();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress && watched == m_popupParent && m_popupAnchor &&
        m_config.model().value("ui").toMap().value("toggle_on_click").toBool()) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (visiblePopupAnchor(m_popupAnchor).contains(mouse->position().toPoint())) {
            closePopup(); event->accept(); return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
void PanelHost::closeToast() { if (m_toast) m_toast->hide(); }
bool PanelHost::openSettings() {
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--settings", "--config", m_config.path()});
}
void PanelHost::openModule(const QString &name, const QString &panelId, const QString &output, QQuickItem *anchor, const QString &trayItem) {
    trace(QString("request %1 visible=%2").arg(name).arg(m_popup && m_popup->isVisible()));
    if (!anchor || m_rebuildPending) return; // Legacy callers without an actual source cannot be placed safely.
    const auto module = m_config.model().value("modules").toMap().value(name).toMap();
    if (!module.value("enabled").toBool() || !module.value("behavior").toMap().value("popup_enabled").toBool()) return;
    for (const auto &window : m_windows) {
        if (window.get() != anchor->window() || window->property("panelId").toString() != panelId ||
            !window->screen() || window->screen()->name() != output || visiblePopupAnchor(anchor).isEmpty()) continue;
        for (const auto &entry : m_config.model().value("panels").toList()) {
            const auto panel = entry.toMap();
            if (panel.value("id").toString() != panelId) continue;
            if (m_popup && m_popup->isVisible() && m_popupAnchor == anchor && m_popupModule == name && m_popupTrayItem == trayItem &&
                m_config.model().value("ui").toMap().value("toggle_on_click").toBool()) { closePopup(); return; }
            closePopup();
            const auto request = ++m_popupRequest;
            m_popupParent = window.get();
            if (!m_preview) {
                // xdg_popup children inherit their layer parent's keyboard policy.
                // Grant keyboard focus only while details are open, not to idle bars.
                LayerShellQt::Window::get(window.get())->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
                window->update();
            }
            // A config reload, output removal, source destruction or later click cancels this request.
            QTimer::singleShot(0, this, [this, name, panel, request, trayItem, parent = QPointer<QQuickView>(window.get()),
                                       item = QPointer<QQuickItem>(anchor), screen = QPointer<QScreen>(window->screen())] {
                trace(QString("queued request=%1 current=%2 rebuilding=%3").arg(request).arg(m_popupRequest).arg(m_rebuildPending));
                if (request != m_popupRequest || m_rebuildPending) return;
                if (parent && item && screen && parent->isVisible() && parent->screen() == screen && item->window() == parent &&
                    QGuiApplication::screens().contains(screen)) createPopup(name, panel, parent, item, trayItem);
                else releasePopupKeyboard();
            });
            return;
        }
    }
}
void PanelHost::createPopup(const QString &name, const QVariantMap &panel, QQuickView *parent, QQuickItem *anchor, const QString &trayItem) {
    const bool trayMenu = name == "tray" && !trayItem.isEmpty();
    auto options = m_config.model().value("ui").toMap();
    if (trayMenu) {
        const auto behavior = m_config.model().value("modules").toMap().value("tray").toMap().value("behavior").toMap();
        options["popup_width"] = behavior.value("menu_width");
        options["popup_height"] = behavior.value("menu_height");
    }
    if (name == "media" || name == "clipboard") {
        const auto behavior = m_config.model().value("modules").toMap().value(name).toMap().value("behavior").toMap();
        options["popup_width"] = behavior.value("popup_width");
        options["popup_height"] = behavior.value("popup_height");
    }
    const auto p = popupPlacement(visiblePopupAnchor(anchor), parent->size(), parent->screen()->size(),
                                  panel.value("edge").toString(), options);
    if (p.anchorRect.isEmpty()) { releasePopupKeyboard(); return; }
    m_popup.reset();
    m_popupHadFocus = false;
    m_popupParent = parent; m_popupAnchor = anchor; m_popupModule = name; m_popupTrayItem = trayItem;
    auto view = std::make_unique<QQuickView>(&m_engine, nullptr);
    view->installEventFilter(this);
    view->setFlags(Qt::Popup | Qt::FramelessWindowHint);
    view->setTransientParent(parent);
    view->setScreen(parent->screen());
    view->setTitle(trayMenu ? "Alure tray menu" : "Alure details");
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->resize(p.size);
    configurePopupPositioner(*view, p);
    // Never mapToGlobal on Wayland. Qt's explicit positioner uses only parent-surface coordinates.
    if (!QGuiApplication::platformName().startsWith("wayland")) {
        const auto available = parent->screen()->availableGeometry();
        auto pos = parent->mapToGlobal(p.position);
        pos.setX(std::clamp(pos.x(), available.left(), std::max(available.left(), available.right() + 1 - p.size.width())));
        pos.setY(std::clamp(pos.y(), available.top(), std::max(available.top(), available.bottom() + 1 - p.size.height())));
        view->setPosition(pos);
    }
    view->setInitialProperties(trayMenu ? QVariantMap{{"trayItem", trayItem}, {"popupPadding", p.padding}, {"bottomAligned", p.gravity.testFlag(Qt::TopEdge)}}
                                       : QVariantMap{{"moduleName", name}, {"outputName", parent->screen()->name()}, {"popupPadding", p.padding}});
    view->setSource(QUrl(trayMenu ? "qrc:/qml/TrayMenu.qml" : "qrc:/qml/ModulePopup.qml"));
    if (view->status() == QQuickView::Error) { qWarning() << view->errors(); releasePopupKeyboard(); return; }
    connect(view.get(), &QWindow::visibleChanged, this, [this, popup = view.get()](bool visible) {
        if (m_popup.get() == popup && !visible) releasePopupKeyboard();
    });
    connect(view.get(), &QWindow::activeChanged, this, [this, popup = view.get()] {
        if (m_popup.get() != popup) return;
        if (m_popup->isActive()) m_popupHadFocus = true;
        else if (m_popupHadFocus && m_config.model().value("ui").toMap().value("close_on_focus_loss").toBool()) closePopup();
    });
    trace(QString("show details %1 parent=%2 anchor=%3,%4 %5x%6").arg(name, panel.value("id").toString())
          .arg(p.anchorRect.x()).arg(p.anchorRect.y()).arg(p.anchorRect.width()).arg(p.anchorRect.height()));
    m_popup = std::move(view);
    m_popup->show();
    m_popup->requestActivate();
    m_popup->rootObject()->forceActiveFocus();
}
void PanelHost::createToast(const QVariantMap &properties, QScreen *screen) {
    m_toast.reset();
    const auto ui = m_config.model().value("ui").toMap();
    const auto toastConfig = ui.value("toast").toMap();
    const int inset = toastConfig.value("margin").toInt();
    const auto edge = toastConfig.value("edge").toString();
    const int gap = ui.value("popup_gap").toInt();
    const QSize extent = screen->size();
    const int xInset = std::clamp(inset, 0, std::max(0, extent.width() - 1));
    const int yInset = std::clamp(inset, 0, std::max(0, extent.height() - 1));
    QSize size(std::max(1, std::min(toastConfig.value("width").toInt(), extent.width() - xInset - gap)),
               std::max(1, std::min(toastConfig.value("height").toInt(), extent.height() - yInset - gap)));
    auto view = std::make_unique<QQuickView>(&m_engine, nullptr);
    view->setScreen(screen); view->setColor(Qt::transparent);
    view->setTitle("Alure notification");
    view->setResizeMode(QQuickView::SizeRootObjectToView); view->resize(size);
    if (!m_preview) {
        using W = LayerShellQt::Window;
        view->setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        auto *layer = W::get(view.get());
        layer->setScope("alure-toast"); layer->setScreen(screen);
        layer->setLayer(W::LayerOverlay); layer->setExclusiveZone(-1); layer->setDesiredSize(size);
        layer->setAnchors(W::Anchors(W::AnchorRight) | (edge == "bottom" ? W::AnchorBottom : W::AnchorTop));
        layer->setMargins(QMargins(xInset, yInset, xInset, yInset));
        layer->setKeyboardInteractivity(W::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
    }
    view->setInitialProperties(properties); view->setSource(QUrl("qrc:/qml/Toast.qml"));
    if (view->status() == QQuickView::Error) { for (const auto &error : view->errors()) QTextStream(stderr) << error.toString() << '\n'; return; }
    trace("show toast");
    m_toast = std::move(view); m_toast->show();
}
void PanelHost::syncNotifications(const QVariantList &items) {
    const auto options = m_config.model().value("ui").toMap().value("toast").toMap();
    const auto module = m_config.model().value("modules").toMap().value("notifications").toMap();
    const bool enabled = options.value("enabled").toBool() && module.value("enabled").toBool() && module.value("behavior").toMap().value("toast_enabled").toBool();
    QSet<QString> current;
    QVariantMap latest;
    bool toastActive = false;
    for (const auto &entry : items) {
        const auto row = entry.toMap();
        const auto key = row.value("id").toString() + ":" + row.value("createdAt").toString();
        current.insert(key);
        const bool show = row.value("active").toBool() && !row.value("suppressed").toBool();
        if (!m_seenNotifications.contains(key))
            trace(QString("notification %1: %2").arg(row.value("id").toString(), !enabled ? "banners disabled" : row.value("suppressed").toBool() ? "suppressed by DND" : show ? "banner eligible" : "already closed"));
        if (show && key == m_toastId) toastActive = true;
        if (enabled && show && !m_seenNotifications.contains(key)) { latest = row; m_toastId = key; toastActive = true; }
    }
    m_seenNotifications = current;
    if (!enabled || !toastActive) closeToast();
    if (latest.isEmpty()) return;
    QScreen *screen = nullptr;
    for (auto *candidate : QGuiApplication::screens())
        if ((options.value("output").toString() == "primary" && candidate == QGuiApplication::primaryScreen()) || options.value("output").toString() == candidate->name()) { screen = candidate; break; }
    if (screen) createToast({{"notification", latest}}, screen);
    else qWarning().noquote() << "Alure: notification banner output unavailable:" << options.value("output").toString();
}

void PanelHost::closeOsd() {
    m_osdTimer.stop(); m_osdWindows.clear();
}
void PanelHost::showOsd(const QVariantMap &snapshot) {
    const auto options = m_config.model().value("ui").toMap().value("osd").toMap();
    const auto kind = snapshot.value("kind").toString();
    closeOsd();
    if (m_rebuildPending || !options.value("enabled").toBool() || !options.value(kind + "_enabled").toBool()) return;
    const auto output = options.value("output").toString();
    for (auto *screen : QGuiApplication::screens()) {
        if (output != "*" && !(output == "primary" && screen == QGuiApplication::primaryScreen()) && output != screen->name()) continue;
        const auto placement = osdPlacement(options, screen->size());
        auto view = std::make_unique<QQuickView>(&m_engine, nullptr);
        view->setScreen(screen); view->setTitle("Alure OSD"); view->setColor(Qt::transparent);
        view->setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        view->setResizeMode(QQuickView::SizeRootObjectToView); view->resize(placement.size);
        if (!m_preview) {
            using W = LayerShellQt::Window;
            auto *layer = W::get(view.get());
            layer->setScope("alure-osd"); layer->setScreen(screen);
            layer->setLayer(W::LayerOverlay);
            // -1 ignores other surfaces' zones and reserves no space itself.
            layer->setExclusiveZone(-1); layer->setDesiredSize(placement.size);
            layer->setAnchors(W::AnchorBottom); layer->setMargins(placement.margins);
            layer->setKeyboardInteractivity(W::KeyboardInteractivityNone); layer->setActivateOnShow(false);
        }
        if (!QGuiApplication::platformName().startsWith("wayland")) view->setPosition(screen->geometry().topLeft() + placement.position);
        view->setInitialProperties({{"snapshot", snapshot}}); view->setSource(QUrl("qrc:/qml/Osd.qml"));
        if (view->status() == QQuickView::Error) { qWarning() << "OSD QML failed:" << view->errors(); continue; }
        view->show(); m_osdWindows.push_back(std::move(view));
    }
    if (!m_osdWindows.empty()) {
        trace("show OSD " + kind);
        m_osdTimer.start(options.value("duration_ms").toInt());
    } else qWarning().noquote() << "Alure: OSD output unavailable:" << output;
}

}

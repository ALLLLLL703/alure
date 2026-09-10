#include "PanelHost.h"
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
    return {vertical ? QSize(thickness, length) : QSize(length, thickness), m, anchors, edge, layer, zone, vertical};
}
PanelHost::PanelHost(ConfigStore &config, QQmlEngine &engine, bool preview, QObject *parent)
    : QObject(parent), m_config(config), m_engine(engine), m_preview(preview) {
    connect(&config, &ConfigStore::modelChanged, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &PanelHost::scheduleRebuild);
    m_engine.rootContext()->setContextProperty("Shell", this);
    rebuild();
}
PanelHost::~PanelHost() = default;
void PanelHost::scheduleRebuild() {
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] { m_rebuildPending = false; rebuild(); });
}
void PanelHost::trace(const QString &message) const {
    if (m_config.model().value("runtime").toMap().value("trace_windows").toBool()) QTextStream(stderr) << "Alure windows: " << message << '\n';
}
void PanelHost::rebuild() {
    trace("rebuilding panels after config/output change");
    closePopup(); closeToast();
    const auto iconTheme = m_config.model().value("theme").toMap().value("icon_theme").toString();
    static const QString desktopIconTheme = QIcon::themeName();
    QIcon::setThemeName(iconTheme.isEmpty() ? desktopIconTheme : iconTheme);
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
void PanelHost::closePopup() { trace("hide details"); if (m_popup) m_popup->hide(); m_popupHadFocus = false; }
void PanelHost::closeToast() { if (m_toast) m_toast->hide(); }
bool PanelHost::openSettings() {
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--settings", "--config", m_config.path()});
}
void PanelHost::openModule(const QString &name, const QString &panelId, const QString &output) {
    const auto module = m_config.model().value("modules").toMap().value(name).toMap();
    if (!module.value("enabled").toBool() || !module.value("behavior").toMap().value("popup_enabled").toBool()) return;
    for (const auto &window : m_windows) {
        if (window->property("panelId").toString() != panelId || window->screen()->name() != output) continue;
        for (const auto &entry : m_config.model().value("panels").toList()) {
            const auto panel = entry.toMap();
            if (panel.value("id").toString() == panelId) {
                // Defer destruction of any previous QML tree beyond the triggering handler.
                QTimer::singleShot(0, this, [this, name, panel, screen = QPointer<QScreen>(window->screen())] {
                    if (screen) createAuxiliary("Popup.qml", {{"moduleName", name}}, panel, screen, false);
                });
                return;
            }
        }
    }
}
void PanelHost::createAuxiliary(const QString &source, const QVariantMap &properties, const QVariantMap &panel, QScreen *screen, bool toast) {
    auto &owned = toast ? m_toast : m_popup;
    owned.reset();
    if (!toast) m_popupHadFocus = false;
    const auto ui = m_config.model().value("ui").toMap();
    const auto toastConfig = ui.value("toast").toMap();
    const int inset = toast ? toastConfig.value("margin").toInt() :
        panel.value("thickness").toInt() + panel.value("margins").toMap().value(panel.value("edge").toString()).toInt() + ui.value("popup_gap").toInt();
    const auto edge = toast ? toastConfig.value("edge").toString() : panel.value("edge").toString();
    const int gap = ui.value("popup_gap").toInt();
    const QSize extent = screen->size();
    const int xInset = std::clamp(edge == "left" || edge == "right" ? inset : (toast ? inset : gap), 0, std::max(0, extent.width() - 1));
    const int yInset = std::clamp(edge == "top" || edge == "bottom" ? inset : gap, 0, std::max(0, extent.height() - 1));
    QSize size(std::max(1, std::min(toast ? toastConfig.value("width").toInt() : ui.value("popup_width").toInt(), extent.width() - xInset - gap)),
               std::max(1, std::min(toast ? toastConfig.value("height").toInt() : ui.value("popup_height").toInt(), extent.height() - yInset - gap)));
    auto view = std::make_unique<QQuickView>(&m_engine, nullptr);
    view->setScreen(screen); view->setColor(Qt::transparent);
    view->setTitle(toast ? "Alure notification" : "Alure details");
    view->setResizeMode(QQuickView::SizeRootObjectToView); view->resize(size);
    if (!m_preview) {
        using W = LayerShellQt::Window;
        view->setFlags(Qt::FramelessWindowHint | (toast ? Qt::WindowDoesNotAcceptFocus : Qt::WindowFlags()));
        auto *layer = W::get(view.get());
        layer->setScope(toast ? "alure-toast" : "alure-popup"); layer->setScreen(screen);
        layer->setLayer(W::LayerOverlay); layer->setExclusiveZone(-1); layer->setDesiredSize(size);
        layer->setAnchors(W::Anchors(edge == "left" ? W::AnchorLeft : W::AnchorRight) | (edge == "bottom" ? W::AnchorBottom : W::AnchorTop));
        layer->setMargins(QMargins(xInset, yInset, xInset, yInset));
        layer->setKeyboardInteractivity(toast ? W::KeyboardInteractivityNone : W::KeyboardInteractivityOnDemand);
        layer->setActivateOnShow(!toast);
    }
    view->setInitialProperties(properties); view->setSource(QUrl("qrc:/qml/" + source));
    if (view->status() == QQuickView::Error) { for (const auto &error : view->errors()) QTextStream(stderr) << error.toString() << '\n'; return; }
    if (!toast) connect(view.get(), &QWindow::activeChanged, this, [this] {
        if (!m_popup) return;
        trace(QString("details active=%1 close_on_focus_loss=%2").arg(m_popup->isActive()).arg(m_config.model().value("ui").toMap().value("close_on_focus_loss").toBool()));
        if (m_popup->isActive()) m_popupHadFocus = true;
        else if (m_popupHadFocus && m_config.model().value("ui").toMap().value("close_on_focus_loss").toBool()) closePopup();
    });
    trace(toast ? "show toast" : "show details");
    owned = std::move(view); owned->show();
    if (!toast) { owned->requestActivate(); owned->rootObject()->forceActiveFocus(); }
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
        if (show && key == m_toastId) toastActive = true;
        if (enabled && show && !m_seenNotifications.contains(key)) { latest = row; m_toastId = key; toastActive = true; }
    }
    m_seenNotifications = current;
    if (!enabled || !toastActive) closeToast();
    if (latest.isEmpty()) return;
    QScreen *screen = nullptr;
    for (auto *candidate : QGuiApplication::screens())
        if ((options.value("output").toString() == "primary" && candidate == QGuiApplication::primaryScreen()) || options.value("output").toString() == candidate->name()) { screen = candidate; break; }
    if (screen) createAuxiliary("Toast.qml", {{"notification", latest}}, {}, screen, true);
}

}

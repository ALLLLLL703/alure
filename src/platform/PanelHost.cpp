#include "PanelHost.h"
#include "ConfigStore.h"
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
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
    const int edgeMargin = edge == W::AnchorTop ? m.top() : edge == W::AnchorBottom ? m.bottom() : edge == W::AnchorLeft ? m.left() : m.right();
    const auto layerName = panel.value("layer").toString();
    const auto layer = layerName == "background" ? W::LayerBackground : layerName == "bottom" ? W::LayerBottom : layerName == "overlay" ? W::LayerOverlay : W::LayerTop;
    int zone = panel.value("exclusive_zone").toInt();
    if (zone == -1) zone = thickness + edgeMargin;
    return {vertical ? QSize(thickness, length) : QSize(length, thickness), m, anchors, edge, layer, zone, vertical};
}
PanelHost::PanelHost(ConfigStore &config, QQmlEngine &engine, bool preview, QObject *parent)
    : QObject(parent), m_config(config), m_engine(engine), m_preview(preview) {
    connect(&config, &ConfigStore::modelChanged, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PanelHost::scheduleRebuild);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &PanelHost::scheduleRebuild);
    rebuild();
}
PanelHost::~PanelHost() = default;
void PanelHost::scheduleRebuild() {
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] { m_rebuildPending = false; rebuild(); });
}
void PanelHost::rebuild() {
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
            view->setInitialProperties({{"panel", panel}, {"vertical", placement.vertical}});
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
}

#pragma once
#include <LayerShellQt/Window>
#include <QMargins>
#include <QObject>
#include <QSize>
#include <QVariantMap>
#include <memory>
#include <vector>
class QQmlEngine;
class QQuickView;
namespace Alure {
class ConfigStore;
struct PanelPlacement {
    QSize size;
    QMargins margins;
    LayerShellQt::Window::Anchors anchors;
    LayerShellQt::Window::Anchor edge;
    LayerShellQt::Window::Layer layer;
    int exclusiveZone;
    bool vertical;
};
// Pure contract, shared with geometry tests; all values are logical pixels.
PanelPlacement panelPlacement(const QVariantMap &panel, QSize screenSize);
class PanelHost final : public QObject {
    Q_OBJECT
public:
    PanelHost(ConfigStore &config, QQmlEngine &engine, bool preview, QObject *parent = nullptr);
    ~PanelHost() override;
    void rebuild();
private:
    void scheduleRebuild();
    ConfigStore &m_config;
    QQmlEngine &m_engine;
    bool m_preview;
    bool m_rebuildPending = false;
    std::vector<std::unique_ptr<QQuickView>> m_windows;
};
}

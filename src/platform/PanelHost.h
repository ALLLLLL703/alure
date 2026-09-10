#pragma once
#include <LayerShellQt/Window>
#include <QMargins>
#include <QObject>
#include <QSize>
#include <QVariantMap>
#include <QSet>
#include <memory>
#include <vector>
class QQmlEngine;
class QQuickView;
class QScreen;
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
    Q_INVOKABLE void openModule(const QString &name, const QString &panelId, const QString &output);
    Q_INVOKABLE void closePopup();
    Q_INVOKABLE bool openSettings();
    Q_INVOKABLE void closeToast();
    void syncNotifications(const QVariantList &items);
private:
    void createAuxiliary(const QString &source, const QVariantMap &properties, const QVariantMap &panel, QScreen *screen, bool toast);
    void scheduleRebuild();
    void trace(const QString &message) const;
    ConfigStore &m_config;
    QQmlEngine &m_engine;
    bool m_preview;
    bool m_rebuildPending = false;
    std::vector<std::unique_ptr<QQuickView>> m_windows;
    std::unique_ptr<QQuickView> m_popup, m_toast;
    QSet<QString> m_seenNotifications;
    QString m_toastId;
    bool m_popupHadFocus = false;
};
}

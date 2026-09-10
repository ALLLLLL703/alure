#pragma once
#include <LayerShellQt/Window>
#include <QMargins>
#include <QObject>
#include <QSize>
#include <QVariantMap>
#include <QSet>
#include <QPointer>
#include <memory>
#include <vector>
class QQmlEngine;
class QQuickView;
class QQuickItem;
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
    Q_INVOKABLE void openModule(const QString &name, const QString &panelId, const QString &output, QQuickItem *anchor = nullptr, const QString &trayItem = {});
    Q_INVOKABLE void closePopup();
    Q_INVOKABLE bool openSettings();
    Q_INVOKABLE void closeToast();
    void syncNotifications(const QVariantList &items);
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void releasePopupKeyboard();
    void createPopup(const QString &name, const QVariantMap &panel, QQuickView *parent, QQuickItem *anchor, const QString &trayItem);
    void createToast(const QVariantMap &properties, QScreen *screen);
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
    QPointer<QQuickView> m_popupParent;
    QPointer<QQuickItem> m_popupAnchor;
    QString m_popupModule, m_popupTrayItem;
    bool m_popupHadFocus = false;
    quint64 m_popupRequest = 0;
};
}

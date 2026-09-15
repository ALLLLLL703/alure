#pragma once
#include <QObject>
#include <memory>
class QQmlEngine;
class QQuickView;
namespace Alure {
class ConfigStore;
// Exactly one centered surface, retained across registry/menu/submenu pages.
class TrayLauncherHost final : public QObject {
    Q_OBJECT
public:
    TrayLauncherHost(ConfigStore &config, QQmlEngine &engine, bool preview = false);
    ~TrayLauncherHost() override;
    bool ready() const;
    Q_INVOKABLE void closePopup();
    void activate();
signals:
    void finished();
private:
    bool eventFilter(QObject *, QEvent *) override;
    std::unique_ptr<QQuickView> m_window;
    bool m_closed = false, m_hadFocus = false, m_closeOnFocusLoss = false;
};
}

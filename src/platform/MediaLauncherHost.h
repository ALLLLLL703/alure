#pragma once
#include <QObject>
#include <memory>
class QQmlEngine;
class QQuickView;
namespace Alure {
class ConfigStore;
// Owns the launcher's single surface across registry and detail pages.
class MediaLauncherHost final : public QObject {
    Q_OBJECT
public:
    MediaLauncherHost(ConfigStore &config, QQmlEngine &engine, bool preview = false);
    ~MediaLauncherHost() override;
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

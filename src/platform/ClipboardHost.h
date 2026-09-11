#pragma once
#include "ConfigStore.h"
#include <QPointer>
#include <QTimer>
#include <memory>
#include <vector>
class QQmlEngine;
class QQuickView;
namespace Alure {
// One-shot cursor-local clipboard window; never constructs the shell's panel/services.
class ClipboardHost final : public QObject {
    Q_OBJECT
public:
    ClipboardHost(ConfigStore &config, QQmlEngine &engine, bool preview = false);
    ~ClipboardHost() override;
    Q_INVOKABLE void closePopup();
signals:
    void finished();
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void present(QQuickView *view, QPointF point, bool centered);
    void fallback();
    ConfigStore &m_config;
    QTimer m_deadline;
    std::vector<std::unique_ptr<QQuickView>> m_windows;
    QPointer<QQuickView> m_selected;
    bool m_preview = false, m_located = false, m_closed = false, m_hadFocus = false;
};
}

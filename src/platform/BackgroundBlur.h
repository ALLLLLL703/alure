#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QRegion>
#include <QTimer>

namespace Alure {
// One helper per window background, placed inside its QML Rectangle. It only
// requests compositor blur: no texture capture, render effects or polling.
class BackgroundBlur : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool blurEnabled READ blurEnabled WRITE setBlurEnabled NOTIFY blurEnabledChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
public:
    explicit BackgroundBlur(QQuickItem *parent = nullptr);
    ~BackgroundBlur() override;
    bool blurEnabled() const { return m_enabled; }
    void setBlurEnabled(bool enabled);
    qreal radius() const { return m_radius; }
    void setRadius(qreal radius);
    // Requested shape, not evidence that the compositor rendered the effect.
    QRegion requestedRegion() const { return m_region; }
    static void registerQmlType();
signals:
    void blurEnabledChanged();
    void radiusChanged();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void schedule();
    void watchAncestors();
    void sync();
    void detachWindow();
    QRegion shape() const;
    bool m_enabled = false;
    qreal m_radius = 0;
    QPointer<QQuickWindow> m_window;
    QRegion m_region;
    QTimer m_update;
    QList<QMetaObject::Connection> m_ancestorConnections;
};
}

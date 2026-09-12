#include "BackgroundBlur.h"
#include <KWindowEffects>
#include <QGuiApplication>
#include <QPainterPath>
#include <QQuickWindow>
#include <QtQml>
#include <algorithm>

namespace Alure {
namespace {
bool wayland() { return QGuiApplication::platformName().startsWith("wayland"); }

void diagnoseSupport() {
    // Protocol capabilities arrive asynchronously. Report once, after discovery,
    // without polling; KWindowSystem itself handles later capability changes.
    static bool scheduled = false;
    if (scheduled) return;
    scheduled = true;
    QTimer::singleShot(1000, qGuiApp, [] {
        if (!wayland() || !KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind))
            qInfo("Alure: background blur unavailable on this platform/compositor; retaining alpha transparency. theme.blur_enabled=false disables requests.");
    });
}
}

void BackgroundBlur::registerQmlType() {
    static const int type = qmlRegisterType<BackgroundBlur>("Alure", 1, 0, "BackgroundBlur");
    Q_UNUSED(type)
    // Start asynchronous protocol discovery before QML creates/maps the first
    // surface, not only after its background geometry becomes available.
    if (wayland()) (void)KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
}

BackgroundBlur::BackgroundBlur(QQuickItem *parent) : QQuickItem(parent) {
    m_update.setSingleShot(true);
    connect(&m_update, &QTimer::timeout, this, &BackgroundBlur::sync);
    connect(this, &QQuickItem::windowChanged, this, [this] {
        detachWindow();
        m_window = window();
        if (m_window) m_window->installEventFilter(this);
        watchAncestors();
    });
    watchAncestors();
    m_window = window();
    if (m_window) m_window->installEventFilter(this);
}

BackgroundBlur::~BackgroundBlur() { detachWindow(); }

void BackgroundBlur::detachWindow() {
    if (m_window) {
        m_window->removeEventFilter(this);
        if (wayland() && !m_region.isEmpty()) {
            KWindowEffects::enableBlurBehind(m_window, false);
            m_window->update();
        }
    }
    m_window = nullptr;
    m_region = {};
}

void BackgroundBlur::setBlurEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit blurEnabledChanged();
    schedule();
}

void BackgroundBlur::setRadius(qreal radius) {
    if (m_radius == radius) return;
    m_radius = radius;
    emit radiusChanged();
    schedule();
}

void BackgroundBlur::schedule() { m_update.start(0); }

void BackgroundBlur::watchAncestors() {
    for (const auto &connection : m_ancestorConnections) disconnect(connection);
    m_ancestorConnections.clear();
    for (QQuickItem *item = this; item; item = item->parentItem()) {
        const auto watch = [this, item](auto signal) {
            m_ancestorConnections.append(connect(item, signal, this, &BackgroundBlur::schedule));
        };
        watch(&QQuickItem::xChanged);
        watch(&QQuickItem::yChanged);
        watch(&QQuickItem::widthChanged);
        watch(&QQuickItem::heightChanged);
        watch(&QQuickItem::visibleChanged);
        watch(&QQuickItem::opacityChanged);
        watch(&QQuickItem::scaleChanged);
        watch(&QQuickItem::rotationChanged);
        watch(&QQuickItem::transformOriginChanged);
        m_ancestorConnections.append(connect(item, &QQuickItem::parentChanged, this, &BackgroundBlur::watchAncestors));
    }
    schedule();
}

QRegion BackgroundBlur::shape() const {
    if (!m_enabled || !m_window || !m_window->isVisible() || width() <= 0 || height() <= 0) return {};
    for (const QQuickItem *item = this; item; item = item->parentItem())
        if (!item->isVisible() || item->opacity() <= 0) return {};
    QPainterPath path;
    const qreal radius = std::clamp(m_radius, qreal(0), std::min(width(), height()) / 2);
    path.addRoundedRect(boundingRect(), radius, radius);
    auto polygon = path.toFillPolygon();
    for (auto &point : polygon) point = mapToScene(point);
    return QRegion(polygon.toPolygon()).intersected(QRect(QPoint(), m_window->size()));
}

void BackgroundBlur::sync() {
    const auto region = shape();
    if (m_region == region) return;
    m_region = region;
    if (!m_window) return;
    if (!region.isEmpty()) diagnoseSupport();
    if (wayland()) {
        // An empty region means the WHOLE window to KWindowEffects. Never pass
        // it with enable=true (notably clipboard probes and hidden panels).
        // Keep the request even before capabilities arrive; KDE replays it on
        // discovery, expose and native surface recreation and owns its lifetime.
        KWindowEffects::enableBlurBehind(m_window, !region.isEmpty(), region);
        m_window->update(); // Blur regions are double-buffered surface state.
    }
}

bool BackgroundBlur::eventFilter(QObject *, QEvent *event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
        event->type() == QEvent::Hide || event->type() == QEvent::Expose)
        schedule();
    return false;
}
}

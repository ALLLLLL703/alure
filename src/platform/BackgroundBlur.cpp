#include "BackgroundBlur.h"
#include <KWindowEffects>
#include <QGuiApplication>
#include <QPainterPath>
#include <QPlatformSurfaceEvent>
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
        m_surfaceAlive = m_window && m_window->handle();
        if (m_window) m_window->installEventFilter(this);
        watchAncestors();
    });
    watchAncestors();
    m_window = window();
    m_surfaceAlive = m_window && m_window->handle();
    if (m_window) m_window->installEventFilter(this);
}

BackgroundBlur::~BackgroundBlur() {
    // QQuickItem's base destructor can emit windowChanged/parentChanged after
    // our connection list and timer have already been destroyed.
    disconnect(this, nullptr, this, nullptr);
    for (const auto &connection : m_ancestorConnections) disconnect(connection);
    detachWindow();
}

void BackgroundBlur::detachWindow() {
    m_update.stop();
    if (m_window) {
        // A detached/deleted helper must clear its still-live window, but must
        // not issue protocol requests during native surface destruction.
        if (m_backendRequested && m_surfaceAlive) applyRegion({});
        m_window->removeEventFilter(this);
    }
    m_window = nullptr;
    m_surfaceAlive = false;
    m_backendRequested = false;
    m_region = {};
}

void BackgroundBlur::applyRegion(const QRegion &region) {
    if (!wayland() || !m_window || !m_surfaceAlive) return;
    // KWindowSystem 6.30's false path removes its destruction observers, then
    // creates a new ext-background-effect object. Reusing the QWindow address
    // can then send requests to that dead surface (fatal Wayland error).
    // Keep the library's ownership tracking, using a nonempty region wholly
    // outside the surface to disable visible blur. Empty means WHOLE window.
    const auto trackedRegion = region.isEmpty() ? QRegion(-1, -1, 1, 1) : region;
    KWindowEffects::enableBlurBehind(m_window, true, trackedRegion);
    m_backendRequested = true;
    m_window->update(); // Double-buffered surface state; no manual wl_commit.
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
    if (!m_enabled || !m_window || !m_surfaceAlive || !m_window->isVisible() || width() <= 0 || height() <= 0) return {};
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
    applyRegion(region);
}

bool BackgroundBlur::eventFilter(QObject *, QEvent *event) {
    if (event->type() == QEvent::PlatformSurface) {
        const auto type = static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType();
        m_surfaceAlive = type == QPlatformSurfaceEvent::SurfaceCreated;
        if (!m_surfaceAlive) {
            m_update.stop();
            m_region = {};
            // KWindowSystem keeps its own surface/window destruction listeners.
            // Do not untrack it or manufacture a new effect while tearing down.
        } else {
            schedule();
        }
    }
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
        event->type() == QEvent::Hide || event->type() == QEvent::Expose)
        schedule();
    return false;
}
}

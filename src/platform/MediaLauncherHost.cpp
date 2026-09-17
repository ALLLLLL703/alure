#include "MediaLauncherHost.h"
#include "ConfigStore.h"
#include <LayerShellQt/Window>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QQuickItem>
#include <QScreen>

namespace Alure {
MediaLauncherHost::MediaLauncherHost(ConfigStore &config, QQmlEngine &engine, bool preview) {
    const auto options = config.model().value("modules").toMap().value("media_launcher").toMap().value("behavior").toMap();
    const auto output = options.value("output").toString();
    QScreen *screen = nullptr;
    for (auto *candidate : QGuiApplication::screens())
        if ((output == "primary" && candidate == QGuiApplication::primaryScreen()) || candidate->name() == output) { screen = candidate; break; }
    if (!screen) { qWarning("Media launcher output is unavailable"); return; }
    engine.rootContext()->setContextProperty("Shell", this);
    m_closeOnFocusLoss = options.value("close_on_focus_loss").toBool();
    m_window = std::make_unique<QQuickView>(&engine, nullptr);
    auto *view = m_window.get();
    const QSize size(qMin(options.value("popup_width").toInt(), screen->size().width()), qMin(options.value("popup_height").toInt(), screen->size().height()));
    view->setTitle("Alure media launcher"); view->setScreen(screen); view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView); view->resize(size);
    view->setFlags(Qt::FramelessWindowHint);
    if (!preview) {
        using W = LayerShellQt::Window;
        auto *layer = W::get(view);
        layer->setScope("alure-media-launcher"); layer->setScreen(screen); layer->setLayer(W::LayerOverlay);
        layer->setAnchors(W::Anchors{}); layer->setExclusiveZone(-1); layer->setDesiredSize(size);
        layer->setKeyboardInteractivity(W::KeyboardInteractivityExclusive);
        layer->setActivateOnShow(true);
    } else view->setPosition(screen->geometry().center() - QPoint(size.width() / 2, size.height() / 2));
    view->installEventFilter(this);
    connect(screen, &QScreen::geometryChanged, this, &MediaLauncherHost::closePopup);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this, screen](QScreen *removed) { if (removed == screen) closePopup(); });
    connect(view, &QWindow::activeChanged, this, [this] {
        if (m_window->isActive()) m_hadFocus = true;
        else if (m_hadFocus && m_closeOnFocusLoss) closePopup();
    });
    view->setSource(QUrl("qrc:/qml/MediaLauncher.qml"));
    if (ready()) activate();
}
MediaLauncherHost::~MediaLauncherHost() {
    m_closed = true;
    if (m_window) { m_window->removeEventFilter(this); m_window->hide(); }
    m_window.reset();
}
bool MediaLauncherHost::ready() const { return m_window && m_window->status() == QQuickView::Ready; }
void MediaLauncherHost::activate() {
    if (!ready() || m_closed) return;
    m_window->show(); m_window->raise(); m_window->requestActivate();
}
void MediaLauncherHost::closePopup() {
    if (m_closed) return;
    m_closed = true;
    if (m_window) m_window->hide();
    emit finished();
}
bool MediaLauncherHost::eventFilter(QObject *, QEvent *event) {
    if (event->type() == QEvent::Close) { closePopup(); return true; }
    if (event->type() == QEvent::KeyPress && m_window && m_window->rootObject()
        && !m_window->rootObject()->property("registryPage").toBool()) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            const bool reverse = key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier);
            QMetaObject::invokeMethod(m_window->rootObject(), "moveDetailFocusFromHost", Q_ARG(QVariant, reverse));
            return true;
        }
    }
    return false;
}
}

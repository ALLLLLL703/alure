#include "ClipboardHost.h"
#include <LayerShellQt/Window>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>

namespace Alure {
ClipboardHost::ClipboardHost(ConfigStore &config, QQmlEngine &engine, bool preview)
    : m_config(config), m_preview(preview) {
    engine.rootContext()->setContextProperty("Shell", this);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, &ClipboardHost::fallback);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &ClipboardHost::closePopup);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &ClipboardHost::closePopup);
    for (auto *screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::geometryChanged, this, &ClipboardHost::closePopup);
        auto view = std::make_unique<QQuickView>(&engine, nullptr);
        view->setTitle("Alure clipboard"); view->setScreen(screen); view->setColor(Qt::transparent);
        view->setResizeMode(QQuickView::SizeRootObjectToView); view->resize(screen->size());
        view->installEventFilter(this);
        if (!preview) {
            using W = LayerShellQt::Window;
            view->setFlags(Qt::FramelessWindowHint);
            auto *layer = W::get(view.get());
            layer->setScope("alure-clipboard"); layer->setScreen(screen); layer->setLayer(W::LayerOverlay);
            layer->setExclusiveZone(-1); layer->setDesiredSize(screen->size());
            layer->setAnchors(W::Anchors(W::AnchorLeft) | W::AnchorRight | W::AnchorTop | W::AnchorBottom);
            layer->setKeyboardInteractivity(W::KeyboardInteractivityNone); layer->setActivateOnShow(false);
        }
        view->setSource(QUrl("qrc:/qml/ClipboardOverlay.qml"));
        if (view->status() != QQuickView::Ready) continue;
        connect(view.get(), &QWindow::activeChanged, this, [this, view = view.get()] {
            if (m_closed || m_selected != view) return;
            if (view->isActive()) m_hadFocus = true;
            else if (m_hadFocus && m_config.model().value("ui").toMap().value("close_on_focus_loss").toBool()) closePopup();
        });
        m_windows.push_back(std::move(view));
    }
    for (auto &view : m_windows) view->show();
    const auto options = config.model().value("modules").toMap().value("clipboard").toMap().value("behavior").toMap();
    m_deadline.start(options.value("cursor_timeout_ms").toInt());
}
ClipboardHost::~ClipboardHost() {
    m_closed = true; m_deadline.stop();
    for (auto &view : m_windows) view->hide();
    m_windows.clear(); // before the event-filter state and borrowed window pointer die
}
bool ClipboardHost::eventFilter(QObject *watched, QEvent *event) {
    auto *view = qobject_cast<QQuickView *>(watched);
    if (!view || m_closed) return false;
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && m_config.model().value("ui").toMap().value("escape_closes").toBool()) {
        closePopup(); return true;
    }
    if (event->type() == QEvent::Close) { closePopup(); return true; }
    if (event->type() == QEvent::Enter && !m_located) {
        m_located = true; m_deadline.stop();
        const auto position = static_cast<QEnterEvent *>(event)->position();
        const QPointer<QQuickView> guard(view);
        // Retire other probes after leaving the platform's pointer-enter callback.
        QTimer::singleShot(0, this, [this, guard, position] { if (guard && !m_closed) present(guard, position, false); });
    }
    return false;
}
void ClipboardHost::present(QQuickView *view, QPointF point, bool centered) {
    m_located = true; m_selected = view;
    for (auto &candidate : m_windows) if (candidate.get() != view) candidate->hide();
    auto *root = view->rootObject();
    root->setProperty("anchorPosition", point); root->setProperty("centered", centered); root->setProperty("cardVisible", true);
    if (!m_preview) LayerShellQt::Window::get(view)->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    else view->requestActivate();
    root->forceActiveFocus();
}
void ClipboardHost::fallback() {
    if (m_closed || m_located) return;
    const auto options = m_config.model().value("modules").toMap().value("clipboard").toMap().value("behavior").toMap();
    if (options.value("cursor_fallback").toString() == "center") {
        const auto output = options.value("output").toString();
        for (auto &view : m_windows) {
            if (output == view->screen()->name() || (output == "primary" && view->screen() == QGuiApplication::primaryScreen())) {
                qWarning("Alure clipboard: cursor position unavailable; using configured output center.");
                present(view.get(), {}, true); return;
            }
        }
    }
    qWarning("Alure clipboard: no cursor/output position available; closing probes."); closePopup();
}
void ClipboardHost::closePopup() {
    if (m_closed) return;
    m_closed = true; m_deadline.stop();
    for (auto &view : m_windows) view->hide();
    emit finished();
}
}

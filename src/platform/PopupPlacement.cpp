#include "PopupPlacement.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>
namespace Alure {
QRect visiblePopupAnchor(QQuickItem *item) {
    if (!item || !item->window() || !item->isVisible()) return {};
    QRectF bounds = item->mapRectToScene(item->boundingRect());
    for (auto *ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (!ancestor->isVisible()) return {};
        if (ancestor->clip()) bounds = bounds.intersected(ancestor->mapRectToScene(ancestor->boundingRect()));
    }
    const QRect panel(QPoint(), item->window()->size());
    return bounds.isEmpty() ? QRect() : bounds.toAlignedRect().intersected(panel);
}
PopupPlacement popupPlacement(QRect clicked, QSize parentSize, QSize outputSize,
                              const QString &panelEdge, const QVariantMap &ui) {
    PopupPlacement p{};
    clicked = clicked.intersected(QRect(QPoint(), parentSize));
    if (clicked.isEmpty() || outputSize.isEmpty()) return p;
    // Leave at least half even a tiny output for content, rather than consuming it with gap.
    p.padding = std::min(ui.value("popup_gap").toInt(), std::min(outputSize.width(), outputSize.height()) / 4);
    p.size = QSize(std::min(ui.value("popup_width").toInt() + 2 * p.padding, outputSize.width()),
                   std::min(ui.value("popup_height").toInt() + 2 * p.padding, outputSize.height()));
    auto direction = ui.value("popup_direction").toString();
    if (direction == "inward") direction = panelEdge == "top" ? "bottom" : panelEdge == "bottom" ? "top" : panelEdge == "left" ? "right" : "left";
    const bool vertical = direction == "left" || direction == "right";
    p.anchorRect = clicked;
    if (direction == "bottom") { p.anchorRect.setBottom(parentSize.height() - 1); p.anchor = p.gravity = Qt::BottomEdge; }
    else if (direction == "top") { p.anchorRect.setTop(0); p.anchor = p.gravity = Qt::TopEdge; }
    else if (direction == "right") { p.anchorRect.setRight(parentSize.width() - 1); p.anchor = p.gravity = Qt::RightEdge; }
    else { p.anchorRect.setLeft(0); p.anchor = p.gravity = Qt::LeftEdge; }
    const auto alignment = ui.value("popup_alignment").toString();
    if (alignment == "start") {
        p.anchor |= vertical ? Qt::TopEdge : Qt::LeftEdge;
        p.gravity |= vertical ? Qt::BottomEdge : Qt::RightEdge;
    } else if (alignment == "end") {
        p.anchor |= vertical ? Qt::BottomEdge : Qt::RightEdge;
        p.gravity |= vertical ? Qt::TopEdge : Qt::LeftEdge;
    }
    const QRectF r(p.anchorRect);
    qreal x = r.center().x(), y = r.center().y();
    if (p.anchor & Qt::LeftEdge) x = r.left();
    if (p.anchor & Qt::RightEdge) x = r.right();
    if (p.anchor & Qt::TopEdge) y = r.top();
    if (p.anchor & Qt::BottomEdge) y = r.bottom();
    x -= p.gravity & Qt::LeftEdge ? p.size.width() : p.gravity & Qt::RightEdge ? 0 : p.size.width() / 2.;
    y -= p.gravity & Qt::TopEdge ? p.size.height() : p.gravity & Qt::BottomEdge ? 0 : p.size.height() / 2.;
    p.position = QPoint(qRound(x), qRound(y));
    return p;
}
void configurePopupPositioner(QWindow &window, const PopupPlacement &p) {
    // Qt Wayland 6.9+ xdg-shell's explicit positioner overrides. No guessed global
    // position: the compositor attaches this xdg_popup to the actual layer surface.
    window.setProperty("_q_waylandPopupAnchorRect", p.anchorRect);
    window.setProperty("_q_waylandPopupAnchor", QVariant::fromValue(p.anchor));
    window.setProperty("_q_waylandPopupGravity", QVariant::fromValue(p.gravity));
    // xdg_positioner constraint_adjustment: slide_x/y + flip_x/y. Compositor decides.
    window.setProperty("_q_waylandPopupConstraintAdjustment", 15u);
}
}

#pragma once
#include <QRect>
#include <QVariantMap>
class QQuickItem;
class QWindow;
namespace Alure {
struct PopupPlacement {
    QRect anchorRect; // Parent-surface logical coordinates; always inside the panel.
    Qt::Edges anchor, gravity;
    QSize size; // Includes symmetric transparent padding, also after a compositor flip.
    int padding;
    QPoint position; // Parent-relative unconstrained position (non-Wayland preview only).
};
QRect visiblePopupAnchor(QQuickItem *item);
PopupPlacement popupPlacement(QRect clicked, QSize parentSize, QSize outputSize,
                              const QString &panelEdge, const QVariantMap &ui);
void configurePopupPositioner(QWindow &window, const PopupPlacement &placement);
}

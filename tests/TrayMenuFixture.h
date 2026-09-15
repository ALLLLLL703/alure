#pragma once
#include "TrayMenu.h"
#include <QDBusVariant>
#include <QDBusContext>
#include <QObject>

class FakeTrayMenu : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
public:
    int clicked = -1, shown = -1;
    bool checked = true, rejectSubmenu = false, holdSubmenu = false;
public slots:
    bool AboutToShow(int id) {
        shown = id;
        if (id == 5 && rejectSubmenu) sendErrorReply("org.alure.SubmenuFailure", "Synthetic submenu failure");
        if (id == 5 && holdSubmenu) setDelayedReply(true);
        return false;
    }
    uint GetLayout(int parent, int, const QStringList &, Alure::MenuLayout &layout) {
        const auto node = [](int id, QVariantMap props) { return QVariant::fromValue(QDBusVariant(QVariant::fromValue(Alure::MenuLayout{id, props, {}}))); };
        layout.id = parent;
        layout.children = parent == 0 ? QVariantList{
            node(1, {{"label", "_Open fixture"}}),
            node(2, {{"label", "Disabled"}, {"enabled", false}}),
            node(3, {{"type", "separator"}}),
            node(4, {{"label", "Checked"}, {"toggle-type", "checkmark"}, {"toggle-state", checked ? 1 : 0}}),
            node(5, {{"label", "More"}, {"children-display", "submenu"}}),
            node(6, {{"label", "Hidden"}, {"visible", false}})} : QVariantList{node(7, {{"label", "Submenu action"}})};
        return 1;
    }
    void Event(int id, const QString &event, const QDBusVariant &, uint) { if (event == "clicked") clicked = id; }
signals:
    void LayoutUpdated(uint revision, int parent);
};

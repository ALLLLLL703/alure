import QtQuick
import QtQuick.Controls
import "Ui.js" as Ui

Item {
    id: root
    required property string moduleName
    required property bool vertical
    required property real crossSize
    signal requested(Item anchor)
    readonly property var config: Config.model.modules[moduleName]
    readonly property var style: config.style
    readonly property var service: moduleName === "calendar" ? null : Services[moduleName] || null
    readonly property bool listMode: (moduleName === "workspaces" || moduleName === "tray") && service && service.available && service.items.length > 0
    property date now: new Date()
    readonly property string summary: moduleName === "calendar" ? Qt.formatDateTime(now, config.behavior.format) : !service || !service.available ? "Unavailable" : Ui.format(config.behavior.format, Ui.values(moduleName, service.state, service.items))
    implicitWidth: vertical ? crossSize : Math.min(style.max_width, listMode ? strip.contentWidth : Math.max(style.min_width, main.implicitWidth))
    implicitHeight: vertical ? (listMode ? Math.min(style.max_width, strip.contentHeight) : Config.model.ui.module_height) : crossSize
    Timer { interval: root.config.behavior.interval_ms; running: root.moduleName === "calendar"; repeat: true; onTriggered: root.now = new Date() }
    ShellButton {
        id: main
        objectName: root.moduleName + "-button"
        anchors.fill: parent
        visible: !root.listMode
        text: root.style.show_label && !root.vertical ? root.summary : ""
        iconName: root.style.show_icon ? root.style.icon : ""
        iconSize: root.style.icon_size
        foreground: root.style.foreground || Config.model.theme.palette.foreground
        baseColor: root.style.background || "transparent"
        Accessible.name: Ui.title(root.moduleName) + ": " + root.summary
        onClicked: root.requested(main)
        accessibleDescription: Ui.title(root.moduleName) + " · " + root.summary
    }
    Flickable {
        id: strip
        objectName: root.moduleName + "-list"
        anchors.fill: parent
        visible: root.listMode
        clip: true
        contentWidth: root.vertical ? width : items.implicitWidth
        contentHeight: root.vertical ? items.implicitHeight : height
        flickableDirection: root.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        Grid {
            id: items
            rows: root.vertical ? -1 : 1
            columns: root.vertical ? 1 : -1
            spacing: Config.model.theme.spacing / 2
            Repeater {
                model: root.listMode ? root.service.items : []
                delegate: ShellButton {
                    id: entry
                    required property var modelData
                    objectName: root.moduleName + "-entry-" + modelData.id
                    width: root.vertical ? root.width : Math.min(root.style.max_width, Math.max(root.style.min_width, implicitWidth))
                    height: root.vertical ? Config.model.ui.module_height : root.crossSize
                    text: root.style.show_label && root.moduleName === "workspaces" ? Ui.format(root.config.behavior.format, Object.assign({}, modelData, {name: modelData.name || String(modelData.idx)})) : root.style.show_label ? Ui.format(root.config.behavior.format, {title: modelData.Title || "Tray"}) : ""
                    iconName: root.style.show_icon ? root.style.icon : ""
                    iconSource: root.moduleName !== "tray" ? "" : modelData.iconUrl || "image://icons/theme/" + (modelData.Status === "NeedsAttention" ? modelData.AttentionIconName || modelData.IconName || root.style.icon : modelData.IconName || root.style.icon)
                    iconSize: root.style.icon_size
                    accent: root.moduleName === "workspaces" && !!modelData.is_active
                    foreground: root.style.foreground || Config.model.theme.palette.foreground
                    baseColor: root.style.background || "transparent"
                    Accessible.name: root.moduleName === "workspaces" ? "Workspace " + (modelData.name || modelData.idx) + " · " + modelData.output : modelData.Title || modelData.id
                    onClicked: {
                        if (!root.config.behavior.allow_actions) { root.requested(entry); return }
                        const point = mapToGlobal(width / 2, height / 2)
                        root.service.action("activate", {id: modelData.id, x: Math.round(point.x), y: Math.round(point.y)})
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton | Qt.MiddleButton
                        onTapped: function(eventPoint, button) {
                            if (root.moduleName !== "tray" || !root.config.behavior.allow_actions) { root.requested(entry); return }
                            const point = entry.mapToGlobal(entry.width / 2, entry.height / 2)
                            root.service.action(button === Qt.MiddleButton ? "secondaryActivate" : "contextMenu", {id: entry.modelData.id, x: Math.round(point.x), y: Math.round(point.y)})
                        }
                    }
                    accessibleDescription: Accessible.name
                }
            }
        }
    }
}

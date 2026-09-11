import QtQuick
import QtQuick.Controls
import "Ui.js" as Ui

Item {
    id: root
    required property string moduleName
    required property bool vertical
    required property real crossSize
    signal requested(Item anchor)
    signal menuRequested(string itemId, Item anchor)
    readonly property var config: Config.model.modules[moduleName]
    readonly property var style: config.style
    opacity: style.opacity
    readonly property var service: moduleName === "calendar" ? null : Services[moduleName] || null
    readonly property bool listMode: (moduleName === "workspaces" || moduleName === "tray") && service && service.available && service.items.length > 0
    readonly property bool sharedIcon: listMode && moduleName === "workspaces" && style.show_icon
    readonly property real iconExtent: sharedIcon ? style.icon_size + 2 * Config.model.ui.panel_padding : 0
    readonly property real iconOffset: sharedIcon ? iconExtent + Config.model.theme.spacing / 2 : 0
    property date now: new Date()
    readonly property string summary: moduleName === "calendar" ? Qt.formatDateTime(now, config.behavior.format) : !service || !service.available ? (moduleName === "clipboard" ? "Clipboard" : "Unavailable") : Ui.format(config.behavior.format, Ui.values(moduleName, service.state, service.items))
    implicitWidth: vertical ? crossSize : Math.min(style.max_width, listMode ? iconOffset + strip.contentWidth : Math.max(style.min_width, main.implicitWidth))
    implicitHeight: vertical ? (listMode ? Math.min(style.max_width, iconOffset + strip.contentHeight) : Config.model.ui.module_height) : crossSize
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
        WheelHandler {
            target: null
            enabled: root.moduleName === "volume" && root.config.enabled && root.config.behavior.scroll_enabled
                     && root.config.behavior.allow_actions && !!root.service && root.service.available && !!root.service.state.canSetVolume
            onWheel: event => {
                event.accepted = false
                if (event.angleDelta.y === 0) return
                const delta = event.angleDelta.y / 120 * root.config.behavior.scroll_step * (root.config.behavior.scroll_inverted ? -1 : 1)
                event.accepted = root.service.action("adjustVolume", {delta: delta})
            }
        }
    }
    ShellButton {
        id: workspaceIcon
        objectName: "workspaces-shared-icon"
        visible: root.sharedIcon
        width: root.vertical ? root.crossSize : root.iconExtent
        height: root.vertical ? root.iconExtent : root.crossSize
        iconName: root.style.icon
        iconSize: root.style.icon_size
        foreground: root.style.foreground || Config.model.theme.palette.foreground
        Accessible.name: "Workspace details"
        onClicked: root.requested(workspaceIcon)
    }
    Flickable {
        id: strip
        objectName: root.moduleName + "-list"
        x: root.vertical ? 0 : root.iconOffset
        y: root.vertical ? root.iconOffset : 0
        width: Math.max(0, root.width - x)
        height: Math.max(0, root.height - y)
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
                    iconName: root.moduleName !== "workspaces" && root.style.show_icon ? root.style.icon : ""
                    iconSource: root.moduleName !== "tray" ? "" : modelData.iconUrl || "image://icons/theme/" + (modelData.Status === "NeedsAttention" ? modelData.AttentionIconName || modelData.IconName || root.style.icon : modelData.IconName || root.style.icon)
                    iconSize: root.style.icon_size
                    accent: root.moduleName === "workspaces" && !!modelData.is_active
                    horizontalAlignment: root.moduleName === "workspaces" ? Text.AlignHCenter : Text.AlignLeft
                    highlightBackground: root.moduleName !== "workspaces" || root.style.active_indicator === "pill"
                    Rectangle {
                        visible: entry.accent && root.style.active_indicator === "underline"
                        x: root.vertical ? 0 : entry.padding
                        y: root.vertical ? entry.padding : entry.height - height
                        width: root.vertical ? Math.max(1, Config.model.theme.border_width) : Math.max(0, entry.width - 2 * entry.padding)
                        height: root.vertical ? Math.max(0, entry.height - 2 * entry.padding) : Math.max(1, Config.model.theme.border_width)
                        color: Config.model.theme.palette.accent
                    }
                    foreground: root.style.foreground || Config.model.theme.palette.foreground
                    baseColor: root.style.background || "transparent"
                    Accessible.name: root.moduleName === "workspaces" ? "Workspace " + (modelData.name || modelData.idx) + " · " + modelData.output : modelData.Title || modelData.id
                    onClicked: {
                        if (!root.config.behavior.allow_actions) { root.requested(entry); return }
                        if (root.moduleName === "tray" && modelData.ItemIsMenu) { root.menuRequested(modelData.id, entry); return }
                        const point = mapToGlobal(width / 2, height / 2)
                        root.service.action("activate", {id: modelData.id, x: Math.round(point.x), y: Math.round(point.y)})
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton | Qt.MiddleButton
                        onTapped: function(eventPoint, button) {
                            if (root.moduleName !== "tray" || !root.config.behavior.allow_actions) { root.requested(entry); return }
                            if (button === Qt.RightButton) { root.menuRequested(entry.modelData.id, entry); return }
                            const point = entry.mapToGlobal(entry.width / 2, entry.height / 2)
                            root.service.action("secondaryActivate", {id: entry.modelData.id, x: Math.round(point.x), y: Math.round(point.y)})
                        }
                    }
                    accessibleDescription: Accessible.name
                }
            }
        }
    }
}

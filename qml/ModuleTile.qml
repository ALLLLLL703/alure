import QtQuick
import QtQuick.Controls
import "Ui.js" as Ui

Item {
    id: tile
    required property string token
    required property var host
    property string sourcePath: ""
    property int sourceIndex: -1
    property bool held: false
    readonly property string title: token === "@spacer" ? "Fixed space" : token === "@stretch" ? "Flexible space" : token === "@settings" ? "Settings" : Ui.title(token)
    readonly property string iconName: token === "@spacer" ? "spacer" : token === "@stretch" ? "stretch" : token === "@settings" ? host.draft.ui.settings_icon : (host.draft.modules[token] || {}).style?.icon || "fallback"
    objectName: (sourcePath || "palette") + "-module-" + token + "-" + sourceIndex
    Accessible.name: title
    Accessible.description: Ui.moduleDescription(token)
    implicitWidth: implicitHeight
    implicitHeight: Config.model.ui.module_height + 6
    width: implicitWidth
    height: implicitHeight
    Rectangle {
        id: block
        objectName: "module-tile-block"
        ToolTip.text: tile.title + "\n" + Ui.moduleDescription(tile.token)
        ToolTip.delay: Config.model.settings.module_tooltip_delay_ms
        ToolTip.visible: Config.model.settings.module_tooltips && tile.visible && tile.enabled && mouse.containsMouse && !mouse.pressed && !tile.held
        width: tile.width; height: tile.height
        radius: Config.model.theme.radius / 2
        color: Config.model.theme.palette.surface
        border.color: tile.held ? Config.model.theme.palette.accent : Config.model.theme.palette.border
        Drag.active: tile.held
        Drag.source: tile
        Drag.keys: ["alure-module"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        z: tile.held ? 1000 : 0
        Image {
            objectName: "module-tile-icon"
            anchors.centerIn: parent
            width: Math.max(0, Math.min(Config.model.theme.icon_size, tile.width - 12, tile.height - 12))
            height: width
            source: "image://icons/builtin/" + tile.iconName
            sourceSize: Qt.size(width, height)
            fillMode: Image.PreserveAspectFit
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            drag.target: block
            onPositionChanged: { if (drag.active) tile.held = true }
            onReleased: { if (tile.held) block.Drag.drop(); tile.held = false; block.x = 0; block.y = 0 }
            onCanceled: { tile.held = false; block.x = 0; block.y = 0 }
        }
        states: State {
            when: tile.held
            ParentChange { target: block; parent: tile.host.dragLayer }
        }
    }
}

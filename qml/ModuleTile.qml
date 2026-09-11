import QtQuick
import "Ui.js" as Ui

Item {
    id: tile
    required property string token
    required property var host
    property string sourcePath: ""
    property int sourceIndex: -1
    property bool held: false
    implicitWidth: label.implicitWidth + 24
    implicitHeight: Config.model.ui.module_height + 6
    width: implicitWidth
    height: implicitHeight
    Rectangle {
        id: block
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
        InfoText {
            id: label
            anchors.fill: parent; anchors.margins: 6
            text: tile.token === "@spacer" ? "Fixed space" : tile.token === "@stretch" ? "Flexible space" : tile.token === "@settings" ? "Settings" : Ui.title(tile.token)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1; elide: Text.ElideRight
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
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

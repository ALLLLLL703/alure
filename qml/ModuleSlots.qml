import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Ui.js" as Ui

ColumnLayout {
    id: board
    required property var host
    readonly property var theme: Config.model.theme
    readonly property var keys: host.panelLayout === "linear" ? ["modules"] : ["modules_left", "modules_center", "modules_right"]
    readonly property string prefix: "panels." + host.panelIndex + "."
    Layout.fillWidth: true
    function list(key) {
        const pending = host.pendingFields[prefix + key]
        return pending === undefined ? host.draft.panels[host.panelIndex][key] : JSON.parse(pending)
    }
    function requestDrop(source, key, at) {
        const data = {token: source.token, sourcePath: source.sourcePath, sourceIndex: source.sourceIndex}
        Qt.callLater(function() { if (key) board.drop(data, key, at); else board.remove(data) })
    }
    function remove(source) {
        if (!source.sourcePath) return
        const key = source.sourcePath.split(".").pop()
        const next = list(key).slice(); next.splice(source.sourceIndex, 1)
        host.stageField(source.sourcePath, Ui.literal(next))
    }
    function drop(source, key, at) {
        const same = source.sourcePath === prefix + key
        let next = list(key).slice()
        if (same) { next.splice(source.sourceIndex, 1); if (source.sourceIndex < at) --at }
        else remove(source)
        if (source.token !== "@spacer" && source.token !== "@stretch") {
            const previous = next.indexOf(source.token)
            if (previous >= 0) { next.splice(previous, 1); if (previous < at) --at }
        }
        next.splice(Math.max(0, at), 0, source.token)
        host.stageModuleList(prefix + key, next)
        if (!source.token.startsWith("@")) host.stageField("modules." + source.token + ".enabled", "true")
    }
    InfoText { text: "Drag modules into a slot. Drop beside a block to reorder; drag back here to remove."; color: board.theme.palette.muted; Layout.fillWidth: true }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: availableFlow.implicitHeight + 16
        color: pool.containsDrag ? Qt.alpha(board.theme.palette.accent, 0.2) : "transparent"
        border.color: board.theme.palette.border
        radius: board.theme.radius
        Flow {
            id: availableFlow
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
            spacing: board.theme.spacing / 2
            Repeater {
                model: Object.keys(board.host.draft.modules || {}).concat(["@settings", "@spacer", "@stretch"])
                ModuleTile { required property string modelData; token: modelData; host: board.host }
            }
        }
        DropArea {
            id: pool
            anchors.fill: parent; keys: ["alure-module"]
            onDropped: drop => { board.requestDrop(drop.source, "", 0); drop.acceptProposedAction() }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: board.theme.spacing
        Repeater {
            model: board.keys
            Rectangle {
                id: zone
                required property string modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                Layout.preferredHeight: Math.max(120, zoneTitle.implicitHeight + zoneFlow.implicitHeight + 32)
                Layout.alignment: Qt.AlignTop
                radius: board.theme.radius
                border.color: target.containsDrag ? board.theme.palette.accent : board.theme.palette.border
                color: Qt.alpha(board.theme.palette.surface, 0.6)
                InfoText {
                    id: zoneTitle
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8
                    text: zone.modelData === "modules" ? "Linear strip" : zone.modelData === "modules_left" ? "Left / top" : zone.modelData === "modules_center" ? "Center" : "Right / bottom"
                    color: board.theme.palette.accent
                }
                DropArea {
                    id: target
                    anchors.fill: parent; keys: ["alure-module"]
                    onDropped: drop => { board.requestDrop(drop.source, zone.modelData, board.list(zone.modelData).length); drop.acceptProposedAction() }
                }
                Flow {
                    id: zoneFlow
                    anchors.top: zoneTitle.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8
                    spacing: board.theme.spacing / 2
                    Repeater {
                        model: board.list(zone.modelData)
                        Item {
                            required property string modelData
                            required property int index
                            width: Math.min(block.implicitWidth, zoneFlow.width)
                            height: block.implicitHeight
                            ModuleTile {
                                id: block
                                width: parent.width
                                token: parent.modelData; sourceIndex: parent.index
                                sourcePath: board.prefix + zone.modelData
                                host: board.host
                            }
                            DropArea {
                                anchors.fill: parent; keys: ["alure-module"]
                                onDropped: drop => { board.requestDrop(drop.source, zone.modelData, parent.index + (drop.x > width / 2 ? 1 : 0)); drop.acceptProposedAction() }
                            }
                        }
                    }
                }
            }
        }
    }
}

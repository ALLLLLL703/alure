import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var panel
    required property string outputName
    required property bool vertical
    readonly property var theme: Config.model.theme
    readonly property bool zoned: panel.layout === "three-zone"
    readonly property int inset: Math.min(Config.model.ui.panel_padding, Math.min(width, height) / 4)
    readonly property var lists: {
        const source = zoned ? [panel.modules_left, panel.modules_center, panel.modules_right] : [panel.modules, [], []]
        const result = source.map(list => list.filter(name => name === "@settings" ? Config.model.ui.show_settings : name.startsWith("@") || Config.model.modules[name].enabled))
        if (Config.model.ui.show_settings && !result.some(list => list.indexOf("@settings") >= 0)) result[zoned ? 2 : 0].push("@settings")
        return result
    }
    Rectangle {
        anchors.fill: parent
        radius: root.theme.radius
        color: Qt.alpha(root.theme.palette.background, root.theme.opacity)
        border.color: Qt.alpha(root.theme.palette.border, root.theme.opacity)
        border.width: root.theme.border_width
    }
    Flickable {
        id: modules
        objectName: "panel-modules"
        anchors.fill: parent
        anchors.margins: root.inset
        readonly property real viewportLength: root.vertical ? height : width
        readonly property real crossSize: root.vertical ? width : height
        readonly property real zoneGap: root.theme.spacing
        readonly property real totalLength: root.zoned ? viewportLength : Math.max(viewportLength, left.naturalLength)
        clip: true
        contentWidth: root.vertical ? width : totalLength
        contentHeight: root.vertical ? totalLength : height
        flickableDirection: root.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; visible: !root.vertical && modules.contentWidth > modules.width }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; visible: root.vertical && modules.contentHeight > modules.height }
        PanelGroup {
            id: left
            objectName: "panel-zone-left"
            tokens: root.lists[0]
            panel: root.panel
            vertical: root.vertical
            crossSize: modules.crossSize
            availableLength: root.zoned ? (modules.totalLength - center.mainLength) / 2 - modules.zoneGap : modules.totalLength
            onRequested: (name, anchor) => Shell.openModule(name, root.panel.id, root.outputName, anchor)
            onMenuRequested: (itemId, anchor) => Shell.openModule("tray", root.panel.id, root.outputName, anchor, itemId)
        }
        PanelGroup {
            id: center
            objectName: "panel-zone-center"
            tokens: root.lists[1]
            panel: root.panel
            vertical: root.vertical
            crossSize: modules.crossSize
            availableLength: Math.min(modules.totalLength, center.naturalLength)
            x: root.vertical ? 0 : (modules.totalLength - mainLength) / 2
            y: root.vertical ? (modules.totalLength - mainLength) / 2 : 0
            onRequested: (name, anchor) => Shell.openModule(name, root.panel.id, root.outputName, anchor)
            onMenuRequested: (itemId, anchor) => Shell.openModule("tray", root.panel.id, root.outputName, anchor, itemId)
        }
        PanelGroup {
            id: right
            objectName: "panel-zone-right"
            tokens: root.lists[2]
            panel: root.panel
            vertical: root.vertical
            crossSize: modules.crossSize
            availableLength: (modules.totalLength - center.mainLength) / 2 - modules.zoneGap
            x: root.vertical ? 0 : modules.totalLength - mainLength
            y: root.vertical ? modules.totalLength - mainLength : 0
            onRequested: (name, anchor) => Shell.openModule(name, root.panel.id, root.outputName, anchor)
            onMenuRequested: (itemId, anchor) => Shell.openModule("tray", root.panel.id, root.outputName, anchor, itemId)
        }
    }
}

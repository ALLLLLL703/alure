import QtQuick

Item {
    id: group
    required property var tokens
    required property var panel
    required property bool vertical
    required property real crossSize
    required property real availableLength
    signal requested(string moduleName, Item anchor)
    signal menuRequested(string itemId, Item anchor)
    readonly property real gap: Config.model.theme.spacing
    readonly property int stretchCount: tokens.filter(t => t === "@stretch").length
    readonly property real naturalLength: {
        let size = Math.max(0, entries.count - 1) * gap
        for (let i = 0; i < entries.count; ++i) { const item = entries.itemAt(i); if (item) size += item.naturalLength }
        return size
    }
    readonly property real stretchSize: stretchCount ? Math.max(0, availableLength - naturalLength) / stretchCount : 0
    readonly property real mainLength: naturalLength + stretchCount * stretchSize
    implicitWidth: vertical ? crossSize : mainLength
    implicitHeight: vertical ? mainLength : crossSize
    function offset(index) {
        let position = index * gap
        for (let i = 0; i < index; ++i) { const item = entries.itemAt(i); if (item) position += item.mainLength }
        return position
    }
    Repeater {
        id: entries
        model: group.tokens
        delegate: Item {
            id: slot
            required property string modelData
            required property int index
            readonly property bool moduleEntry: !modelData.startsWith("@")
            readonly property real naturalLength: modelData === "@spacer" ? group.panel.spacer_size : modelData === "@stretch" ? 0 : modelData === "@settings" ? Config.model.ui.module_height : module.item ? (group.vertical ? module.item.implicitHeight : module.item.implicitWidth) : 0
            readonly property real mainLength: modelData === "@stretch" ? group.stretchSize : naturalLength
            objectName: "panel-slot-" + modelData + "-" + index
            x: group.vertical ? 0 : group.offset(index)
            y: group.vertical ? group.offset(index) : 0
            width: group.vertical ? group.crossSize : mainLength
            height: group.vertical ? mainLength : group.crossSize
            Loader {
                id: module
                anchors.fill: parent
                active: slot.moduleEntry
                sourceComponent: ModuleStrip {
                    moduleName: slot.modelData
                    vertical: group.vertical
                    crossSize: group.crossSize
                    onRequested: anchor => group.requested(moduleName, anchor)
                    onMenuRequested: (itemId, anchor) => group.menuRequested(itemId, anchor)
                }
            }
            ShellButton {
                anchors.fill: parent
                visible: slot.modelData === "@settings"
                iconName: Config.model.ui.settings_icon
                Accessible.name: "Open Alure settings"
                onClicked: Shell.openSettings()
                accessibleDescription: "Alure settings"
            }
        }
    }
}

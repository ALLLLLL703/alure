import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: root
    required property string trayItem
    required property int popupPadding
    property bool bottomAligned: false
    readonly property var menu: Services.tray.menu
    readonly property var theme: Config.model.theme
    Component.onCompleted: { Services.tray.openMenu(trayItem); list.forceActiveFocus() }
    Component.onDestruction: menu.close()
    Connections {
        target: root.Window.window
        function onVisibleChanged() { if (!root.Window.window.visible) root.menu.close() }
    }
    Connections { target: root.menu; function onActivated() { Shell.closePopup() } }
    Keys.onEscapePressed: event => { if (Config.model.ui.escape_closes) Shell.closePopup(); event.accepted = true }
    Rectangle {
        x: root.popupPadding
        y: root.bottomAligned ? root.height - height - root.popupPadding : root.popupPadding
        width: root.width - 2 * root.popupPadding
        height: Math.min(root.height - 2 * root.popupPadding, content.implicitHeight + 2 * root.theme.padding)
        color: Qt.alpha(root.theme.palette.background, root.theme.opacity)
        border.color: root.theme.palette.border
        border.width: root.theme.border_width
        radius: root.theme.radius
        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: root.theme.padding
            spacing: root.theme.spacing / 2
            ShellButton {
                objectName: "tray-menu-back"
                visible: root.menu.canGoBack
                text: "‹ Back"
                enabled: !root.menu.loading
                Layout.fillWidth: true
                onClicked: root.menu.back()
            }
            InfoText {
                visible: text.length > 0
                text: root.menu.error || (root.menu.loading ? "Loading menu…" : root.menu.items.length === 0 ? "No menu items" : "")
                color: root.theme.palette.muted
                Layout.fillWidth: true
            }
            ListView {
                id: list
                objectName: "tray-menu-list"
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: contentHeight
                clip: true
                model: root.menu.items
                currentIndex: -1
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar { }
                Keys.onReturnPressed: { if (currentItem && currentItem.enabled) root.menu.select(currentItem.modelData.id) }
                Keys.onEnterPressed: { if (currentItem && currentItem.enabled) root.menu.select(currentItem.modelData.id) }
                Keys.onRightPressed: { if (currentItem && currentItem.enabled && currentItem.modelData.submenu) root.menu.select(currentItem.modelData.id) }
                Keys.onLeftPressed: root.menu.back()
                delegate: ShellButton {
                    required property var modelData
                    required property int index
                    readonly property bool separator: modelData.type === "separator"
                    objectName: "tray-menu-item-" + modelData.id
                    width: list.width
                    height: separator ? root.theme.spacing : Math.max(Config.model.ui.module_height, implicitContentHeight + 2 * padding)
                    enabled: !separator && modelData.enabled && !root.menu.loading
                    accent: ListView.isCurrentItem
                    // DBusMenu mnemonics use underscores, not Qt's ampersands.
                    text: separator ? "" : (modelData["toggle-type"] ? (modelData["toggle-state"] === 1 ? (modelData["toggle-type"] === "radio" ? "● " : "✓ ") : modelData["toggle-state"] === 0 ? "○ " : "− ") : "") + String(modelData.label || "").replace(/__|_/g, m => m === "__" ? "_" : "") + (modelData.submenu ? "  ›" : "")
                    iconName: !separator ? modelData["icon-name"] || "" : ""
                    iconSource: iconName ? "image://icons/theme/" + iconName : ""
                    Accessible.description: modelData.submenu ? "Submenu" : ""
                    onClicked: root.menu.select(modelData.id)
                    Rectangle {
                        visible: parent.separator
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: root.theme.border_width
                        color: root.theme.palette.border
                    }
                }
            }
        }
    }
}

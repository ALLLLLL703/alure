import QtQuick

Item {
    id: root
    property bool cardVisible: false
    property bool centered: false
    property point anchorPosition: Qt.point(0, 0)
    readonly property var options: Config.model.modules.clipboard.behavior
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPressed: mouse => {
            if (root.cardVisible && root.options.close_on_outside_click &&
                (mouse.x < card.x || mouse.x >= card.x + card.width || mouse.y < card.y || mouse.y >= card.y + card.height)) Shell.closePopup()
        }
    }
    ClipboardPopup {
        id: card
        objectName: "clipboard-card"
        visible: root.cardVisible
        width: Math.min(root.options.popup_width, root.width)
        height: Math.min(root.options.popup_height, root.height)
        x: root.centered ? (root.width - width) / 2 : Math.max(0, Math.min(root.anchorPosition.x + root.options.cursor_gap, root.width - width))
        y: root.centered ? (root.height - height) / 2 : Math.max(0, Math.min(root.anchorPosition.y + root.options.cursor_gap, root.height - height))
        focus: true
    }
}

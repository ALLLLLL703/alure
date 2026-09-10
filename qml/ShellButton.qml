import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool accent: false
    property string accessibleDescription: ""
    property string iconName: ""
    property string iconSource: ""
    property int iconSize: Config.model.theme.icon_size
    property color foreground: Config.model.theme.palette.foreground
    property color baseColor: "transparent"
    readonly property var theme: Config.model.theme
    font.family: theme.font
    font.pixelSize: theme.font_size
    implicitHeight: Config.model.ui.module_height
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, implicitHeight)
    padding: Config.model.ui.panel_padding
    hoverEnabled: true
    Accessible.name: text || iconName
    background: Rectangle {
        radius: Math.min(control.theme.radius, height / 2)
        color: control.down ? Qt.alpha(control.theme.palette.accent, 0.3) : control.accent || control.hovered || control.visualFocus ? Qt.alpha(control.theme.palette.accent, 0.16) : control.baseColor
        border.width: control.visualFocus ? control.theme.border_width : 0
        border.color: control.theme.palette.accent
        Behavior on color { ColorAnimation { duration: Config.model.ui.animation_ms } }
    }
    Accessible.description: accessibleDescription
    contentItem: Item {
        implicitWidth: label.implicitWidth + (control.iconName ? control.iconSize + spacing : 0)
        implicitHeight: Math.max(label.implicitHeight, control.iconSize)
        readonly property real spacing: control.iconName && control.text ? control.theme.spacing / 2 : 0
        opacity: control.enabled ? 1 : 0.45
        Image {
            visible: control.iconName.length > 0
            width: visible ? control.iconSize : 0
            height: control.iconSize
            anchors.verticalCenter: parent.verticalCenter
            sourceSize: Qt.size(width, height)
            source: !visible ? "" : control.iconSource || "image://icons/" + control.theme.icon_mode + "/" + control.iconName
        }
        Text {
            id: label
            text: control.text
            textFormat: Text.PlainText
            x: control.iconName ? control.iconSize + parent.spacing : 0
            width: Math.max(0, control.availableWidth - (control.iconName ? control.iconSize + parent.spacing : 0))
            height: Math.max(implicitHeight, control.iconSize)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: control.accent ? control.theme.palette.accent : control.foreground
            font: control.font
        }
    }
}

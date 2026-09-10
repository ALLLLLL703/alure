import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property var panel
    required property bool vertical
    readonly property var theme: Config.model.theme
    Rectangle {
        anchors.fill: parent
        radius: root.theme.radius
        // Alpha applies to the backdrop only: text/icons remain legible.
        color: Qt.alpha(root.theme.palette.background, root.theme.opacity)
        border.color: Qt.alpha(root.theme.palette.border, root.theme.opacity)
        border.width: root.theme.border_width
    }
    GridLayout {
        anchors.fill: parent
        anchors.margins: Math.min(root.theme.padding, Math.min(root.width, root.height) / 4)
        columns: root.vertical ? 1 : 3
        columnSpacing: root.theme.spacing
        rowSpacing: root.theme.spacing
        Image {
            visible: Config.model.foundation.show_icon
            source: "image://icons/" + root.theme.icon_mode + "/" + Config.model.foundation.icon
            sourceSize: Qt.size(root.theme.icon_size, root.theme.icon_size)
            Layout.preferredWidth: root.theme.icon_size
            Layout.preferredHeight: root.theme.icon_size
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
        }
        Text {
            text: Config.model.foundation.label
            color: root.theme.palette.foreground
            font.family: root.theme.font
            font.pixelSize: root.theme.font_size
            font.bold: Config.model.foundation.bold
            elide: Text.ElideRight
            Layout.maximumWidth: root.vertical ? root.width : root.width / 3
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
        }
        Text {
            visible: Config.model.foundation.show_notice
            text: Config.model.foundation.notice
            color: root.theme.palette.muted
            font.family: root.theme.font
            font.pixelSize: root.theme.font_size
            elide: Text.ElideRight
            wrapMode: root.vertical ? Text.WrapAnywhere : Text.NoWrap
            Layout.fillWidth: true
            Layout.fillHeight: root.vertical
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: root.vertical ? Text.AlignHCenter : Text.AlignRight
        }
    }
}

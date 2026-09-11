import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    required property var snapshot
    readonly property var options: Config.model.ui.osd
    readonly property var theme: Config.model.theme
    readonly property color foreground: options.foreground || theme.palette.foreground
    readonly property color accent: options.accent || theme.palette.accent
    readonly property bool muted: snapshot.kind === "volume" && !!snapshot.muted
    readonly property string label: snapshot.kind === "volume" ? (muted ? "Muted" : "Volume") : snapshot.kind === "keyboard" ? "Keyboard brightness" : "Display brightness"
    objectName: "osd-card"
    color: options.background || theme.palette.background
    opacity: options.opacity
    radius: options.radius
    Accessible.role: Accessible.Indicator
    Accessible.name: label + " " + Math.round(snapshot.percent) + "%"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Math.min(root.options.padding, Math.min(root.width, root.height) / 4)
        spacing: root.options.spacing
        RowLayout {
            Layout.fillWidth: true
            spacing: root.options.spacing
            Image {
                visible: root.options.show_icon
                Layout.preferredWidth: root.options.icon_size
                Layout.preferredHeight: root.options.icon_size
                source: "image://icons/" + root.theme.icon_mode + "/" + root.options[root.snapshot.kind + "_icon"]
                fillMode: Image.PreserveAspectFit
            }
            Text {
                visible: root.options.show_label
                Layout.fillWidth: true
                text: root.label
                color: root.foreground
                font.family: root.theme.font
                font.pixelSize: root.options.font_size
                elide: Text.ElideRight
            }
            Text {
                objectName: "osd-value"
                Layout.fillWidth: !root.options.show_label
                horizontalAlignment: Text.AlignRight
                text: root.snapshot.kind === "keyboard" ? root.snapshot.level + " / " + root.snapshot.maximum : Math.round(root.snapshot.percent) + "%"
                color: root.foreground
                font.family: root.theme.font
                font.pixelSize: root.options.font_size
            }
        }
        Rectangle {
            objectName: "osd-bar"
            Layout.fillWidth: true
            Layout.preferredHeight: root.options.bar_height
            radius: height / 2
            color: Qt.alpha(root.foreground, 0.2)
            Rectangle {
                height: parent.height
                width: parent.width * (root.muted ? 0 : Math.max(0, Math.min(100, root.snapshot.percent))) / 100
                radius: parent.radius
                color: root.accent
            }
        }
    }
}

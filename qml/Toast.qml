import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    required property var notification
    readonly property var theme: Config.model.theme
    padding: theme.padding
    background: Rectangle {
        radius: root.theme.radius
        color: Qt.alpha(root.theme.palette.background, root.theme.opacity)
        border.width: root.theme.border_width
        border.color: root.theme.palette.accent
    }
    Timer { interval: Config.model.ui.toast.duration_ms; running: true; onTriggered: Shell.closeToast() }
    contentItem: ColumnLayout {
        spacing: root.theme.spacing / 2
        RowLayout {
            InfoText { text: root.notification.appName; color: root.theme.palette.accent; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
            ShellButton {
                text: "×"; Accessible.name: "Hide notification banner"
                onClicked: Shell.closeToast()
            }
        }
        InfoText { text: root.notification.summary; font.bold: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
        InfoText { text: root.notification.body; Layout.fillWidth: true; Layout.fillHeight: true; maximumLineCount: 2; elide: Text.ElideRight; clip: true }
        InfoText { text: "Actions and full text in notification history"; color: root.theme.palette.muted; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    required property var notification
    readonly property var theme: Config.model.theme
    readonly property var options: Config.model.ui.toast
    readonly property var moduleConfig: Config.model.modules.notifications
    opacity: moduleConfig.style.opacity
    padding: options.padding
    clip: true
    function activate() {
        if (moduleConfig.behavior.allow_actions && Services.notifications.action("activate", {id: notification.id})) Shell.closeToast()
    }
    Accessible.role: Accessible.Button
    Accessible.name: notification.appName + ": " + notification.summary
    Accessible.onPressAction: activate()
    background: Rectangle {
        radius: root.theme.radius
        color: Qt.alpha(root.theme.palette.background, root.theme.opacity)
        border.width: root.theme.border_width
        border.color: root.theme.palette.accent
        MouseArea {
            anchors.fill: parent
            enabled: root.moduleConfig.behavior.allow_actions
            onClicked: root.activate()
        }
    }
    Timer { interval: root.options.duration_ms; running: true; onTriggered: Shell.closeToast() }
    contentItem: RowLayout {
        spacing: root.options.spacing
        Image {
            id: appImage
            visible: root.options.show_icon
            Layout.preferredWidth: Math.min(root.options.icon_size, root.availableHeight)
            Layout.preferredHeight: Layout.preferredWidth
            source: root.notification.iconUrl || "image://icons/builtin/notifications"
            sourceSize: Qt.size(128, 128)
            cache: false
            fillMode: Image.PreserveAspectFit
            Image { anchors.fill: parent; visible: appImage.status === Image.Error; source: "image://icons/builtin/notifications"; fillMode: Image.PreserveAspectFit }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: root.options.spacing
            InfoText { text: root.notification.appName; font.pixelSize: root.options.font_size; color: root.theme.palette.accent; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
            InfoText { text: root.notification.summary; font.pixelSize: root.options.font_size; font.bold: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
            InfoText { text: root.notification.body; font.pixelSize: root.options.font_size; Layout.fillWidth: true; maximumLineCount: root.options.body_lines; elide: Text.ElideRight }
        }
        ShellButton {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: root.options.icon_size
            Layout.preferredHeight: root.options.icon_size
            iconName: "close"
            iconSize: Math.min(18, root.options.icon_size)
            Accessible.name: "Hide notification banner"
            onClicked: Shell.closeToast()
        }
    }
}

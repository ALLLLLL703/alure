import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var panel
    required property string outputName
    required property bool vertical
    readonly property var theme: Config.model.theme
    readonly property int inset: Math.min(Config.model.ui.panel_padding, Math.min(width, height) / 4)
    Rectangle {
        anchors.fill: parent
        radius: root.theme.radius
        color: Qt.alpha(root.theme.palette.background, root.theme.opacity)
        border.color: Qt.alpha(root.theme.palette.border, root.theme.opacity)
        border.width: root.theme.border_width
    }
    Flickable {
        id: modules
        anchors.fill: parent
        anchors.margins: root.inset
        clip: true
        contentWidth: root.vertical ? width : flow.implicitWidth
        contentHeight: root.vertical ? flow.implicitHeight : height
        flickableDirection: root.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; visible: !root.vertical && modules.contentWidth > modules.width }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; visible: root.vertical && modules.contentHeight > modules.height }
        Grid {
            id: flow
            rows: root.vertical ? -1 : 1
            columns: root.vertical ? 1 : -1
            spacing: root.theme.spacing
            Repeater {
                model: root.panel.modules.filter(name => Config.model.modules[name].enabled)
                delegate: ModuleStrip {
                    required property string modelData
                    moduleName: modelData
                    vertical: root.vertical
                    crossSize: root.vertical ? modules.width : modules.height
                    onRequested: Shell.openModule(moduleName, root.panel.id, root.outputName)
                }
            }
            ShellButton {
                visible: Config.model.ui.show_settings
                width: root.vertical ? modules.width : implicitWidth
                height: root.vertical ? Config.model.ui.module_height : modules.height
                iconName: Config.model.ui.settings_icon
                Accessible.name: "Open Alure settings"
                onClicked: Shell.openSettings()
                accessibleDescription: "Alure settings"
            }
        }
    }
}

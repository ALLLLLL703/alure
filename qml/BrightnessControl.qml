import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var service
    required property var config
    required property string kind
    readonly property var device: service ? service.state[kind] || ({}) : ({})
    readonly property var options: config.behavior[kind]
    readonly property bool keyboard: kind === "keyboard"
    readonly property bool ready: !!device.available
    readonly property bool canSet: ready && config.enabled && config.behavior.allow_actions && !!device.canSet
    readonly property real lower: keyboard ? device.minimum || 0 : options.min_percent
    readonly property real upper: keyboard ? device.limit || 0 : options.max_percent
    Layout.fillWidth: true
    spacing: Config.model.theme.spacing
    InfoText { text: root.keyboard ? "Keyboard backlight" : "Display brightness"; font.bold: true; Layout.fillWidth: true }
    InfoText {
        text: root.ready ? root.device.device + " · " + Math.round(root.device.percent) + "%" + (root.keyboard ? " · Level " + root.device.level + " / " + root.device.maximum : "") : root.device.diagnostic || "Unavailable"
        Layout.fillWidth: true
    }
    InfoText { visible: root.ready && !!root.device.diagnostic; text: root.device.diagnostic || ""; color: Config.model.theme.palette.accent; Layout.fillWidth: true }
    Slider {
        id: slider
        objectName: root.kind + "-brightness-slider"
        Layout.fillWidth: true
        from: root.lower; to: Math.max(from, root.upper)
        stepSize: 1
        snapMode: Slider.SnapAlways
        enabled: root.canSet
        Binding {
            target: slider; property: "value"
            when: !slider.pressed && (!root.service || !root.service.adjusting)
            value: root.ready ? (root.keyboard ? root.device.level : root.device.percent) : root.lower
            restoreMode: Binding.RestoreNone
        }
        Accessible.name: root.keyboard ? "Keyboard backlight level" : "Display brightness percent"
        onMoved: root.service.action("setBrightness", root.keyboard ? {kind: root.kind, level: value} : {kind: root.kind, percent: value})
    }
    RowLayout {
        ShellButton { objectName: root.kind + "-brightness-down"; iconName: "minus"; Accessible.name: "Decrease " + root.kind + " brightness"; enabled: root.canSet; onClicked: root.service.action("adjustBrightness", {kind: root.kind, delta: -root.options.step}) }
        ShellButton { objectName: root.kind + "-brightness-up"; iconName: "up"; Accessible.name: "Increase " + root.kind + " brightness"; enabled: root.canSet; onClicked: root.service.action("adjustBrightness", {kind: root.kind, delta: root.options.step}) }
    }
}

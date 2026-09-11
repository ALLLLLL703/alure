import QtQuick
import QtQuick.Layouts
import "Ui.js" as Ui

ColumnLayout {
    id: root
    readonly property var behavior: Config.model.modules.calendar.behavior
    property date today: new Date()
    property date selected: today
    property int year: today.getFullYear()
    property int month: today.getMonth()
    readonly property var cells: Ui.monthCells(year, month, behavior.first_day_of_week)
    spacing: Config.model.theme.spacing
    function navigate(delta) {
        const date = new Date(year, month + delta, 1)
        year = date.getFullYear(); month = date.getMonth()
    }
    Timer { interval: root.behavior.interval_ms; running: true; repeat: true; onTriggered: root.today = new Date() }
    RowLayout {
        Layout.fillWidth: true
        ShellButton { iconName: "previous"; Accessible.name: "Previous month"; enabled: root.behavior.allow_actions; onClicked: root.navigate(-1) }
        InfoText { text: Qt.formatDate(new Date(root.year, root.month, 1), root.behavior.month_format); font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
        ShellButton { iconName: "next"; Accessible.name: "Next month"; enabled: root.behavior.allow_actions; onClicked: root.navigate(1) }
    }
    GridLayout {
        columns: 7
        columnSpacing: Config.model.theme.spacing / 2
        rowSpacing: Config.model.theme.spacing / 2
        Layout.fillWidth: true
        Repeater {
            model: 7
            InfoText {
                required property int index
                text: Qt.formatDate(new Date(2024, 0, 7 + (index + root.behavior.first_day_of_week) % 7), "ddd")
                color: Config.model.theme.palette.muted
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.preferredWidth: 1
            }
        }
        Repeater {
            model: root.cells
            ShellButton {
                required property date modelData
                text: String(modelData.getDate())
                accent: Ui.sameDay(modelData, root.selected)
                foreground: modelData.getMonth() === root.month ? Config.model.theme.palette.foreground : Config.model.theme.palette.muted
                baseColor: Ui.sameDay(modelData, root.today) ? Config.model.theme.palette.surface : "transparent"
                enabled: root.behavior.allow_actions
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Accessible.name: Qt.formatDate(modelData, Qt.DefaultLocaleLongDate)
                onClicked: root.selected = modelData
            }
        }
    }
    InfoText { text: Qt.formatDate(root.selected, Qt.DefaultLocaleLongDate); Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
    ShellButton {
        text: "Today"; Layout.alignment: Qt.AlignHCenter; enabled: root.behavior.allow_actions
        onClicked: { root.year = root.today.getFullYear(); root.month = root.today.getMonth(); root.selected = root.today }
    }
    InfoText { text: "Local calendar · no event provider"; color: Config.model.theme.palette.muted; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Ui.js" as Ui

ColumnLayout {
    id: field
    required property var spec
    required property var host
    objectName: "setting-field-" + spec.path
    readonly property var pending: host.pendingFields[spec.path]
    readonly property var value: {
        if (pending === undefined) return spec.value
        try { return JSON.parse(pending) } catch (e) { return pending }
    }
    readonly property var theme: Config.model.theme
    spacing: theme.spacing / 2
    Layout.fillWidth: true
    function stage(value) { if (spec.kind === "modules") host.stageModuleList(spec.path, value); else host.stageField(spec.path, Ui.literal(value)) }
    function moduleTitle(name) { return name === "@spacer" ? "Fixed space" : name === "@stretch" ? "Flexible space" : name === "@settings" ? "Settings shortcut" : Ui.title(name) }
    InfoText { visible: field.spec.showGroup; text: field.spec.group; color: field.theme.palette.accent; font.bold: true; font.pixelSize: field.theme.font_size * 1.2; Layout.fillWidth: true }
    InfoText { text: field.spec.label; font.bold: true; Layout.fillWidth: true }
    InfoText { visible: text.length > 0; text: field.spec.help; color: field.theme.palette.muted; Layout.fillWidth: true }
    Flow {
        visible: field.spec.kind === "theme"
        Layout.fillWidth: true
        spacing: field.theme.spacing
        Repeater {
            model: field.spec.kind === "theme" ? Config.themeNames : []
            ShellButton {
                required property string modelData
                readonly property var previewPalette: Config.inspectText("[theme]\nname = " + Ui.literal(modelData)).model.theme.palette
                objectName: "theme-choice-" + modelData
                text: modelData
                baseColor: previewPalette.background
                foreground: previewPalette.foreground
                accent: field.value === modelData
                onClicked: field.stage(modelData)
                Accessible.name: modelData + " theme preview"
            }
        }
    }
    Switch {
        palette.dark: field.theme.palette.accent
        visible: field.spec.kind === "boolean"
        objectName: "field-" + field.spec.path + "-switch"
        text: checked ? "Enabled" : "Disabled"
        checked: !!field.value
        onClicked: field.stage(checked)
        Accessible.name: field.spec.label
    }
    ComboBox {
        visible: field.spec.kind === "enum" || field.spec.kind === "font"
        objectName: "field-" + field.spec.path + "-choice"
        Layout.fillWidth: true
        model: field.spec.kind === "font" ? Qt.fontFamilies() : field.spec.options
        editable: field.spec.kind === "font"
        currentIndex: model.indexOf(field.value)
        editText: String(field.value)
        onActivated: field.stage(currentText)
        onEditTextChanged: { if (editable && activeFocus) field.stage(editText) }
        Accessible.name: field.spec.label
    }
    RowLayout {
        visible: field.spec.kind === "number"
        Layout.fillWidth: true
        Slider {
            palette.dark: field.theme.palette.accent
            objectName: "field-" + field.spec.path + "-slider"
            Layout.fillWidth: true
            from: field.spec.low; to: field.spec.high; stepSize: field.spec.step
            value: Number(field.value)
            // Only stage: replacing the form model during a drag destroys the grabbed delegate.
            onMoved: field.stage(Number(value.toFixed(field.spec.step < 1 ? 2 : 0)))
            Accessible.name: field.spec.label
        }
        TextField {
            objectName: "field-" + field.spec.path + "-number"
            Layout.preferredWidth: field.theme.font_size * 7
            text: String(field.value)
            selectByMouse: true
            onTextEdited: field.host.stageField(field.spec.path, text)
            onAccepted: field.host.flushFields()
            Accessible.name: field.spec.label + " exact value"
        }
    }
    RowLayout {
        visible: ["string", "color", "argv"].indexOf(field.spec.kind) >= 0
        Layout.fillWidth: true
        Rectangle {
            visible: field.spec.kind === "color"
            Layout.preferredWidth: field.theme.icon_size * 2
            Layout.preferredHeight: field.theme.icon_size * 2
            color: Config.validColor(String(field.value)) ? field.value || "transparent" : "transparent"
            border.color: field.theme.palette.foreground
            radius: field.theme.radius / 2
        }
        TextField {
            objectName: "field-" + field.spec.path + "-text"
            Layout.fillWidth: true
            text: field.spec.kind === "argv" ? (field.pending === undefined ? Ui.literal(field.spec.value) : field.pending) : String(field.value)
            selectByMouse: true
            placeholderText: field.spec.kind === "color" ? "Inherit" : ""
            onTextEdited: { if (field.spec.kind === "argv") field.host.stageField(field.spec.path, text); else field.stage(text) }
            onAccepted: field.host.flushFields()
            Accessible.name: field.spec.label
        }
        ShellButton {
            visible: field.spec.kind === "color"
            objectName: "field-" + field.spec.path + "-color"
            text: "Choose…"
            onClicked: field.host.pickColor(field.spec.path, String(field.value))
        }
    }
    ColumnLayout {
        visible: field.spec.kind === "modules"
        Layout.fillWidth: true
        readonly property var selected: field.spec.kind === "modules" ? field.value : []
        id: moduleList
        Flow {
            Layout.fillWidth: true
            spacing: field.theme.spacing / 2
            ShellButton { text: "Add fixed space"; objectName: "field-" + field.spec.path + "-add-spacer"; onClicked: field.stage(moduleList.selected.concat(["@spacer"])) }
            ShellButton { text: "Add flexible space"; objectName: "field-" + field.spec.path + "-add-stretch"; onClicked: field.stage(moduleList.selected.concat(["@stretch"])) }
        }
        Repeater {
            model: moduleList.visible ? moduleList.selected.concat(Object.keys(field.host.draft.modules).concat(["@settings"]).filter(n => moduleList.selected.indexOf(n) < 0)) : []
            RowLayout {
                required property string modelData
                required property int index
                Layout.fillWidth: true
                Switch {
                    palette.dark: field.theme.palette.accent
                    objectName: "panel-module-" + modelData
                    Layout.fillWidth: true
                    text: field.moduleTitle(modelData)
                    checked: index < moduleList.selected.length
                    onClicked: { const next = moduleList.selected.slice(); if (checked) next.push(modelData); else next.splice(index, 1); field.stage(next) }
                }
                ShellButton {
                    objectName: "panel-module-" + modelData + "-up"
                    text: "↑"; Accessible.name: "Move " + field.moduleTitle(modelData) + " up"
                    enabled: index > 0 && index < moduleList.selected.length
                    onClicked: { const next = moduleList.selected.slice(); const name = next.splice(index, 1)[0]; next.splice(index - 1, 0, name); field.stage(next) }
                }
                ShellButton {
                    objectName: "panel-module-" + modelData + "-down"
                    text: "↓"; Accessible.name: "Move " + field.moduleTitle(modelData) + " down"
                    enabled: index < moduleList.selected.length - 1
                    onClicked: { const next = moduleList.selected.slice(); const name = next.splice(index, 1)[0]; next.splice(index + 1, 0, name); field.stage(next) }
                }
            }
        }
    }
}

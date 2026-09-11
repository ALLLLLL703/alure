import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Ui.js" as Ui

Item {
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
    property string commandDraft: spec.kind === "argv" ? Config.commandText(spec.value) : ""
    // The form owns width; only content height flows back to its layout.
    // Relative control widths must not feed the parent's implicit width.
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredWidth: 0
    implicitWidth: 0
    implicitHeight: row.y + row.height
    function stage(value) { host.stageField(spec.path, Ui.literal(value)) }
    InfoText {
        id: heading
        visible: field.spec.showGroup
        width: field.width
        text: field.spec.group; color: field.theme.palette.accent; font.bold: true
    }
    Item {
        id: row
        width: field.width
        y: heading.visible ? heading.implicitHeight + field.theme.spacing / 2 : 0
        height: Math.max(labels.implicitHeight, controls.implicitHeight)
        ColumnLayout {
            id: labels
            width: Math.max(0, row.width - controls.width - field.theme.spacing)
            anchors.verticalCenter: parent.verticalCenter
            InfoText { text: field.spec.label; font.bold: true; Layout.fillWidth: true }
            InfoText { visible: text.length > 0; text: field.spec.help; color: field.theme.palette.muted; Layout.fillWidth: true }
        }
        ColumnLayout {
            id: controls
            width: field.width * 0.56
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Switch {
                palette.dark: field.theme.palette.accent
                visible: field.spec.kind === "boolean"
                objectName: "field-" + field.spec.path + "-switch"
                Layout.alignment: Qt.AlignRight
                checked: !!field.value
                onClicked: field.stage(checked)
                Accessible.name: field.spec.label
            }
            ComboBox {
                visible: ["enum", "font", "theme"].indexOf(field.spec.kind) >= 0
                objectName: "field-" + field.spec.path + "-choice"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: field.spec.kind === "font" ? Qt.fontFamilies() : field.spec.kind === "theme" ? Config.themeNames : field.spec.options
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
                    Layout.minimumWidth: 0
                    from: field.spec.low; to: field.spec.high; stepSize: field.spec.step
                    value: Number(field.value)
                    onMoved: field.stage(Number(value.toFixed(field.spec.step < 1 ? 2 : 0)))
                    Accessible.name: field.spec.label
                }
                TextField {
                    objectName: "field-" + field.spec.path + "-number"
                    Layout.preferredWidth: Math.min(field.width * 0.18, field.theme.font_size * 6)
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
                TextField {
                    objectName: "field-" + field.spec.path + "-text"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: field.spec.kind === "argv" ? field.commandDraft : String(field.value)
                    selectByMouse: true
                    placeholderText: field.spec.kind === "color" ? "Inherit" : field.spec.kind === "argv" ? "command --argument 'quoted value'" : ""
                    onTextEdited: {
                        if (field.spec.kind === "argv") { field.commandDraft = text; field.host.stageCommand(field.spec.path, text) }
                        else field.stage(text)
                    }
                    onAccepted: field.host.flushFields()
                    Accessible.name: field.spec.label
                }
                ShellButton {
                    visible: field.spec.kind === "color"
                    objectName: "field-" + field.spec.path + "-color"
                    text: "Color…"
                    baseColor: Config.validColor(String(field.value)) ? field.value || "transparent" : "transparent"
                    onClicked: field.host.pickColor(field.spec.path, String(field.value))
                }
            }
        }
    }
}

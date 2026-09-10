import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Ui.js" as Ui

ApplicationWindow {
    id: window
    readonly property var theme: Config.model.theme
    property string savedText: Config.source
    property var inspection: Config.inspectText(Config.source)
    readonly property var draft: inspection.model
    property var pendingFields: ({})
    readonly property bool dirty: editor.text !== savedText || Object.keys(pendingFields).length > 0
    property string statusText: "Forms edit the same TOML draft. Preview changes this window only; Save & apply publishes to watching panels."
    property int section: 0
    property int panelIndex: 0
    property string moduleName: "workspaces"
    property string pendingAction: ""
    property string panelOperation: ""
    property bool closingApproved: false
    readonly property var sectionNames: ["Appearance", "Panels", "Modules", "Integrations", "Configuration"]
    readonly property var formFields: {
        if (inspection.error) return []
        if (section === 0) return Ui.fields(draft.theme, "theme").concat(Ui.fields(draft.ui, "ui"))
        if (section === 1) return (draft.panels && draft.panels[panelIndex]) ? Ui.fields(draft.panels[panelIndex], "panels." + panelIndex) : []
        if (section === 2 && draft.modules && draft.modules[moduleName]) return [{path: "modules." + moduleName + ".enabled", value: draft.modules[moduleName].enabled, literal: String(draft.modules[moduleName].enabled), boolean: true}].concat(Ui.fields(draft.modules[moduleName].style, "modules." + moduleName + ".style"))
        if (section === 3 && draft.modules && draft.modules[moduleName]) return Ui.fields(draft.modules[moduleName].behavior, "modules." + moduleName + ".behavior")
        return []
    }
    function inspect() { inspection = Config.inspectText(editor.text) }
    function flushFields() {
        let text = editor.text
        const paths = Object.keys(pendingFields)
        for (let i = 0; i < paths.length; ++i) {
            const result = Config.editLiteral(text, paths[i], pendingFields[paths[i]])
            if (result.error) { statusText = paths[i] + ": " + result.error; return false }
            text = result.text
        }
        pendingFields = ({})
        if (paths.length) { editor.text = text; inspect() }
        return true
    }
    function editField(path, literal) {
        const result = Config.editLiteral(editor.text, path, literal)
        if (result.error) { statusText = result.error; return false }
        const pending = Object.assign({}, pendingFields); delete pending[path]; pendingFields = pending
        editor.text = result.text; inspect(); statusText = "Draft updated · " + path + ". Save to publish."
        return true
    }
    function save() {
        if (!flushFields()) return false
        if (!Config.saveText(editor.text)) return false
        savedText = Config.source; inspect()
        statusText = "Saved atomically. Watching panels reload; runtime.watch=false requires panel restart."
        return true
    }
    function reload() {
        pendingFields = ({})
        const valid = Config.reload(); editor.text = Config.source; savedText = Config.source; inspect()
        statusText = valid ? "Reloaded disk configuration." : "Invalid disk text loaded for repair; runtime retains last valid model."
    }
    function finishAction() {
        if (pendingAction === "close") {
            // Close only after the modal has left the popup stack. Never recurse in onClosing.
            closingApproved = true
            Qt.callLater(function() { window.close() })
        } else reload()
    }
    function confirmDiscard(action) {
        if (!dirty) { if (action === "close") { closingApproved = true; close() } else reload(); return }
        pendingAction = action; unsaved.open()
    }
    width: Config.model.settings.width
    height: Config.model.settings.height
    minimumWidth: 400
    minimumHeight: 300
    visible: true
    title: "Alure Settings" + (dirty ? " *" : "")
    color: theme.palette.background
    font.family: theme.font
    font.pixelSize: theme.font_size
    palette.window: theme.palette.background
    palette.base: theme.palette.surface
    palette.text: theme.palette.foreground
    palette.windowText: theme.palette.foreground
    palette.button: theme.palette.surface
    palette.buttonText: theme.palette.foreground
    palette.highlight: theme.palette.accent
    onClosing: event => { if (dirty && !closingApproved) { event.accepted = false; confirmDiscard("close") } }
    Shortcut { sequence: Config.model.settings.close_shortcut; enabled: sequence.length > 0 && !unsaved.visible; onActivated: window.close() }
    Timer { id: parseDelay; interval: Config.model.runtime.reload_delay_ms; onTriggered: window.inspect() }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.theme.padding
        spacing: window.theme.spacing
        RowLayout {
            Image { source: "image://icons/builtin/alure"; sourceSize: Qt.size(window.theme.icon_size * 2, window.theme.icon_size * 2); Layout.preferredWidth: window.theme.icon_size * 2; Layout.preferredHeight: window.theme.icon_size * 2 }
            ColumnLayout {
                InfoText { text: "Alure"; font.bold: true; font.pixelSize: window.theme.font_size * 1.6; color: window.theme.palette.accent }
                InfoText { text: Config.path; elide: Text.ElideMiddle; maximumLineCount: 1; Layout.fillWidth: true; color: window.theme.palette.muted }
                Layout.fillWidth: true
            }
            InfoText { text: window.dirty ? "Unsaved draft" : "Saved"; color: window.theme.palette.muted }
            ShellButton { objectName: "settings-close"; text: "Close"; visible: Config.model.settings.show_close_button; onClicked: window.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: window.theme.spacing
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Math.min(window.width * 0.22, window.theme.font_size * 12)
                Repeater {
                    model: window.sectionNames
                    ShellButton {
                        required property int index
                        required property string modelData
                        text: modelData
                        objectName: "settings-section-" + index
                        accent: window.section === index
                        Layout.fillWidth: true
                        onClicked: { if (window.flushFields()) { window.inspect(); window.section = index } }
                    }
                }
                Item { Layout.fillHeight: true }
                InfoText { text: "v0.1 · TOML first\nNo services run in settings."; color: window.theme.palette.muted; Layout.fillWidth: true }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                InfoText { text: window.sectionNames[window.section]; font.bold: true; font.pixelSize: window.theme.font_size * 1.3 }
                InfoText {
                    visible: window.section !== 4
                    text: window.section === 0 ? "midnight / dawn / forest · dimensions are logical pixels. Strings use TOML quotes; arrays set order. Empty color overrides inherit the theme." : window.section === 1 ? "Select a panel to edit its fields. modules is its ordered TOML array. Inline/quoted tables may require the raw editor. Explicit margins prevent corner conflicts." : window.section === 2 ? "Enable modules and adjust icon, text, widths and color overrides. Vertical bars use compact icons; workspace labels remain visible." : "Commands are argv arrays, never shell text. Enabling a provider may run reads on Save. Device and update actions remain explicit. Notification server is opt-in and never replaces another daemon."
                    color: window.theme.palette.muted
                    Layout.fillWidth: true
                }
                ComboBox {
                    visible: window.section === 2 || window.section === 3
                    model: Object.keys(window.draft.modules || {})
                    currentIndex: Math.max(0, model.indexOf(window.moduleName))
                    onActivated: { if (window.flushFields()) window.moduleName = currentText; else currentIndex = model.indexOf(window.moduleName) }
                    Layout.fillWidth: true
                    Accessible.name: "Module"
                }
                Flow {
                    visible: window.section === 1
                    Layout.fillWidth: true
                    spacing: window.theme.spacing / 2
                    ComboBox {
                        model: (window.draft.panels || []).map(p => p.id + " · " + p.edge + " · " + p.output)
                        currentIndex: window.panelIndex
                        width: Math.min(parent.width, window.theme.font_size * 20)
                        onActivated: { if (window.flushFields()) window.panelIndex = currentIndex; else currentIndex = window.panelIndex }
                        Accessible.name: "Panel"
                    }
                    Repeater {
                        model: ["add", "remove", "up", "down"]
                        ShellButton {
                            required property string modelData
                            text: modelData
                            onClicked: { if (window.flushFields()) { window.panelOperation = modelData; panelNotice.open() } }
                        }
                    }
                }
                ScrollView {
                    visible: window.section !== 4
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: window.theme.spacing
                        Repeater {
                            model: window.formFields
                            delegate: ColumnLayout {
                                id: field
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: window.theme.spacing / 2
                                InfoText { text: field.modelData.path; color: window.theme.palette.muted; Layout.fillWidth: true }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Switch {
                                        visible: field.modelData.boolean
                                        checked: !!field.modelData.value
                                        text: checked ? "Enabled" : "Disabled"
                                        onClicked: { if (!window.editField(field.modelData.path, String(checked))) checked = Qt.binding(() => !!field.modelData.value) }
                                        Accessible.name: field.modelData.path
                                    }
                                    TextField {
                                        id: valueEditor
                                        visible: !field.modelData.boolean
                                        text: window.pendingFields[field.modelData.path] !== undefined ? window.pendingFields[field.modelData.path] : field.modelData.literal
                                        onTextEdited: { const values = Object.assign({}, window.pendingFields); values[field.modelData.path] = text; window.pendingFields = values }
                                        Layout.fillWidth: true
                                        selectByMouse: true
                                        Accessible.name: field.modelData.path
                                        onAccepted: window.editField(field.modelData.path, text)
                                    }
                                    ShellButton {
                                        visible: !field.modelData.boolean
                                        text: "Set"
                                        Accessible.name: "Set " + field.modelData.path
                                        onClicked: window.editField(field.modelData.path, valueEditor.text)
                                    }
                                }
                            }
                        }
                    }
                }
                InfoText { visible: window.section === 4; text: "Complete TOML editor · all supported and unknown keys. Ctrl+A selects all; standard editor shortcuts apply. Reload asks before discarding changes."; color: window.theme.palette.muted; Layout.fillWidth: true }
                ScrollView {
                    visible: window.section === 4
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        id: editor
                        objectName: "settings-raw-editor"
                        text: Config.source
                        textFormat: TextEdit.PlainText
                        font.family: Config.model.settings.editor_font
                        font.pixelSize: Config.model.settings.editor_font_size
                        color: window.theme.palette.foreground
                        selectionColor: window.theme.palette.accent
                        wrapMode: TextEdit.NoWrap
                        selectByMouse: true
                        onTextChanged: parseDelay.restart()
                        background: Rectangle { color: window.theme.palette.surface; radius: window.theme.radius }
                    }
                }
            }
        }
        InfoText {
            text: window.inspection.error || Config.diagnostic || window.statusText
            color: window.inspection.error || Config.diagnostic ? window.theme.palette.accent : window.theme.palette.muted
            maximumLineCount: 4
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Flow {
            Layout.fillWidth: true
            spacing: window.theme.spacing
            ShellButton { text: "Reload…"; onClicked: window.confirmDiscard("reload") }
            ShellButton { text: "Preview draft"; onClicked: { if (!window.flushFields()) return; window.inspect(); if (Config.previewText(editor.text)) window.statusText = "Valid draft previewed in settings only. Save & apply updates the config and watching panels." } }
            ShellButton { text: "Save & apply"; accent: true; onClicked: window.save() }
        }
    }
    Dialog {
        id: unsaved
        anchors.centerIn: parent
        width: Math.min(window.width - window.theme.padding * 2, window.theme.font_size * 32)
        implicitHeight: Math.min(window.height - window.theme.padding * 2, window.theme.font_size * 20)
        title: "Unsaved configuration"
        modal: true
        objectName: "settings-unsaved"
        closePolicy: Popup.NoAutoClose
        property bool completed: false
        onOpened: completed = false
        onClosed: { if (completed) window.finishAction() }
        contentItem: InfoText { text: "Save this draft, discard it, or cancel. External file conflicts will stop Save without losing your draft.\n" + Config.diagnostic + "\n" + window.statusText }
        // Explicit buttons keep validation/conflict failures in the modal, unlike automatic AcceptRole.
        footer: RowLayout {
            ShellButton { objectName: "unsaved-save"; text: "Save"; onClicked: { if (window.save()) { unsaved.completed = true; unsaved.close() } } }
            ShellButton { objectName: "unsaved-discard"; text: "Discard"; onClicked: { unsaved.completed = true; unsaved.close() } }
            ShellButton { objectName: "unsaved-cancel"; text: "Cancel"; onClicked: unsaved.close() }
        }
    }
    Dialog {
        id: panelNotice
        anchors.centerIn: parent
        width: Math.min(window.width - window.theme.padding * 2, window.theme.font_size * 34)
        implicitHeight: Math.min(window.height - window.theme.padding * 2, window.theme.font_size * 20)
        title: "Change panel definitions?"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: InfoText { text: "This normalizes only panel sections in your current draft: panel comments and formatting can be removed. Unknown panel keys and nested values are preserved. Unrelated sections are unchanged. Nothing reaches disk until Save. Use the raw editor to retain panel comments." }
        onAccepted: {
            const result = Config.editPanels(editor.text, window.panelOperation, window.panelIndex)
            if (result.error) { window.statusText = result.error; return }
            editor.text = result.text; window.inspect()
            window.panelIndex = Math.max(0, Math.min(window.panelIndex, window.draft.panels.length - 1))
            window.statusText = "Panel draft changed; review Configuration, then Save to publish."
        }
    }
}

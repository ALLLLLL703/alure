import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import "Ui.js" as Ui
import "SettingsFields.js" as Fields

ApplicationWindow {
    id: window
    readonly property var theme: Config.model.theme
    property string savedText: Config.source
    property var inspection: Config.inspectText(Config.source)
    readonly property var draft: inspection.model
    property var pendingFields: ({})
    property var commandErrors: ({})
    readonly property Item dragLayer: Overlay.overlay
    readonly property bool dirty: editor.text !== savedText || Object.keys(pendingFields).length > 0 || Object.keys(commandErrors).length > 0
    property string statusText: "Forms edit the same TOML draft. Preview changes this window only; Save & apply publishes to watching panels."
    property int section: 0
    property int appearanceGroup: 0
    property int panelIndex: 0
    property string moduleName: "workspaces"
    property string pendingAction: ""
    property string panelOperation: ""
    property bool closingApproved: false
    readonly property var sectionNames: ["Appearance", "Panels", "", "", "Configuration"]
    readonly property string panelLayout: pendingFields["panels." + panelIndex + ".layout"] !== undefined ? JSON.parse(pendingFields["panels." + panelIndex + ".layout"]) : (draft.panels && draft.panels[panelIndex] ? draft.panels[panelIndex].layout : "linear")
    readonly property var formFields: {
        if (inspection.error) return []
        if (section === 0) return appearanceGroup === 0 ? Fields.fields(draft.theme, "theme") : appearanceGroup === 1 ? Fields.fields(draft.ui, "ui").filter(f => !f.path.startsWith("ui.toast.")) : Fields.fields(draft.settings, "settings")
        if (section === 1) return (draft.panels && draft.panels[panelIndex]) ? Fields.fields(draft.panels[panelIndex], "panels." + panelIndex).filter(f => !/\.modules(_left|_center|_right)?$/.test(f.path)) : []
        if (section === 2 && draft.modules && draft.modules[moduleName]) {
            const prefix = "modules." + moduleName
            const style = Fields.fields(draft.modules[moduleName].style, prefix + ".style")
            const behavior = Fields.fields(draft.modules[moduleName].behavior, prefix + ".behavior").filter(f =>
                (moduleName === "media" || !f.path.endsWith(".preferred_player")) &&
                (!f.path.endsWith(".command") || ["volume", "wifi", "updates"].indexOf(moduleName) >= 0))
            if (style.length) { style[0].group = "Appearance"; style[0].showGroup = true }
            if (behavior.length) { behavior[0].group = Ui.title(moduleName) + " · behavior & integration"; behavior[0].showGroup = true }
            return [Fields.describe(prefix + ".enabled", draft.modules[moduleName].enabled)].concat(style, behavior, moduleName === "notifications" ? Fields.fields(draft.ui.toast, "ui.toast") : [])
        }
        return []
    }
    function resetFormScroll() {
        Qt.callLater(function() { if (formScroll.contentItem) formScroll.contentItem.contentY = 0 })
    }
    onSectionChanged: resetFormScroll()
    onAppearanceGroupChanged: resetFormScroll()
    onModuleNameChanged: resetFormScroll()
    onPanelIndexChanged: resetFormScroll()
    function inspect() { parseDelay.stop(); inspection = Config.inspectText(editor.text) }
    function stageField(path, literal) {
        const values = Object.assign({}, pendingFields); values[path] = literal; pendingFields = values
    }
    function stageCommand(path, text) {
        const parsed = Config.commandArguments(text)
        const errors = Object.assign({}, commandErrors)
        if (parsed.error) { errors[path] = parsed.error; statusText = parsed.error }
        else { delete errors[path]; stageField(path, Ui.literal(JSON.parse(JSON.stringify(parsed.argv)))) }
        commandErrors = errors
    }
    function stageModuleList(path, next) {
        if (path.endsWith(".modules")) { stageField(path, Ui.literal(next)); return }
        const prefix = path.slice(0, path.lastIndexOf(".") + 1)
        const panel = draft.panels[Number(path.split(".")[1])]
        const values = Object.assign({}, pendingFields)
        const keys = ["modules_left", "modules_center", "modules_right"]
        const lists = keys.map(key => values[prefix + key] === undefined ? panel[key] : JSON.parse(values[prefix + key]))
        keys.forEach(key => { delete values[prefix + key] })
        // Remove a moved module from its old zone before inserting it into its new one.
        keys.forEach((key, index) => {
            if (prefix + key !== path) values[prefix + key] = Ui.literal(lists[index].filter(name => name === "@spacer" || name === "@stretch" || next.indexOf(name) < 0))
        })
        values[path] = Ui.literal(next)
        pendingFields = values
    }
    function pickColor(path, value) {
        colorPicker.fieldPath = path
        colorPicker.selectedColor = Config.validColor(value) ? value : theme.palette.accent
        colorPicker.open()
    }
    function flushFields() {
        const errors = Object.keys(commandErrors)
        if (errors.length) { statusText = errors[0] + ": " + commandErrors[errors[0]]; return false }
        let text = editor.text
        const paths = Object.keys(pendingFields)
        // Work on a local draft: clear all affected zones before restoring their
        // complete lists, so moves/swaps never fail on an intermediate duplicate.
        const zones = paths.filter(path => /\.modules_(left|center|right)$/.test(path))
        for (let i = 0; i < zones.length; ++i) {
            const result = Config.editLiteral(text, zones[i], "[]")
            if (result.error) { statusText = zones[i] + ": " + result.error; return false }
            text = result.text
        }
        for (let i = 0; i < paths.length; ++i) {
            const result = Config.editLiteral(text, paths[i], pendingFields[paths[i]])
            if (result.error) { statusText = paths[i] + ": " + result.error; return false }
            text = result.text
        }
        if (paths.length) { editor.text = text; inspect() }
        pendingFields = ({})
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
        pendingFields = ({}); commandErrors = ({})
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
    // Config.modelChanged also fires for theme-only previews. Do not reset a size
    // supplied by the user/compositor unless the configured dimension changes.
    readonly property int configuredWidth: Config.model.settings.width
    readonly property int configuredHeight: Config.model.settings.height
    width: configuredWidth
    height: configuredHeight
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
    palette.light: theme.palette.border
    palette.midlight: theme.palette.surface
    palette.mid: theme.palette.border
    palette.dark: theme.palette.background
    palette.brightText: theme.palette.foreground
    palette.shadow: theme.palette.background
    palette.alternateBase: theme.palette.surface
    palette.highlightedText: theme.palette.background
    palette.placeholderText: theme.palette.muted
    palette.disabled.buttonText: theme.palette.muted
    palette.disabled.text: theme.palette.muted
    palette.disabled.windowText: theme.palette.muted
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
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: window.theme.spacing
            ScrollView {
                Layout.preferredWidth: Math.min(window.width * 0.22, window.theme.font_size * 12)
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    Repeater {
                        model: [0, 1, 4]
                        ShellButton {
                            required property int modelData
                            text: window.sectionNames[modelData]
                            objectName: "settings-section-" + modelData
                            accent: window.section === modelData
                            Layout.fillWidth: true
                            onClicked: { if (window.flushFields()) { window.inspect(); window.section = modelData } }
                        }
                    }
                    InfoText { text: "MODULES"; color: window.theme.palette.muted; font.bold: true; Layout.fillWidth: true; Layout.topMargin: window.theme.spacing }
                    Repeater {
                        model: Object.keys(window.draft.modules || {})
                        ShellButton {
                            required property string modelData
                            text: Ui.title(modelData)
                            iconName: modelData
                            objectName: "settings-module-" + modelData
                            accent: window.section === 2 && window.moduleName === modelData
                            Layout.fillWidth: true
                            onClicked: { if (window.flushFields()) { window.inspect(); window.moduleName = modelData; window.section = 2 } }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                InfoText { text: window.section === 2 ? Ui.title(window.moduleName) : window.sectionNames[window.section]; font.bold: true; font.pixelSize: window.theme.font_size * 1.3 }
                InfoText {
                    visible: window.section !== 4
                    text: window.section === 0 ? "Theme, typography and shared interactions. Save & apply publishes your draft." : window.section === 1 ? "Drag blocks into the panel slots. Placing a module also enables it. Save & apply publishes the layout." : "Appearance, module-specific behavior and system integration in one place."
                    color: window.theme.palette.muted
                    Layout.fillWidth: true
                }
                Item {
                    visible: window.section === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 0
                    implicitHeight: Math.max(categoryLabel.implicitHeight, categoryChoice.implicitHeight)
                    InfoText { id: categoryLabel; text: "Category"; width: parent.width * 0.44 - window.theme.spacing; anchors.verticalCenter: parent.verticalCenter }
                    ComboBox {
                        id: categoryChoice
                        objectName: "appearance-group"
                        model: ["Theme & typography", "Layout & popups", "Settings window"]
                        currentIndex: window.appearanceGroup
                        onActivated: { if (window.flushFields()) window.appearanceGroup = currentIndex; else currentIndex = window.appearanceGroup }
                        width: parent.width * 0.56
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.name: "Appearance category"
                    }
                }
                // A hidden Flow cannot contain a parent-width selector: its
                // implicit width then feeds itself while the layout ignores it.
                ColumnLayout {
                    visible: window.section === 1
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 0
                    spacing: window.theme.spacing / 2
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(panelLabel.implicitHeight, panelChoice.implicitHeight)
                        InfoText { id: panelLabel; text: "Panel"; width: parent.width * 0.44 - window.theme.spacing; anchors.verticalCenter: parent.verticalCenter }
                        ComboBox {
                            id: panelChoice
                            model: (window.draft.panels || []).map(p => p.id + " · " + p.edge + " · " + p.output)
                            currentIndex: window.panelIndex
                            width: parent.width * 0.56
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            onActivated: { if (window.flushFields()) window.panelIndex = currentIndex; else currentIndex = window.panelIndex }
                            Accessible.name: "Panel"
                        }
                    }
                    InfoText {
                        objectName: "panel-gap-help"
                        text: Fields.panelGapHelp((window.draft.panels || [])[window.panelIndex])
                        Layout.fillWidth: true
                        color: window.theme.palette.muted
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: window.theme.spacing / 2
                        Repeater {
                            model: ["add", "remove", "up", "down"]
                            ShellButton {
                                required property string modelData
                                text: modelData
                                onClicked: { if (window.flushFields()) { window.panelOperation = modelData; panelNotice.open() } }
                            }
                        }
                    }
                }
                ScrollView {
                    id: formScroll
                    objectName: "settings-form-scroll"
                    visible: window.section !== 4
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: window.theme.spacing
                        Loader {
                            active: window.section === 1 && !window.inspection.error && !!window.draft.panels[window.panelIndex]
                            Layout.fillWidth: true
                            sourceComponent: ModuleSlots { host: window }
                        }
                        Repeater {
                            model: window.formFields
                            delegate: SettingField {
                                required property var modelData
                                spec: modelData
                                host: window
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
            ShellButton { objectName: "settings-reload"; text: "Reload…"; onClicked: window.confirmDiscard("reload") }
            ShellButton { objectName: "settings-apply-fields"; text: "Apply fields"; visible: window.section !== 4; onClicked: { if (window.flushFields()) window.statusText = "Fields applied to the draft. Preview or Save when ready." } }
            ShellButton { objectName: "settings-preview"; text: "Preview draft"; onClicked: { if (!window.flushFields()) return; window.inspect(); if (Config.previewText(editor.text)) window.statusText = "Valid draft previewed in settings only. Save & apply updates the config and watching panels." } }
            ShellButton { objectName: "settings-save"; text: "Save & apply"; accent: true; onClicked: window.save() }
        }
    }
    NativeDialogs.ColorDialog {
        id: colorPicker
        objectName: "settings-color-picker"
        property string fieldPath: ""
        title: "Choose color"
        options: NativeDialogs.ColorDialog.DontUseNativeDialog | NativeDialogs.ColorDialog.ShowAlphaChannel
        onAccepted: window.stageField(fieldPath, Ui.literal(String(selectedColor)))
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

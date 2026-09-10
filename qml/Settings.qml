import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    readonly property var theme: Config.model.theme
    property string savedText: Config.source
    property string statusText: "Edit TOML, then Save & apply. Reload discards editor changes."
    width: Config.model.settings.width
    height: Config.model.settings.height
    visible: true
    title: "Alure Settings" + (editor.text !== savedText ? " *" : "")
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
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.theme.padding
        spacing: window.theme.spacing
        Label {
            text: "Alure / Configuration"
            font.bold: true
            color: window.theme.palette.accent
        }
        Label {
            text: Config.path
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            color: window.theme.palette.muted
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: editor
                text: Config.source
                font.family: Config.model.settings.editor_font
                font.pixelSize: Config.model.settings.editor_font_size
                color: window.theme.palette.foreground
                selectionColor: window.theme.palette.accent
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                background: Rectangle { color: window.theme.palette.surface; radius: window.theme.radius }
            }
        }
        Label {
            text: Config.diagnostic.length > 0 ? Config.diagnostic : window.statusText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: window.theme.palette.accent
        }
        RowLayout {
            spacing: window.theme.spacing
            Button {
                text: "Reload (discard edits)"
                onClicked: {
                    const valid = Config.reload()
                    editor.text = Config.source
                    window.savedText = Config.source
                    window.statusText = valid ? "Reloaded." : "Invalid document loaded for repair; running panels keep the last valid model."
                }
            }
            Button {
                text: "Save & apply"
                onClicked: {
                    if (Config.saveText(editor.text)) {
                        window.savedText = Config.source
                        window.statusText = "Saved atomically. Running panels reload when runtime.watch is enabled."
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Label { text: "Foundation · TOML editor"; color: window.theme.palette.muted }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    readonly property var theme: Config.model.theme
    readonly property var options: Config.model.modules.clipboard.behavior
    readonly property var service: Services.clipboard
    readonly property bool viewActive: visible && !!Window.window && Window.window.visible
    readonly property var rows: service.items.filter(row => String(row.label).toLocaleLowerCase().includes(search.text.toLocaleLowerCase()))
    opacity: Config.model.modules.clipboard.style.opacity
    property string selectedId: ""
    property string confirmAction: ""
    property string statusText: ""
    onSelectedIdChanged: confirmAction = ""
    readonly property bool canAct: service.available && !service.busy && options.allow_actions
    function select(id) { selectedId = id; confirmAction = ""; service.previewItem(id) }
    function moveSelection(delta) {
        const index = Math.max(0, Math.min(rows.length - 1, history.currentIndex + delta))
        if (rows[index]) { select(rows[index].id); history.positionViewAtIndex(index, ListView.Contain) }
    }
    Shortcut { sequence: root.options.copy_shortcut; enabled: root.viewActive && root.canAct && !!root.selectedId && !root.confirmAction; onActivated: root.act("copy", false) }
    Shortcut { sequence: root.options.next_shortcut; enabled: root.viewActive && root.rows.length > 0; onActivated: root.moveSelection(1) }
    Shortcut { sequence: root.options.previous_shortcut; enabled: root.viewActive && root.rows.length > 0; onActivated: root.moveSelection(-1) }
    function syncSelection() {
        if (!viewActive || service.busy) return
        if (!rows.some(row => row.id === selectedId)) selectedId = rows.length ? rows[0].id : ""
        if (selectedId) service.previewItem(selectedId)
    }
    function act(name, confirmed) {
        if (!service.action(name, {id: selectedId, confirmed: !!confirmed})) statusText = "Request not accepted; refresh and check permissions."
        confirmAction = ""
    }
    onRowsChanged: Qt.callLater(root.syncSelection)
    onViewActiveChanged: {
        if (viewActive) { service.openView(); Qt.callLater(() => search.forceActiveFocus()) }
        else { service.closeView(); selectedId = ""; search.clear() }
    }
    Component.onDestruction: { if (viewActive) service.closeView() }
    Connections {
        target: root.service
        function onChanged() { Qt.callLater(root.syncSelection) }
        function onCopied() { root.statusText = "Copied"; if (root.options.close_on_copy) Shell.closePopup() }
    }
    padding: theme.padding
    font.family: theme.font
    font.pixelSize: theme.font_size
    palette.text: theme.palette.foreground
    palette.windowText: theme.palette.foreground
    palette.buttonText: theme.palette.foreground
    palette.base: theme.palette.surface
    palette.button: theme.palette.surface
    palette.highlight: theme.palette.accent
    palette.highlightedText: theme.palette.background
    Keys.onEscapePressed: event => { if (Config.model.ui.escape_closes) { Shell.closePopup(); event.accepted = true } }
    background: Rectangle { color: Qt.alpha(root.theme.palette.background, root.theme.opacity); radius: root.theme.radius; border.width: root.theme.border_width; border.color: root.theme.palette.border }
    contentItem: ColumnLayout {
        spacing: root.theme.spacing
        RowLayout {
            Layout.fillWidth: true
            InfoText { text: "Clipboard"; font.bold: true; font.pixelSize: root.theme.font_size * 1.3; Layout.fillWidth: true }
            ShellButton { iconName: "refresh"; Accessible.name: "Refresh clipboard history"; enabled: !root.service.busy; onClicked: root.service.refresh() }
        }
        TextField {
            id: search
            objectName: "clipboard-search"
            Layout.fillWidth: true
            placeholderText: "Search history previews…"
            maximumLength: 256
            Accessible.name: "Search clipboard history"
        }
        InfoText {
            Layout.fillWidth: true
            visible: !root.service.available || root.statusText.length > 0
            text: root.service.diagnostic || root.statusText
            color: root.theme.palette.muted
        }
        InfoText { visible: root.service.available && !root.rows.length; text: "No matching history entries"; color: root.theme.palette.muted; Layout.fillWidth: true }
        ListView {
            id: history
            objectName: "clipboard-history"
            Layout.fillWidth: true; Layout.fillHeight: true
            cacheBuffer: 0
            spacing: root.theme.spacing / 2
            clip: true
            model: root.rows
            currentIndex: root.rows.findIndex(row => row.id === root.selectedId)
            ScrollBar.vertical: ScrollBar { }
            delegate: ItemDelegate {
                id: entry
                required property var modelData
                required property int index
                objectName: "clipboard-entry-" + modelData.id
                width: history.width
                highlighted: root.selectedId === modelData.id
                hoverEnabled: true
                readonly property var preview: root.service.previews[modelData.id] || ({})
                Component.onCompleted: Qt.callLater(function() { root.service.previewItem(entry.modelData.id) })
                contentItem: ColumnLayout {
                    spacing: root.theme.spacing / 2
                    Image {
                        objectName: "clipboard-image-" + entry.modelData.id
                        visible: entry.preview.kind === "image"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? root.options.inline_image_height : 0
                        source: visible ? entry.preview.imageUrl || "" : ""
                        asynchronous: false; cache: false
                        fillMode: Image.PreserveAspectFit
                        Accessible.name: "Clipboard image"
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: entry.preview.kind !== "image"
                        text: entry.highlighted && entry.preview.kind === "text" ? entry.preview.text : entry.modelData.label
                        textFormat: Text.PlainText
                        font: root.font
                        wrapMode: Text.Wrap
                        maximumLineCount: entry.highlighted ? root.options.inline_text_lines : 2
                        elide: Text.ElideRight
                        color: entry.highlighted ? root.theme.palette.background : root.theme.palette.foreground
                    }
                }
                background: Rectangle { color: entry.highlighted ? root.theme.palette.accent : entry.hovered ? root.theme.palette.surface : "transparent"; radius: root.theme.radius / 2 }
                onClicked: root.select(modelData.id)
                onDoubleClicked: { if (root.canAct) root.act("copy", false) }
            }
        }
        RowLayout {
            ShellButton { objectName: "clipboard-copy"; text: "Copy"; enabled: root.canAct && !!root.selectedId; onClicked: root.act("copy", false) }
            ShellButton { objectName: "clipboard-delete"; text: "Delete"; visible: root.options.allow_delete; enabled: root.canAct && !!root.selectedId; onClicked: { if (root.options.confirm_delete) root.confirmAction = "delete"; else root.act("delete", false) } }
            Item { Layout.fillWidth: true }
            ShellButton { objectName: "clipboard-clear"; text: "Clear history…"; visible: root.options.allow_delete; enabled: root.canAct && root.service.items.length > 0; onClicked: root.confirmAction = "wipe" }
        }
        RowLayout {
            visible: root.confirmAction.length > 0
            Layout.fillWidth: true
            InfoText { text: root.confirmAction === "wipe" ? "Delete ALL history?" : "Delete this entry?"; Layout.fillWidth: true }
            ShellButton { objectName: "clipboard-confirm"; text: "Delete"; enabled: root.canAct; onClicked: root.act(root.confirmAction, true) }
            ShellButton { text: "Cancel"; onClicked: root.confirmAction = "" }
        }
    }
}

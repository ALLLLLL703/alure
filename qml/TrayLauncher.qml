import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Alure 1.0

Control {
    id: root
    objectName: "tray-launcher"
    readonly property var theme: Config.model.theme
    readonly property var module: Config.model.modules.tray_launcher
    readonly property var options: module.behavior
    readonly property var menu: Tray.menu
    property string appId: ""
    property string appTitle: ""
    property string selectedId: ""
    property string statusText: ""
    readonly property bool registryPage: appId.length === 0
    readonly property bool busy: registryPage ? (Tray.busy && !Tray.available) : menu.loading
    readonly property string diagnostic: registryPage ? Tray.diagnostic : menu.error || (!Tray.available ? Tray.diagnostic : "")
    readonly property var rows: {
        const query = options.search_case_sensitive ? search.text : search.text.toLocaleLowerCase()
        const source = registryPage ? Tray.items : menu.items
        return source.filter(row => {
            const label = registryPage ? String(row.Title || row.id) : String(row.label || "").replace(/_(.)/g, "$1")
            return !query || (options.search_case_sensitive ? label : label.toLocaleLowerCase()).includes(query)
        })
    }
    function selectable(row) { return registryPage || (row.enabled && row.type !== "separator") }
    function syncSelection() {
        if (!rows.some(row => String(row.id) === selectedId && selectable(row))) {
            const first = rows.find(row => selectable(row))
            selectedId = first ? String(first.id) : ""
        }
    }
    function pageChanged() {
        if (options.reset_search_on_page) search.clear()
        selectedId = ""
        statusText = ""
        Qt.callLater(syncSelection)
        search.forceActiveFocus()
    }
    function moveSelection(delta) {
        let index = rows.findIndex(row => String(row.id) === selectedId)
        for (let i = index + delta; i >= 0 && i < rows.length; i += delta) {
            if (selectable(rows[i])) {
                selectedId = String(rows[i].id)
                entries.positionViewAtIndex(i, ListView.Contain)
                break
            }
        }
        search.forceActiveFocus()
    }
    function choose(id) {
        if (busy || !Tray.available) return
        const row = rows.find(row => String(row.id) === id)
        if (!row || !selectable(row)) return
        selectedId = id
        if (registryPage) {
            appId = id; appTitle = String(row.Title || id)
            Tray.openMenu(id)
            pageChanged()
        } else {
            if (!row.submenu && !options.allow_actions) { statusText = "Actions are disabled by configuration"; return }
            const submenu = !!row.submenu
            if (!menu.select(Number(id))) statusText = "Selection refused; refresh or go Back"
            else if (submenu) pageChanged()
        }
        search.forceActiveFocus()
    }
    function back() {
        if (registryPage) return
        if (menu.canGoBack) menu.back()
        else { menu.close(); appId = ""; appTitle = ""; Tray.refresh() }
        pageChanged()
    }
    onRowsChanged: Qt.callLater(syncSelection)
    Component.onCompleted: { syncSelection(); search.forceActiveFocus() }
    Connections {
        target: root.menu
        function onActivated() {
            if (root.options.close_on_activate) Shell.closePopup()
            else {
                // Reopen the current app after acknowledgement; providers may alter the menu.
                root.statusText = "Action acknowledged"
                Tray.openMenu(root.appId)
                root.pageChanged()
            }
        }
    }
    Shortcut { sequence: root.options.next_shortcut; onActivated: root.moveSelection(1) }
    Shortcut { sequence: root.options.previous_shortcut; onActivated: root.moveSelection(-1) }
    Shortcut { sequence: root.options.activate_shortcut; onActivated: root.choose(root.selectedId) }
    Shortcut { sequence: root.options.back_shortcut; onActivated: root.back() }
    Shortcut { sequence: root.options.close_shortcut; onActivated: { if (root.registryPage) Shell.closePopup(); else root.back() } }
    padding: theme.padding
    opacity: module.style.opacity
    font.family: theme.font
    font.pixelSize: theme.font_size
    palette.text: module.style.foreground || theme.palette.foreground
    palette.windowText: module.style.foreground || theme.palette.foreground
    palette.buttonText: module.style.foreground || theme.palette.foreground
    palette.base: theme.palette.surface
    palette.button: theme.palette.surface
    palette.highlight: theme.palette.accent
    palette.highlightedText: theme.palette.background
    background: Rectangle {
        objectName: "tray-launcher-background"
        color: Qt.alpha(root.module.style.background || root.theme.palette.background, root.theme.opacity)
        radius: root.theme.radius
        border.width: root.theme.border_width
        border.color: root.theme.palette.border
        BackgroundBlur { anchors.fill: parent; radius: parent.radius; blurEnabled: root.theme.blur_enabled }
    }
    contentItem: ColumnLayout {
        spacing: root.theme.spacing
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "tray-launcher-back"; text: "Back"; enabled: !root.registryPage; focusPolicy: Qt.NoFocus; onClicked: root.back() }
            Label { Layout.fillWidth: true; text: root.registryPage ? "Tray apps" : root.appTitle; elide: Text.ElideRight }
            Button { text: "Apps"; visible: !root.registryPage; focusPolicy: Qt.NoFocus; onClicked: { root.menu.close(); root.appId = ""; root.pageChanged(); Tray.refresh() } }
            Button { objectName: "tray-launcher-refresh"; text: "Refresh"; enabled: !root.busy; focusPolicy: Qt.NoFocus; onClicked: { if (root.registryPage) Tray.refresh(); else { Tray.openMenu(root.appId); root.pageChanged() } } }
            Button { text: "Close"; focusPolicy: Qt.NoFocus; onClicked: Shell.closePopup() }
        }
        TextField {
            id: search
            objectName: "tray-launcher-search"
            Layout.fillWidth: true
            placeholderText: root.registryPage ? "Search current tray apps" : "Search this menu page"
            selectByMouse: true
            focus: true
        }
        Label {
            objectName: "tray-launcher-status"
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            text: root.diagnostic || root.statusText || (root.busy ? "Loading…" : !root.rows.length ? (search.text ? "No matching entries" : root.registryPage ? "No tray apps registered" : "No menu entries; use Back or Refresh") : "")
        }
        ListView {
            id: entries
            objectName: "tray-launcher-entries"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            currentIndex: root.rows.findIndex(row => String(row.id) === root.selectedId)
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: entry
                required property var modelData
                width: entries.width
                objectName: "tray-launcher-entry-" + modelData.id
                height: modelData.type === "separator" ? Math.max(8, root.theme.spacing) : Math.max(Config.model.ui.module_height, implicitHeight)
                enabled: root.selectable(modelData) && !root.busy
                highlighted: String(modelData.id) === root.selectedId
                focusPolicy: Qt.NoFocus
                text: root.registryPage ? String(modelData.Title || modelData.id) : (modelData["toggle-type"] ? (modelData["toggle-state"] === 1 ? "● " : modelData["toggle-state"] === -1 ? "− " : "○ ") : "") + String(modelData.label || "").replace(/_(.)/g, "$1") + (modelData.submenu ? "  ›" : "")
                background: Rectangle {
                    objectName: "tray-launcher-row-background-" + entry.modelData.id
                    radius: Math.min(root.theme.radius, height / 2)
                    color: entry.highlighted || entry.hovered ? Qt.alpha(root.theme.palette.accent, 0.16) : "transparent"
                    border.width: entry.highlighted ? root.theme.border_width : 0
                    border.color: root.theme.palette.accent
                }
                contentItem: Text {
                    objectName: "tray-launcher-row-label-" + entry.modelData.id
                    text: entry.text
                    textFormat: Text.PlainText
                    font: entry.font
                    color: root.module.style.foreground || root.theme.palette.foreground
                    opacity: entry.enabled ? 1 : 0.45
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Accessible.name: text
                Accessible.description: !root.registryPage && modelData["toggle-type"] ? modelData["toggle-type"] + ": " + modelData["toggle-state"] : ""
                Rectangle { anchors.centerIn: parent; width: parent.width; height: root.theme.border_width; visible: entry.modelData.type === "separator"; color: root.theme.palette.border }
                onClicked: root.choose(String(modelData.id))
            }
        }
    }
}

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
    readonly property color foreground: module.style.foreground || theme.palette.foreground
    component ToolbarButton: ShellButton {
        id: button
        required property string actionLabel
        focusPolicy: Qt.NoFocus
        foreground: root.foreground
        Accessible.name: actionLabel
        accessibleDescription: actionLabel
        ToolTip.text: actionLabel
        ToolTip.delay: Config.model.settings.module_tooltip_delay_ms
        ToolTip.visible: Config.model.settings.module_tooltips && button.visible && button.enabled && button.hovered && !button.down
    }
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
    function entryIcon(row) {
        if (registryPage) {
            const name = row.Status === "NeedsAttention" ? row.AttentionIconName || row.IconName : row.IconName
            return row.iconUrl || "image://icons/theme/" + (name || module.style.icon)
        }
        if (row["toggle-type"])
            return "image://icons/builtin/" + (row["toggle-state"] === 1 ? "check" : row["toggle-state"] === -1 ? "minus" : "circle")
        return "image://icons/theme/" + row["icon-name"]
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
    function moveSelection(delta, wrap) {
        const selectableIndexes = []
        for (let i = 0; i < rows.length; ++i)
            if (selectable(rows[i])) selectableIndexes.push(i)
        if (!selectableIndexes.length) {
            selectedId = ""
            search.forceActiveFocus()
            return
        }
        const currentIndex = rows.findIndex(row => String(row.id) === selectedId)
        let position = selectableIndexes.indexOf(currentIndex)
        if (position < 0)
            position = delta > 0 ? 0 : selectableIndexes.length - 1
        else {
            position += delta
            if (wrap)
                position = (position + selectableIndexes.length) % selectableIndexes.length
            else
                position = Math.max(0, Math.min(selectableIndexes.length - 1, position))
        }
        const index = selectableIndexes[position]
        selectedId = String(rows[index].id)
        entries.positionViewAtIndex(index, ListView.Contain)
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
    Shortcut { sequence: root.options.next_shortcut; onActivated: root.moveSelection(1, false) }
    Shortcut { sequence: root.options.previous_shortcut; onActivated: root.moveSelection(-1, false) }
    Shortcut { sequence: root.options.tab_shortcut; onActivated: root.moveSelection(1, true) }
    Shortcut { sequence: root.options.reverse_tab_shortcut; onActivated: root.moveSelection(-1, true) }
    Shortcut { sequence: root.options.activate_shortcut; onActivated: root.choose(root.selectedId) }
    Shortcut {
        sequence: root.options.refresh_shortcut
        onActivated: {
            if (root.registryPage) Tray.refresh()
            else { Tray.openMenu(root.appId); root.pageChanged() }
        }
    }
    Shortcut { sequence: root.options.back_shortcut; onActivated: root.back() }
    Shortcut { sequence: root.options.close_shortcut; onActivated: { if (root.registryPage) Shell.closePopup(); else root.back() } }
    padding: theme.padding
    opacity: module.style.opacity
    font.family: theme.font
    font.pixelSize: theme.font_size
    palette.text: root.foreground
    palette.windowText: root.foreground
    palette.buttonText: root.foreground
    palette.base: theme.palette.surface
    palette.button: theme.palette.surface
    palette.highlight: theme.palette.accent
    palette.highlightedText: theme.palette.background
    background: Rectangle {
        objectName: "tray-launcher-background"
        color: Qt.alpha(root.module.style.background || root.theme.palette.background, root.theme.opacity)
        radius: root.theme.radius
        border.width: root.theme.border_width
        border.color: Qt.alpha(root.theme.palette.border, root.theme.opacity)
        BackgroundBlur { anchors.fill: parent; radius: parent.radius; blurEnabled: root.theme.blur_enabled }
    }
    contentItem: ColumnLayout {
        spacing: root.theme.spacing
        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.spacing / 2
            ToolbarButton {
                objectName: "tray-launcher-back"
                iconName: "previous"
                actionLabel: "Back"
                visible: !root.registryPage
                onClicked: root.back()
            }
            Image {
                visible: root.registryPage && root.module.style.show_icon
                Layout.preferredWidth: Math.min(root.module.style.icon_size, Config.model.ui.module_height)
                Layout.preferredHeight: Layout.preferredWidth
                sourceSize: Qt.size(width, height)
                source: "image://icons/" + root.theme.icon_mode + "/" + root.module.style.icon
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.registryPage ? "Tray apps" : root.appTitle
                textFormat: Text.PlainText
                color: root.foreground
                elide: Text.ElideRight
            }
            ToolbarButton {
                objectName: "tray-launcher-apps"
                iconName: root.module.style.icon
                actionLabel: "All tray apps"
                visible: !root.registryPage
                onClicked: { root.menu.close(); root.appId = ""; root.pageChanged(); Tray.refresh() }
            }
            ToolbarButton {
                objectName: "tray-launcher-refresh"
                iconName: "refresh"
                actionLabel: "Refresh"
                enabled: !root.busy
                onClicked: { if (root.registryPage) Tray.refresh(); else { Tray.openMenu(root.appId); root.pageChanged() } }
            }
            ToolbarButton {
                objectName: "tray-launcher-close"
                iconName: "close"
                actionLabel: "Close"
                onClicked: Shell.closePopup()
            }
        }
        TextField {
            id: search
            objectName: "tray-launcher-search"
            Layout.fillWidth: true
            implicitHeight: Math.max(Config.model.ui.module_height, contentHeight + topPadding + bottomPadding)
            padding: Config.model.ui.panel_padding
            leftPadding: root.theme.spacing
            rightPadding: root.theme.spacing
            color: root.foreground
            placeholderTextColor: root.theme.palette.muted
            selectionColor: root.theme.palette.accent
            selectedTextColor: root.theme.palette.background
            placeholderText: root.registryPage ? "Search current tray apps" : "Search this menu page"
            selectByMouse: true
            focus: true
            background: Rectangle {
                objectName: "tray-launcher-search-background"
                radius: Math.min(root.theme.radius, height / 2)
                color: Qt.alpha(root.theme.palette.surface, root.theme.opacity)
                border.width: root.theme.border_width
                border.color: search.activeFocus ? root.theme.palette.accent : root.theme.palette.border
                Behavior on border.color { ColorAnimation { duration: Config.model.ui.animation_ms } }
            }
        }
        Label {
            objectName: "tray-launcher-status"
            Layout.fillWidth: true
            visible: text.length > 0
            color: root.diagnostic ? root.theme.palette.accent : root.theme.palette.muted
            textFormat: Text.PlainText
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
            delegate: ShellButton {
                id: entry
                required property var modelData
                width: entries.width
                objectName: "tray-launcher-entry-" + modelData.id
                height: modelData.type === "separator" ? root.theme.spacing : Math.max(Config.model.ui.module_height, implicitContentHeight + topPadding + bottomPadding)
                enabled: root.selectable(modelData) && !root.busy
                accent: String(modelData.id) === root.selectedId
                focusPolicy: Qt.NoFocus
                text: root.registryPage ? String(modelData.Title || modelData.id) : String(modelData.label || "").replace(/_(.)/g, "$1")
                contentItem: RowLayout {
                    spacing: root.theme.spacing
                    opacity: entry.enabled ? 1 : 0.45
                    Image {
                        objectName: "tray-launcher-row-icon-" + entry.modelData.id
                        readonly property bool toggle: !root.registryPage && !!entry.modelData["toggle-type"]
                        visible: toggle || (root.module.style.show_icon && (root.registryPage || !!entry.modelData["icon-name"]))
                        Layout.preferredWidth: root.module.style.icon_size
                        Layout.preferredHeight: root.module.style.icon_size
                        sourceSize: Qt.size(width, height)
                        source: visible ? root.entryIcon(entry.modelData) : ""
                    }
                    Text {
                        objectName: "tray-launcher-row-label-" + entry.modelData.id
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: entry.text
                        textFormat: Text.PlainText
                        font: entry.font
                        color: root.foreground
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    Image {
                        visible: !!entry.modelData.submenu
                        Layout.preferredWidth: root.module.style.icon_size
                        Layout.preferredHeight: root.module.style.icon_size
                        sourceSize: Qt.size(width, height)
                        source: visible ? "image://icons/" + root.theme.icon_mode + "/next" : ""
                    }
                }
                Accessible.name: text
                Accessible.description: !root.registryPage && modelData["toggle-type"] ? modelData["toggle-type"] + ": " + modelData["toggle-state"] : ""
                Rectangle { anchors.centerIn: parent; width: parent.width; height: root.theme.border_width; visible: entry.modelData.type === "separator"; color: root.theme.palette.border }
                onClicked: root.choose(String(modelData.id))
            }
        }
    }
}

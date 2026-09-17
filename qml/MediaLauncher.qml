import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Alure 1.0

Control {
    id: root
    objectName: "media-launcher"
    readonly property var theme: Config.model.theme
    readonly property var module: Config.model.modules.media_launcher
    readonly property var options: module.behavior
    readonly property var service: Media
    readonly property var players: service.items
    readonly property color foreground: module.style.foreground || theme.palette.foreground
    property string selectedId: ""
    property string selectedService: ""
    property string statusText: ""
    property string actionError: ""
    property var queuedAction: null
    readonly property bool registryPage: selectedService.length === 0
    readonly property var player: players.find(row => String(row.service) === selectedService) || ({})
    readonly property bool controllable: !registryPage && options.allow_actions && !!player.CanControl
    readonly property bool viewActive: visible && !!Window.window && Window.window.visible
    readonly property var rows: {
        const query = options.search_case_sensitive ? search.text : search.text.toLocaleLowerCase()
        return players.filter(row => {
            const label = String(row.identity || row.service) + " " + String(row.title || "")
            return !query || (options.search_case_sensitive ? label : label.toLocaleLowerCase()).includes(query)
        })
    }
    readonly property real controlExtent: Math.min(options.control_size, Math.max(16, (availableWidth - 6 * theme.spacing) / 5.4))

    component ToolbarButton: ShellButton {
        id: button
        required property string actionLabel
        foreground: root.foreground
        Accessible.name: actionLabel
        accessibleDescription: actionLabel
        ToolTip.text: actionLabel
        ToolTip.delay: Config.model.settings.module_tooltip_delay_ms
        ToolTip.visible: Config.model.settings.module_tooltips && button.visible && button.enabled && button.hovered && !button.down
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: event => root.handleDetailKey(event)
        Keys.onReturnPressed: event => { button.click(); event.accepted = true }
        Keys.onEnterPressed: event => { button.click(); event.accepted = true }
    }

    function handleDetailKey(event) {
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
            moveDetailFocus(event.key === Qt.Key_Backtab || !!(event.modifiers & Qt.ShiftModifier), event)
    }
    function moveDetailFocusFromHost(reverse) { moveDetailFocus(reverse, null) }
    function moveDetailFocus(reverse, event) {
        if (registryPage) return
        const order = [backButton, refreshButton, closeButton, progress, shuffleButton, previousButton, playButton, nextButton, repeatButton]
                .filter(item => item.visible && item.enabled && item.activeFocusOnTab)
        const current = order.findIndex(item => item.activeFocus)
        const next = current < 0 ? 0 : (current + (reverse ? -1 : 1) + order.length) % order.length
        if (order.length) order[next].forceActiveFocus(reverse ? Qt.BacktabFocusReason : Qt.TabFocusReason)
        if (event) event.accepted = true
    }
    function syncSelection() {
        if (!registryPage) return
        if (!rows.some(row => String(row.service) === selectedId))
            selectedId = rows.length ? String(rows[0].service) : ""
    }
    function moveSelection(delta, wrap) {
        if (!registryPage || !rows.length) {
            selectedId = ""
            if (registryPage) search.forceActiveFocus()
            return
        }
        let index = rows.findIndex(row => String(row.service) === selectedId)
        if (index < 0) index = delta > 0 ? 0 : rows.length - 1
        else if (wrap) index = (index + delta + rows.length) % rows.length
        else index = Math.max(0, Math.min(rows.length - 1, index + delta))
        selectedId = String(rows[index].service)
        entries.positionViewAtIndex(index, ListView.Contain)
        search.forceActiveFocus()
    }
    function choose(serviceName) {
        if (!registryPage || service.busy && !service.available) return
        const row = rows.find(candidate => String(candidate.service) === serviceName)
        if (!row) return
        selectedId = serviceName
        selectedService = serviceName
        statusText = ""
        actionError = ""
        queuedAction = null
        if (options.reset_search_on_page) search.clear()
        Qt.callLater(() => backButton.forceActiveFocus(Qt.TabFocusReason))
    }
    function back() {
        if (registryPage) return
        root.forceActiveFocus()
        queuedAction = null
        selectedService = ""
        actionError = ""
        if (options.reset_search_on_page) search.clear()
        Qt.callLater(() => { syncSelection(); search.forceActiveFocus() })
    }
    function refresh() {
        service.refresh()
        statusText = "Refreshing players…"
    }
    function dispatch(name, args) {
        if (!viewActive || registryPage || !player.service) return false
        const request = {name: name, service: String(player.service), owner: String(player.owner),
                         args: Object.assign({service: String(player.service), owner: String(player.owner)}, args || {})}
        if (service.busy) {
            queuedAction = request
            actionError = ""
            return true
        }
        queuedAction = null
        actionError = service.action(request.name, request.args) ? "" : "This player did not accept the control request."
        return actionError.length === 0
    }
    function flushAction() {
        if (!queuedAction || service.busy) return
        const request = queuedAction
        queuedAction = null
        const current = players.find(row => String(row.service) === request.service)
        actionError = viewActive && !registryPage && selectedService === request.service && current
                && String(current.owner) === request.owner && options.allow_actions && current.CanControl
                && service.action(request.name, request.args) ? "" : "The queued action was cancelled because its player changed."
    }
    function timeLabel(us) {
        const seconds = Math.max(0, Math.floor(Number(us || 0) / 1000000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }

    onViewActiveChanged: if (!viewActive) queuedAction = null
    onSelectedServiceChanged: queuedAction = null
    onRowsChanged: Qt.callLater(syncSelection)
    onPlayersChanged: {
        statusText = ""
        if (!registryPage && !players.some(row => String(row.service) === selectedService)) {
            const missing = selectedService
            root.forceActiveFocus()
            queuedAction = null
            selectedService = ""
            actionError = ""
            statusText = "Player disappeared: " + missing
            Qt.callLater(() => { syncSelection(); search.forceActiveFocus() })
        }
    }
    Connections {
        target: root.service
        function onBusyChanged() {
            if (root.service.busy) return
            if (root.statusText === "Refreshing players…") root.statusText = ""
            if (root.queuedAction) Qt.callLater(root.flushAction)
        }
    }
    Connections { target: Config; function onModelChanged() { root.queuedAction = null } }
    Component.onCompleted: { syncSelection(); search.forceActiveFocus() }

    Shortcut { enabled: root.registryPage; sequence: root.options.next_shortcut; onActivated: root.moveSelection(1, false) }
    Shortcut { enabled: root.registryPage; sequence: root.options.previous_shortcut; onActivated: root.moveSelection(-1, false) }
    Shortcut { enabled: root.registryPage; sequence: root.options.tab_shortcut; onActivated: root.moveSelection(1, true) }
    Shortcut { enabled: root.registryPage; sequence: root.options.reverse_tab_shortcut; onActivated: root.moveSelection(-1, true) }
    Shortcut { enabled: root.registryPage; sequence: root.options.activate_shortcut; onActivated: root.choose(root.selectedId) }
    Shortcut { sequence: root.options.refresh_shortcut; onActivated: root.refresh() }
    Shortcut { enabled: !root.registryPage; sequence: root.options.back_shortcut; onActivated: root.back() }
    Shortcut {
        sequence: root.options.close_shortcut
        onActivated: { if (root.registryPage) Shell.closePopup(); else root.back() }
    }

    padding: theme.padding
    opacity: module.style.opacity
    font.family: theme.font
    font.pixelSize: theme.font_size
    palette.text: foreground
    palette.windowText: foreground
    palette.buttonText: foreground
    palette.base: theme.palette.surface
    palette.button: theme.palette.surface
    palette.highlight: theme.palette.accent
    palette.highlightedText: theme.palette.background
    background: Rectangle {
        objectName: "media-launcher-background"
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
                id: backButton
                objectName: "media-launcher-back"
                iconName: "previous"
                actionLabel: "Back to all media players"
                visible: !root.registryPage
                activeFocusOnTab: visible
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.tab: refreshButton
                KeyNavigation.backtab: repeatButton.visible && repeatButton.enabled ? repeatButton : nextButton.enabled ? nextButton : playButton
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
                text: root.registryPage ? "Media players" : String(root.player.identity || root.selectedService)
                textFormat: Text.PlainText
                color: root.foreground
                elide: Text.ElideRight
            }
            ToolbarButton {
                id: refreshButton
                objectName: "media-launcher-refresh"
                iconName: "refresh"
                actionLabel: "Refresh (" + root.options.refresh_shortcut + ")"
                activeFocusOnTab: !root.registryPage
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.tab: closeButton
                KeyNavigation.backtab: backButton
                onClicked: root.refresh()
            }
            ToolbarButton {
                id: closeButton
                objectName: "media-launcher-close"
                iconName: "close"
                actionLabel: "Close (" + root.options.close_shortcut + ")"
                activeFocusOnTab: !root.registryPage
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.tab: progress.visible && progress.enabled ? progress : shuffleButton.visible && shuffleButton.enabled ? shuffleButton : previousButton.enabled ? previousButton : playButton
                KeyNavigation.backtab: refreshButton
                onClicked: Shell.closePopup()
            }
        }
        Label {
            objectName: "media-launcher-keyboard-help"
            Layout.fillWidth: true
            text: root.registryPage
                  ? "Keyboard: ↑/↓ choose · Tab/Shift+Tab cycle · Enter open · F5 refresh · Esc close"
                  : "Keyboard: Tab/Shift+Tab controls · Enter/Space activate · seek ←/→ · F5 refresh · Alt+Left/Esc back"
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: root.theme.palette.muted
            Accessible.name: text
        }
        TextField {
            id: search
            objectName: "media-launcher-search"
            visible: root.registryPage
            Layout.fillWidth: true
            implicitHeight: Math.max(Config.model.ui.module_height, contentHeight + topPadding + bottomPadding)
            padding: Config.model.ui.panel_padding
            leftPadding: root.theme.spacing
            rightPadding: root.theme.spacing
            color: root.foreground
            placeholderTextColor: root.theme.palette.muted
            selectionColor: root.theme.palette.accent
            selectedTextColor: root.theme.palette.background
            placeholderText: "Search current media players"
            selectByMouse: true
            focus: true
            background: Rectangle {
                objectName: "media-launcher-search-background"
                radius: Math.min(root.theme.radius, height / 2)
                color: Qt.alpha(root.theme.palette.surface, root.theme.opacity)
                border.width: root.theme.border_width
                border.color: search.activeFocus ? root.theme.palette.accent : root.theme.palette.border
            }
        }
        Label {
            objectName: "media-launcher-status"
            Layout.fillWidth: true
            visible: text.length > 0
            color: root.service.diagnostic || root.actionError ? root.theme.palette.accent : root.theme.palette.muted
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: root.actionError || root.statusText || (!root.service.available ? root.service.diagnostic
                  : root.registryPage && !root.rows.length ? (search.text ? "No matching players" : "No readable MPRIS players") : "")
        }
        ListView {
            id: entries
            objectName: "media-launcher-entries"
            visible: root.registryPage
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            currentIndex: root.rows.findIndex(row => String(row.service) === root.selectedId)
            ScrollBar.vertical: ScrollBar {}
            delegate: ShellButton {
                id: entry
                required property var modelData
                width: entries.width
                objectName: "media-launcher-entry-" + modelData.service
                focusPolicy: Qt.NoFocus
                accent: String(modelData.service) === root.selectedId
                enabled: !(root.service.busy && !root.service.available)
                text: String(modelData.identity || modelData.service)
                iconName: root.module.style.show_icon ? root.module.style.icon : ""
                iconSize: root.module.style.icon_size
                contentItem: RowLayout {
                    spacing: root.theme.spacing
                    Image {
                        visible: root.module.style.show_icon
                        Layout.preferredWidth: root.module.style.icon_size
                        Layout.preferredHeight: root.module.style.icon_size
                        sourceSize: Qt.size(width, height)
                        source: "image://icons/" + root.theme.icon_mode + "/" + root.module.style.icon
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { Layout.fillWidth: true; text: entry.modelData.identity || entry.modelData.service; color: root.foreground; font: entry.font; elide: Text.ElideRight }
                        Text { Layout.fillWidth: true; text: (entry.modelData.playbackStatus || "Unknown") + (entry.modelData.title ? " · " + entry.modelData.title : ""); color: root.theme.palette.muted; font.pixelSize: root.theme.font_size * .9; elide: Text.ElideRight }
                    }
                }
                Accessible.name: text + ", " + (modelData.playbackStatus || "unknown") + (modelData.title ? ", " + modelData.title : "")
                onClicked: root.choose(String(modelData.service))
            }
        }
        ScrollView {
            id: detail
            objectName: "media-launcher-detail"
            visible: !root.registryPage
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: detail.availableWidth
                spacing: root.theme.spacing
                Rectangle {
                    visible: root.options.show_artwork
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(root.options.artwork_height, Math.max(80, root.availableHeight * .34))
                    color: root.theme.palette.surface
                    clip: true
                    Image {
                        id: cover
                        objectName: "media-launcher-artwork"
                        anchors.fill: parent
                        asynchronous: true
                        cache: false
                        sourceSize: Qt.size(Math.ceil(width), Math.ceil(height))
                        fillMode: Image.PreserveAspectFit
                        source: {
                            if (!root.options.show_artwork || !root.viewActive) return ""
                            const url = String(root.player.artUrl || "")
                            return url.startsWith("file:") || (root.options.artwork_remote && /^https?:\/\//i.test(url)) ? url : ""
                        }
                        Accessible.name: "Album artwork"
                    }
                    Image { anchors.centerIn: parent; visible: cover.status !== Image.Ready; source: "image://icons/builtin/media"; width: root.theme.icon_size * 3; height: width; sourceSize: Qt.size(width, height) }
                }
                Label { objectName: "media-launcher-title"; Layout.fillWidth: true; text: root.player.title || "Untitled track"; color: root.foreground; horizontalAlignment: Text.AlignHCenter; font.bold: true; font.pixelSize: root.theme.font_size * 1.4; maximumLineCount: 2; elide: Text.ElideRight }
                Label { objectName: "media-launcher-artist"; visible: root.options.show_artist; Layout.fillWidth: true; text: (root.player.artist || []).join(", "); color: root.foreground; horizontalAlignment: Text.AlignHCenter; maximumLineCount: 1; elide: Text.ElideRight }
                Label { visible: root.options.show_album; Layout.fillWidth: true; text: root.player.album || ""; color: root.theme.palette.muted; horizontalAlignment: Text.AlignHCenter; maximumLineCount: 1; elide: Text.ElideRight }
                Label { Layout.fillWidth: true; text: "Status: " + (root.player.playbackStatus || "Unknown"); color: root.theme.palette.muted; horizontalAlignment: Text.AlignHCenter; Accessible.name: text }
                ColumnLayout {
                    visible: root.options.show_progress
                    Layout.fillWidth: true
                    spacing: 0
                    Slider {
                        id: progress
                        objectName: "media-launcher-progress"
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(0, Number(root.player.lengthUs || 0) / 1000000 - .000001)
                        stepSize: root.options.seek_step_seconds
                        Binding on value {
                            when: !progress.pressed
                            value: Number(root.player.positionUs || 0) / 1000000
                            restoreMode: Binding.RestoreNone
                        }
                        enabled: root.controllable && !!root.player.CanSeek && to > 0
                        activeFocusOnTab: enabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.tab: shuffleButton.visible && shuffleButton.enabled ? shuffleButton : previousButton.enabled ? previousButton : playButton
                        KeyNavigation.backtab: closeButton
                        onMoved: root.dispatch("setPosition", {trackId: root.player.trackId, positionUs: Math.round(value * 1000000)})
                        Accessible.name: "Track position; Left and Right seek " + root.options.seek_step_seconds + " seconds"
                        Keys.onPressed: event => root.handleDetailKey(event)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: root.timeLabel(progress.value * 1000000); color: root.theme.palette.muted }
                        Item { Layout.fillWidth: true }
                        Label { text: root.timeLabel(root.player.lengthUs); color: root.theme.palette.muted }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.spacing
                    Item { Layout.fillWidth: true }
                    MediaButton {
                        id: shuffleButton
                        objectName: "media-launcher-shuffle"
                        visible: root.options.show_shuffle
                        symbol: "shuffle"; Accessible.name: "Shuffle"
                        controlSize: root.controlExtent; glyphSize: root.options.control_icon_size
                        accent: !!root.player.shuffle
                        enabled: root.controllable && !!root.player.hasShuffle
                        activeFocusOnTab: enabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.tab: previousButton.enabled ? previousButton : playButton
                        KeyNavigation.backtab: progress.visible && progress.enabled ? progress : closeButton
                        Keys.onPressed: event => root.handleDetailKey(event)
                        onClicked: root.dispatch("setShuffle", {shuffle: !root.player.shuffle})
                    }
                    MediaButton {
                        id: previousButton
                        objectName: "media-launcher-previous"
                        symbol: "previous"; Accessible.name: "Previous track"
                        controlSize: root.controlExtent; glyphSize: root.options.control_icon_size
                        enabled: root.controllable && !!root.player.CanGoPrevious
                        activeFocusOnTab: enabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.tab: playButton
                        KeyNavigation.backtab: shuffleButton.visible && shuffleButton.enabled ? shuffleButton : progress.visible && progress.enabled ? progress : closeButton
                        Keys.onPressed: event => root.handleDetailKey(event)
                        onClicked: root.dispatch("previous")
                    }
                    MediaButton {
                        id: playButton
                        objectName: "media-launcher-play-pause"
                        symbol: root.player.playbackStatus === "Playing" ? "pause" : "play"
                        Accessible.name: root.player.playbackStatus === "Playing" ? "Pause" : "Play"
                        controlSize: root.controlExtent * 1.4; glyphSize: root.options.control_icon_size * 1.25
                        foreground: root.theme.palette.background
                        baseColor: root.theme.palette.accent
                        enabled: root.controllable && (root.player.playbackStatus === "Playing" ? !!root.player.CanPause : !!root.player.CanPlay)
                        activeFocusOnTab: enabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.tab: nextButton.enabled ? nextButton : repeatButton.visible && repeatButton.enabled ? repeatButton : backButton
                        KeyNavigation.backtab: previousButton.enabled ? previousButton : shuffleButton.visible && shuffleButton.enabled ? shuffleButton : progress.visible && progress.enabled ? progress : closeButton
                        Keys.onPressed: event => root.handleDetailKey(event)
                        onClicked: root.dispatch("playPause")
                    }
                    MediaButton {
                        id: nextButton
                        objectName: "media-launcher-next"
                        symbol: "next"; Accessible.name: "Next track"
                        controlSize: root.controlExtent; glyphSize: root.options.control_icon_size
                        enabled: root.controllable && !!root.player.CanGoNext
                        activeFocusOnTab: enabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.tab: repeatButton.visible && repeatButton.enabled ? repeatButton : backButton
                        KeyNavigation.backtab: playButton
                        Keys.onPressed: event => root.handleDetailKey(event)
                        onClicked: root.dispatch("next")
                    }
                    MediaButton {
                        id: repeatButton
                        objectName: "media-launcher-repeat"
                        visible: root.options.show_repeat
                        symbol: root.player.loopStatus === "Track" ? "repeat-one" : "repeat"
                        Accessible.name: "Repeat: " + (root.player.loopStatus || "Not supported")
                        controlSize: root.controlExtent; glyphSize: root.options.control_icon_size
                        accent: root.player.loopStatus === "Track" || root.player.loopStatus === "Playlist"
                        enabled: root.controllable && !!root.player.hasLoopStatus
                        activeFocusOnTab: enabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.tab: backButton
                        KeyNavigation.backtab: nextButton.enabled ? nextButton : playButton
                        Keys.onPressed: event => root.handleDetailKey(event)
                        onClicked: root.dispatch("setLoopStatus", {loopStatus: root.player.loopStatus === "None" ? "Playlist" : root.player.loopStatus === "Playlist" ? "Track" : "None"})
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}

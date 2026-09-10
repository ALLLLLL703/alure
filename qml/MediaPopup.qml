import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    readonly property var theme: Config.model.theme
    readonly property var options: Config.model.modules.media.behavior
    readonly property var service: Services.media
    readonly property var players: service.items
    property string selectedService: ""
    readonly property int playerIndex: Math.max(0, players.findIndex(p => p.service === selectedService))
    readonly property var player: players[playerIndex] || ({})
    readonly property bool ready: service.available && players.length > 0
    readonly property bool controllable: ready && options.allow_actions && !!player.CanControl
    property var queuedAction: null
    property string actionError: ""
    function dispatch(name, args) {
        const request = {name: name, args: Object.assign({service: player.service}, args || {})}
        if (service.busy) queuedAction = request
        else actionError = service.action(request.name, request.args) ? "" : "This player did not accept the control request."
    }
    function timeLabel(us) {
        const seconds = Math.max(0, Math.floor(Number(us || 0) / 1000000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
    Connections {
        target: root.service
        function onChanged() {
            if (root.queuedAction && !root.service.busy) {
                const action = root.queuedAction; root.queuedAction = null
                root.actionError = root.service.action(action.name, action.args) ? "" : "This player did not accept the control request."
            }
        }
    }
    focus: true
    padding: theme.padding
    font.family: theme.font
    font.pixelSize: theme.font_size
    palette.text: theme.palette.foreground
    palette.buttonText: theme.palette.foreground
    palette.base: theme.palette.surface
    palette.button: theme.palette.surface
    palette.highlight: theme.palette.accent
    palette.dark: theme.palette.accent
    palette.window: theme.palette.background
    palette.windowText: theme.palette.foreground
    palette.highlightedText: theme.palette.background
    Keys.onEscapePressed: event => { if (Config.model.ui.escape_closes) { Shell.closePopup(); event.accepted = true } }
    background: Rectangle {
        radius: root.theme.radius
        color: Qt.alpha(root.theme.palette.background, root.theme.opacity)
        border.width: root.theme.border_width
        border.color: root.theme.palette.border
    }
    contentItem: ColumnLayout {
        spacing: root.theme.spacing
        RowLayout {
            Layout.fillWidth: true
            InfoText { text: "Now Playing"; font.bold: true; font.pixelSize: root.theme.font_size * 1.35; Layout.fillWidth: true }
            ComboBox {
                objectName: "media-player-choice"
                visible: root.ready
                Layout.maximumWidth: root.availableWidth / 2
                model: root.players
                textRole: "identity"
                currentIndex: root.playerIndex
                onActivated: root.selectedService = root.players[currentIndex].service
                Accessible.name: "Media player"
            }
            ShellButton { objectName: "popup-close"; text: "×"; Accessible.name: "Close now playing"; onClicked: Shell.closePopup() }
        }
        InfoText {
            visible: !root.ready || root.actionError.length > 0
            text: root.actionError || root.service.diagnostic
            color: root.theme.palette.muted
            Layout.fillWidth: true
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.ready
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: root.theme.spacing
                Rectangle {
                    visible: root.options.show_artwork
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.options.artwork_height
                    color: root.theme.palette.surface
                    clip: true
                    Image {
                        id: cover
                        objectName: "media-artwork"
                        anchors.fill: parent
                        asynchronous: true
                        cache: false
                        sourceSize: Qt.size(Math.ceil(width), Math.ceil(height))
                        fillMode: Image.PreserveAspectCrop
                        source: {
                            const url = String(root.player.artUrl || "")
                            if (!root.options.show_artwork || !root.Window.window || !root.Window.window.visible) return ""
                            return url.startsWith("file:") || (root.options.artwork_remote && /^https?:\/\//i.test(url)) ? url : ""
                        }
                        Accessible.name: "Album artwork"
                    }
                    InfoText {
                        anchors.centerIn: parent
                        visible: cover.status !== Image.Ready
                        text: cover.status === Image.Loading ? "Loading artwork…" : "♫"
                        color: root.theme.palette.muted
                        font.pixelSize: cover.status === Image.Loading ? root.theme.font_size : root.theme.icon_size * 3
                    }
                }
                InfoText {
                    objectName: "media-title"
                    text: root.player.title || "Untitled track"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    font.bold: true
                    font.pixelSize: root.theme.font_size * 1.4
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                InfoText {
                    objectName: "media-artist"
                    visible: root.options.show_artist
                    text: (root.player.artist || []).join(", ")
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
                InfoText {
                    visible: root.options.show_album
                    text: root.player.album || ""
                    color: root.theme.palette.muted
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
                ColumnLayout {
                    visible: root.options.show_progress
                    Layout.fillWidth: true
                    spacing: 0
                    Slider {
                        id: progress
                        objectName: "media-progress"
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(0, Number(root.player.lengthUs || 0) / 1000000 - 0.000001)
                        enabled: root.controllable && !!root.player.CanSeek && to > 0
                        property string dragTrack: ""
                        property string dragService: ""
                        property real dragPosition: 0
                        Binding on value {
                            when: !progress.pressed
                            value: Number(root.player.positionUs || 0) / 1000000
                            restoreMode: Binding.RestoreNone
                        }
                        onPressedChanged: {
                            if (pressed) { dragTrack = root.player.trackId || ""; dragService = root.player.service || ""; dragPosition = value * 1000000 }
                            else if (dragTrack) { root.dispatch("setPosition", {service: dragService, trackId: dragTrack, positionUs: Math.round(dragPosition)}); dragTrack = "" }
                        }
                        onMoved: {
                            if (pressed) dragPosition = value * 1000000
                            else root.dispatch("setPosition", {trackId: root.player.trackId, positionUs: Math.round(value * 1000000)})
                        }
                        Accessible.name: "Track position"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        InfoText { text: root.timeLabel(progress.pressed ? progress.position * Number(root.player.lengthUs || 0) : root.player.positionUs); color: root.theme.palette.muted }
                        Item { Layout.fillWidth: true }
                        InfoText { text: root.timeLabel(root.player.lengthUs); color: root.theme.palette.muted }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.spacing
                    Item { Layout.fillWidth: true }
                    ShellButton {
                        objectName: "media-shuffle"
                        visible: root.options.show_shuffle
                        text: "⤨"; Accessible.name: "Shuffle"
                        accent: !!root.player.shuffle
                        enabled: root.controllable && !!root.player.hasShuffle && !root.service.busy
                        onClicked: root.dispatch("setShuffle", {shuffle: !root.player.shuffle})
                    }
                    ShellButton {
                        objectName: "media-previous"
                        text: "|◀"; Accessible.name: "Previous track"
                        font.pixelSize: root.theme.icon_size
                        enabled: root.controllable && !!root.player.CanGoPrevious && !root.service.busy
                        onClicked: root.dispatch("previous")
                    }
                    ShellButton {
                        id: playControl
                        objectName: "media-play-pause"
                        contentItem: Text {
                            text: playControl.text
                            font: playControl.font
                            color: playControl.foreground
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            opacity: playControl.enabled ? 1 : 0.45
                        }
                        text: root.player.playbackStatus === "Playing" ? "Ⅱ" : "▶"
                        Accessible.name: root.player.playbackStatus === "Playing" ? "Pause" : "Play"
                        Layout.preferredWidth: Config.model.ui.module_height * 2
                        Layout.preferredHeight: Config.model.ui.module_height * 2
                        font.pixelSize: root.theme.icon_size * 1.6
                        foreground: root.theme.palette.background
                        baseColor: root.theme.palette.accent
                        background: Rectangle { radius: width / 2; color: root.theme.palette.accent; opacity: parent.down ? 0.7 : 1 }
                        enabled: root.controllable && !root.service.busy && (root.player.playbackStatus === "Playing" ? !!root.player.CanPause : !!root.player.CanPlay)
                        onClicked: root.dispatch("playPause")
                    }
                    ShellButton {
                        objectName: "media-next"
                        text: "▶|"; Accessible.name: "Next track"
                        font.pixelSize: root.theme.icon_size
                        enabled: root.controllable && !!root.player.CanGoNext && !root.service.busy
                        onClicked: root.dispatch("next")
                    }
                    ShellButton {
                        objectName: "media-repeat"
                        visible: root.options.show_repeat
                        text: root.player.loopStatus === "Track" ? "↻1" : "↻"
                        Accessible.name: "Repeat: " + (root.player.loopStatus || "Not supported")
                        accent: root.player.loopStatus === "Track" || root.player.loopStatus === "Playlist"
                        enabled: root.controllable && !!root.player.hasLoopStatus && !root.service.busy
                        onClicked: root.dispatch("setLoopStatus", {loopStatus: root.player.loopStatus === "None" ? "Playlist" : root.player.loopStatus === "Playlist" ? "Track" : "None"})
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}

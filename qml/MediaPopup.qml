import QtQuick
import Alure 1.0
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    readonly property var theme: Config.model.theme
    readonly property var options: Config.model.modules.media.behavior
    opacity: Config.model.modules.media.style.opacity
    readonly property var service: Services.media
    readonly property var players: service.items
    property string selectedService: ""
    readonly property int playerIndex: Math.max(0, players.findIndex(p => p.service === selectedService))
    readonly property var player: players[playerIndex] || ({})
    readonly property bool ready: service.available && players.length > 0
    readonly property bool controllable: ready && options.allow_actions && !!player.CanControl
    property var queuedAction: null
    readonly property bool viewActive: visible && !!Window.window && Window.window.visible
    onViewActiveChanged: { if (!viewActive) queuedAction = null }
    Connections { target: Config; function onModelChanged() { root.queuedAction = null } }
    property string actionError: ""
    readonly property real controlExtent: Math.min(options.control_size, Math.max(16, (availableWidth - 6 * theme.spacing) / 5.4))
    readonly property string targetPlayerService: player.service || ""
    onTargetPlayerServiceChanged: queuedAction = null
    function dispatch(name, args) {
        if (!viewActive) return
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
            if (!root.ready) root.queuedAction = null
            if (root.queuedAction && !root.service.busy) Qt.callLater(root.flushAction)
        }
    }
    function flushAction() {
        if (!queuedAction || service.busy) return
        const action = queuedAction; queuedAction = null
        actionError = viewActive && controllable && service.action(action.name, action.args) ? "" : "This player did not accept the control request."
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
        BackgroundBlur { anchors.fill: parent; radius: parent.radius; blurEnabled: Config.model.theme.blur_enabled }
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
                id: playerChoice
                objectName: "media-player-choice"
                visible: root.ready
                Layout.maximumWidth: root.availableWidth / 2
                model: root.players
                textRole: "identity"
                currentIndex: root.playerIndex
                onActivated: root.selectedService = root.players[currentIndex].service
                Accessible.name: "Media player"
                delegate: ItemDelegate {
                    id: sourceChoice
                    required property int index
                    required property var modelData
                    objectName: "media-source-" + index
                    width: playerChoice.width
                    highlighted: playerChoice.highlightedIndex === index
                    hoverEnabled: true
                    contentItem: Text {
                        objectName: "media-source-label-" + sourceChoice.index
                        text: sourceChoice.modelData.identity
                        font: playerChoice.font
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        color: sourceChoice.highlighted || sourceChoice.hovered ? root.theme.palette.background : root.theme.palette.foreground
                    }
                    background: Rectangle {
                        objectName: "media-source-background-" + sourceChoice.index
                        color: sourceChoice.highlighted || sourceChoice.hovered ? root.theme.palette.accent : root.theme.palette.surface
                        radius: root.theme.radius / 2
                    }
                }
            }
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
                    Layout.preferredHeight: Math.min(root.options.artwork_height, Math.max(80, root.availableHeight * 0.38))
                    color: root.theme.palette.surface
                    clip: true
                    Image {
                        id: cover
                        objectName: "media-artwork"
                        anchors.fill: parent
                        asynchronous: true
                        cache: false
                        sourceSize: Qt.size(Math.ceil(width), Math.ceil(height))
                        fillMode: Image.PreserveAspectFit
                        source: {
                            const url = String(root.player.artUrl || "")
                            if (!root.options.show_artwork || !root.Window.window || !root.Window.window.visible) return ""
                            return url.startsWith("file:") || (root.options.artwork_remote && /^https?:\/\//i.test(url)) ? url : ""
                        }
                        Accessible.name: "Album artwork"
                    }
                    Image {
                        anchors.centerIn: parent
                        visible: cover.status !== Image.Ready
                        source: "image://icons/builtin/media"
                        width: root.theme.icon_size * 3; height: width
                        sourceSize: Qt.size(width, height)
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
                    MediaButton {
                        Layout.preferredWidth: root.controlExtent; Layout.preferredHeight: root.controlExtent
                        objectName: "media-shuffle"
                        visible: root.options.show_shuffle
                        symbol: "shuffle"; Accessible.name: "Shuffle"
                        accent: !!root.player.shuffle
                        enabled: root.controllable && !!root.player.hasShuffle
                        onClicked: root.dispatch("setShuffle", {shuffle: !root.player.shuffle})
                    }
                    MediaButton {
                        Layout.preferredWidth: root.controlExtent; Layout.preferredHeight: root.controlExtent
                        objectName: "media-previous"
                        symbol: "previous"; Accessible.name: "Previous track"
                        enabled: root.controllable && !!root.player.CanGoPrevious
                        onClicked: root.dispatch("previous")
                    }
                    MediaButton {
                        objectName: "media-play-pause"
                        symbol: root.player.playbackStatus === "Playing" ? "pause" : "play"
                        Accessible.name: root.player.playbackStatus === "Playing" ? "Pause" : "Play"
                        Layout.preferredWidth: root.controlExtent * 1.4
                        Layout.preferredHeight: root.controlExtent * 1.4
                        glyphSize: root.options.control_icon_size * 1.25
                        foreground: root.theme.palette.background
                        baseColor: root.theme.palette.accent
                        background: Rectangle { radius: width / 2; color: root.theme.palette.accent; opacity: parent.down ? 0.7 : 1 }
                        enabled: root.controllable && (root.player.playbackStatus === "Playing" ? !!root.player.CanPause : !!root.player.CanPlay)
                        onClicked: root.dispatch("playPause")
                    }
                    MediaButton {
                        Layout.preferredWidth: root.controlExtent; Layout.preferredHeight: root.controlExtent
                        objectName: "media-next"
                        symbol: "next"; Accessible.name: "Next track"
                        enabled: root.controllable && !!root.player.CanGoNext
                        onClicked: root.dispatch("next")
                    }
                    MediaButton {
                        Layout.preferredWidth: root.controlExtent; Layout.preferredHeight: root.controlExtent
                        objectName: "media-repeat"
                        visible: root.options.show_repeat
                        symbol: root.player.loopStatus === "Track" ? "repeat-one" : "repeat"
                        Accessible.name: "Repeat: " + (root.player.loopStatus || "Not supported")
                        accent: root.player.loopStatus === "Track" || root.player.loopStatus === "Playlist"
                        enabled: root.controllable && !!root.player.hasLoopStatus
                        onClicked: root.dispatch("setLoopStatus", {loopStatus: root.player.loopStatus === "None" ? "Playlist" : root.player.loopStatus === "Playlist" ? "Track" : "None"})
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}

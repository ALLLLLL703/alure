import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Ui.js" as Ui

Control {
    id: root
    required property string moduleName
    readonly property var theme: Config.model.theme
    readonly property var config: Config.model.modules[moduleName]
    opacity: config.style.opacity
    readonly property var service: moduleName === "calendar" ? null : Services[moduleName] || null
    readonly property bool ready: !!service && service.available
    readonly property bool canAct: ready && config.behavior.allow_actions && !service.busy
    property string actionStatus: ""
    property bool updateConfirmation: false
    function act(name, args) {
        actionStatus = service && service.action(name, args || {}) ? "Request sent; waiting for provider." : "Request not accepted. Check availability, configured commands and action permissions."
    }
    function toggleDnd() {
        const dnd = !service.state.dnd
        if (config.behavior.persist_dnd) {
            const edit = Config.editLiteral(Config.source, "modules.notifications.behavior.dnd", dnd ? "true" : "false")
            if (edit.error) { actionStatus = edit.error; return }
            if (!Config.saveText(edit.text)) { actionStatus = Config.diagnostic; return }
        }
        act("setDnd", {dnd: dnd})
    }
    Connections {
        target: root.service
        function onChanged() {
            if (root.actionStatus.indexOf("Request sent") === 0 && root.service.available && !root.service.busy)
                root.actionStatus = "Provider snapshot updated."
        }
    }
    focus: true
    padding: theme.padding
    font.family: theme.font
    font.pixelSize: theme.font_size
    palette.base: theme.palette.surface
    palette.text: theme.palette.foreground
    palette.button: theme.palette.surface
    palette.buttonText: theme.palette.foreground
    palette.highlight: theme.palette.accent
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
            InfoText { text: Ui.title(root.moduleName); font.bold: true; font.pixelSize: root.theme.font_size * 1.35; Layout.fillWidth: true }
            ShellButton { iconName: "refresh"; Accessible.name: "Refresh"; visible: !!root.service; enabled: !!root.service && !root.service.busy; onClicked: root.service.refresh() }
        }
        InfoText {
            visible: root.moduleName !== "calendar"
            text: !root.service ? "No provider is implemented for this module." : root.service.diagnostic || (root.service.busy ? "Refreshing…" : root.ready ? "Live system data" : "Provider unavailable")
            color: root.ready ? root.theme.palette.muted : root.theme.palette.accent
            Layout.fillWidth: true
        }
        InfoText { visible: root.actionStatus.length > 0; text: root.actionStatus; color: root.theme.palette.muted; Layout.fillWidth: true }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: root.theme.spacing
                Loader {
                    Layout.fillWidth: true
                    active: root.moduleName === "calendar"
                    sourceComponent: CalendarView { }
                }
                ColumnLayout {
                    visible: root.moduleName === "volume" && root.ready
                    Layout.fillWidth: true
                    InfoText { text: root.ready ? Math.round(root.service.state.percent || 0) + "%" + (root.service.state.muted ? " · Muted" : "") : ""; font.pixelSize: root.theme.font_size * 2; Layout.fillWidth: true }
                    Slider {
                        Layout.fillWidth: true
                        from: 0; to: root.config.behavior.max_percent || 100; stepSize: 1
                        value: root.ready ? root.service.state.percent || 0 : 0
                        enabled: root.canAct && (root.config.behavior.set_volume_command || []).length > 0
                        Accessible.name: "Volume percent"
                        onMoved: root.act("setVolume", {percent: value})
                    }
                    ShellButton { text: root.ready && root.service.state.muted ? "Unmute" : "Mute"; enabled: root.canAct && (root.config.behavior.mute_command || []).length > 0; onClicked: root.act("toggleMute") }
                }
                ColumnLayout {
                    visible: root.moduleName === "wifi" && root.ready
                    Layout.fillWidth: true
                    ShellButton {
                        text: root.ready && root.service.state.powered ? "Turn Wi-Fi off" : "Turn Wi-Fi on"
                        enabled: root.canAct && (root.config.behavior.radio_command || []).length > 0
                        onClicked: root.act("setPowered", {powered: !root.service.state.powered})
                    }
                    InfoText { text: "Saved connections"; font.bold: true }
                    Repeater {
                        model: root.moduleName === "wifi" && root.ready ? root.service.state.savedConnections || [] : []
                        ShellButton {
                            required property var modelData
                            text: "Connect · " + modelData.name
                            Layout.fillWidth: true
                            enabled: root.canAct && (root.config.behavior.connect_command || []).length > 0
                            onClicked: root.act("connectSaved", {uuid: modelData.uuid})
                        }
                    }
                    InfoText { text: "Cached access points · only saved profiles can connect; use your network manager for passwords and new networks."; Layout.fillWidth: true; color: root.theme.palette.muted }
                }
                ColumnLayout {
                    visible: root.moduleName === "bluetooth" && root.ready
                    Layout.fillWidth: true
                    Repeater {
                        model: root.moduleName === "bluetooth" && root.ready ? root.service.state.adapters || [] : []
                        ShellButton {
                            required property var modelData
                            text: (modelData.Alias || modelData.Name || modelData.Address) + (modelData.Powered ? " · Turn off" : " · Turn on")
                            Layout.fillWidth: true
                            enabled: root.canAct
                            onClicked: root.act("setPowered", {path: modelData.path, powered: !modelData.Powered})
                        }
                    }
                    InfoText { text: "Paired-device controls · pairing and discovery require your Bluetooth manager."; Layout.fillWidth: true; color: root.theme.palette.muted }
                }
                RowLayout {
                    visible: root.moduleName === "notifications" && root.ready
                    Layout.fillWidth: true
                    ShellButton { objectName: "notification-dnd"; text: root.ready && root.service.state.dnd ? "Turn off do not disturb" : "Turn on do not disturb"; accent: root.ready && !!root.service.state.dnd; enabled: root.canAct; onClicked: root.toggleDnd() }
                    ShellButton { text: "Clear history"; enabled: root.canAct; onClicked: root.act("clearHistory") }
                }
                InfoText {
                    visible: root.moduleName === "notifications" && root.ready
                    Layout.fillWidth: true
                    color: root.theme.palette.muted
                    text: root.ready && root.service.state.dnd ? "Banners paused by do not disturb; history is still recorded."
                          : !Config.model.ui.toast.enabled || !root.config.behavior.toast_enabled ? "Banners disabled in configuration."
                          : "New notifications appear as banners. Earlier suppressed notifications stay in history."
                }
                ColumnLayout {
                    visible: root.moduleName === "updates"
                    Layout.fillWidth: true
                    ShellButton { text: "Run configured update command…"; enabled: root.canAct && (root.config.behavior.update_command || []).length > 0; onClicked: root.updateConfirmation = true }
                    RowLayout {
                        visible: root.updateConfirmation
                        ShellButton { text: "Confirm run"; enabled: root.canAct; onClicked: { root.updateConfirmation = false; root.act("update") } }
                        ShellButton { text: "Cancel"; onClicked: root.updateConfirmation = false }
                    }
                    InfoText { text: "No automatic installation. Set update_command to a terminal argv if desired. The module timeout also applies to this command."; Layout.fillWidth: true; color: root.theme.palette.muted }
                }
                Repeater {
                    model: root.ready && root.moduleName !== "volume" ? (root.moduleName === "notifications" ? root.service.items.slice().reverse() : root.service.items) : []
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: detail.implicitHeight + root.theme.padding * 2
                        radius: root.theme.radius
                        color: root.theme.palette.surface
                        ColumnLayout {
                            id: detail
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: root.theme.padding }
                            spacing: root.theme.spacing / 2
                            Image {
                                visible: root.moduleName === "notifications" && Config.model.ui.toast.show_icon
                                Layout.preferredWidth: Config.model.ui.toast.icon_size
                                Layout.preferredHeight: Layout.preferredWidth
                                source: visible ? card.modelData.iconUrl || "image://icons/builtin/notifications" : ""
                                fillMode: Image.PreserveAspectFit
                                cache: false
                            }
                            InfoText {
                                Layout.fillWidth: true
                                font.bold: true
                                text: {
                                    const d = card.modelData
                                    switch (root.moduleName) {
                                    case "workspaces": return (d.name || "Workspace " + d.idx) + " · " + d.output
                                    case "tray": return d.Title || d.id
                                    case "updates": return d.name
                                    case "wifi": return (d.ssid || "Hidden SSID") + " · " + d.signal + "%" + (d.active ? " · Connected" : "")
                                    case "bluetooth": return d.Alias || d.Name || d.Address
                                    case "notifications": return d.summary || d.appName
                                    case "battery": return d.name + " · " + d.percent + "%"
                                    default: return ""
                                    }
                                }
                            }
                            InfoText {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: {
                                    const d = card.modelData
                                    switch (root.moduleName) {
                                    case "updates": return d.current + " → " + d.next
                                    case "bluetooth": return (d.Connected ? "Connected" : "Disconnected") + (d.Paired ? " · Paired" : " · Not paired")
                                    case "notifications": return d.appName + " · " + Qt.formatDateTime(new Date(Number(d.createdAt)), "HH:mm") + (d.suppressed ? " · Suppressed" : "") + "\n" + d.body
                                    case "battery": return d.status
                                    case "tray": return "Primary / secondary activation. Right-click the tray icon in the panel to open its menu."
                                    default: return ""
                                    }
                                }
                                color: root.theme.palette.muted
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: root.theme.spacing / 2
                                ShellButton { visible: root.moduleName === "workspaces"; text: card.modelData.is_active ? "Active" : "Switch here"; enabled: root.canAct; onClicked: root.act("activate", {id: card.modelData.id}) }
                                Repeater {
                                    model: root.moduleName === "tray" ? ["activate", "secondaryActivate"] : []
                                    ShellButton {
                                        required property string modelData
                                        text: modelData; enabled: root.canAct
                                        onClicked: { const p = mapToGlobal(width / 2, height / 2); root.act(modelData, {id: card.modelData.id, x: Math.round(p.x), y: Math.round(p.y)}) }
                                    }
                                }
                                ShellButton { visible: root.moduleName === "bluetooth"; text: card.modelData.Connected ? "Disconnect" : "Connect"; enabled: root.canAct && !!card.modelData.Paired; onClicked: root.act(card.modelData.Connected ? "disconnect" : "connect", {path: card.modelData.path}) }
                                ShellButton { visible: root.moduleName === "notifications"; text: "Open sender"; enabled: root.canAct; onClicked: root.act("activate", {id: card.modelData.id}) }
                                ShellButton { visible: root.moduleName === "notifications" && !!card.modelData.active; text: "Dismiss"; enabled: root.canAct; onClicked: root.act("dismiss", {id: card.modelData.id}) }
                                Repeater {
                                    model: root.moduleName === "notifications" && card.modelData.active ? card.modelData.actions || [] : []
                                    ShellButton {
                                        required property var modelData
                                        text: modelData.label; enabled: root.canAct
                                        onClicked: root.act("invoke", {id: card.modelData.id, key: modelData.key})
                                    }
                                }
                            }
                        }
                    }
                }
                InfoText { visible: root.ready && root.moduleName !== "volume" && root.service.items.length === 0; text: "No items reported."; color: root.theme.palette.muted; Layout.fillWidth: true }
            }
        }
    }
}

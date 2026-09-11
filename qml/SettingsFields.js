.pragma library
.import "Ui.js" as Ui

function label(key) {
    const names = {modules_left: "Modules · left / top", modules_center: "Modules · center", modules_right: "Modules · right / bottom", layout: "Panel layout", spacer_size: "Fixed spacer size", name: "Theme", font: "Font family", font_size: "Font size", icon_mode: "Icon source", icon_size: "Icon size", icon_theme: "Desktop icon theme", opacity: "Opacity", exclusive_zone: "Reserved screen space", sysfs_path: "Power supply directory", socket_path: "Niri socket", ordering: "Workspace order", id: "Panel name", output: "Output", format: "Label format", command: "Read command", close_shortcut: "Close keyboard shortcut"}
    return names[key] || key.replace(/_/g, " ").replace(/^./, c => c.toUpperCase())
}
function describe(path, value) {
    const key = path.split(".").pop()
    let options = []
    if (path === "modules.brightness.behavior.scroll_target") options = ["screen", "keyboard"]
    if (key === "icon_mode") options = ["builtin", "theme"]
    if (key === "controls_style") options = ["Basic", "Fusion"]
    if (path === "modules.volume.behavior.backend") options = ["auto", "pipewire", "pulseaudio"]
    if (key === "edge") options = path.indexOf("ui.toast") === 0 ? ["top", "bottom"] : ["top", "bottom", "left", "right"]
    if (key === "popup_alignment") options = ["center", "start", "end"]
    if (key === "popup_direction") options = ["inward", "top", "bottom", "left", "right"]
    if (key === "layer") options = ["background", "bottom", "top", "overlay"]
    if (key === "layout") options = ["linear", "three-zone"]
    if (key === "ordering") options = ["output-index", "provider"]
    if (key === "cursor_fallback") options = ["center", "cancel"]
    if (key === "active_indicator") options = ["underline", "pill"]
    const ranges = {module_tooltip_delay_ms: [0,5000], window_gap: [-256,256], control_size: [24,96], control_icon_size: [16,64], font_size: [6,72], editor_font_size: [6,72], spacing: [0,128], padding: [0,128], radius: [0,128], border_width: [0,16], opacity: [0,1,0.01], icon_size: [8,128], panel_padding: [0,128], module_height: [16,256], popup_width: [240,1920], popup_height: [240,2160], popup_gap: [0,256], animation_ms: [0,2000], thickness: [16,512], length: [0,32768], exclusive_zone: [-1,32768], min_width: [16,1024], max_width: [16,2048], interval_ms: [100,86400000], timeout_ms: [100,600000], first_day_of_week: [0,1], max_percent: [1,150], debounce_ms: [10,2000], scroll_step: [1,100], history_limit: [1,1000], default_expire_ms: [100,86400000], max_expire_ms: [100,86400000], reload_delay_ms: [50,10000]}
    let range = ranges[key]
    if (path.indexOf(".margins.") >= 0 || path === "ui.toast.margin") range = [0,4096]
    if (path === "modules.clipboard.behavior.popup_width") range = [280,1920]
    if (path === "modules.clipboard.behavior.popup_height") range = [320,2160]
    if (path.startsWith("modules.clipboard.behavior.")) {
        const clipboardRanges = {inline_image_height:[48,512], inline_text_lines:[2,64], preview_cache_items:[1,128], max_items:[1,1000], max_bytes:[1024,67108864], max_image_pixels:[1024,67108864], preview_image_size:[64,1024], preview_text_chars:[128,65536], cursor_timeout_ms:[50,3000], cursor_gap:[0,128]}
        if (clipboardRanges[key]) range = clipboardRanges[key]
    }
    if (path === "modules.media.behavior.artwork_height") range = [80,720]
    if (path === "modules.tray.behavior.menu_width") range = [160,1920]
    if (path === "modules.tray.behavior.menu_height") range = [100,2160]
    if (path === "ui.toast.width") range = [240,1920]
    if (path === "ui.toast.height") range = [80,1080]
    if (path === "ui.toast.duration_ms") range = [100,600000]
    if (path === "ui.toast.padding") range = [0,64]
    if (path === "ui.toast.spacing") range = [0,32]
    if (path === "ui.toast.body_lines") range = [1,8]
    if (key === "icon_max_pixels") range = [1024,16777216]
    if (key === "icon_cache_kib") range = [64,65536]
    if (key === "spacer_size") range = [0,4096]
    if (path === "settings.width") range = [400,7680]
    if (path === "settings.height") range = [300,4320]
    if (path.startsWith("ui.osd.")) {
        const osdRanges = {width:[160,1920], height:[60,1080], margin_bottom:[0,4096], margin_horizontal:[0,4096], duration_ms:[100,600000], padding:[0,64], spacing:[0,32], bar_height:[1,32]}
        if (osdRanges[key]) range = osdRanges[key]
    }
    if (path.startsWith("modules.brightness.behavior.screen.")) range = key === "min_percent" ? [0,100] : [1,100]
    if (path.startsWith("modules.brightness.behavior.keyboard.")) {
        if (key === "min_level") range = [0,2147483647]
        if (key === "max_level") range = [-1,2147483647]
        if (key === "step") range = [1,2147483647]
    }
    const color = /^ui\.osd\.(background|foreground|accent)$/.test(path) || path.indexOf("theme.palette.") === 0 || /\.style\.(foreground|background)$/.test(path)
    const kind = path === "theme.name" ? "theme" : options.length ? "enum" : color ? "color" : key === "font" || key === "editor_font" ? "font" : /^modules(_left|_center|_right)?$/.test(key) && path.indexOf("panels.") === 0 ? "modules" : typeof value === "boolean" ? "boolean" : typeof value === "number" ? "number" : Array.isArray(value) ? "argv" : "string"
    const help = {device: "Empty selects the first readable device by name. Keyboard names must end :kbd_backlight; caps/mute LEDs are excluded.", scroll_target: "Wheel on Brightness adjusts this kind; the popup always exposes both controls.", min_level: "Native keyboard LED level, not percent.", max_level: "-1 uses the hardware maximum. Other values are capped to the hardware maximum.", step: "Screen: percent points. Keyboard: whole native levels per wheel notch/button press.", set_command: "Brightness writer prefix. Appends --class backlight/leds --device observed-name set raw-level. Empty disables writes.", margin_bottom: "Lower-center distance from physical output bottom; no reserved space or input capture.", margin_horizontal: "Minimum horizontal screen-edge clearance.", volume_enabled: "OSD on observed audio percent or mute changes, not button presses. First baseline and reload are silent.", screen_enabled: "OSD on observed screen brightness changes, including external changes at the brightness polling interval.", keyboard_enabled: "OSD on observed keyboard backlight level changes at the brightness polling interval.",scroll_enabled: "Adjust volume by scrolling over the panel's Sound module without opening its popup.", scroll_step: "Percentage points per vertical mouse-wheel notch (1–100); limited by Max percent.", scroll_inverted: "Reverse the delivered wheel direction. Off: positive/up increases volume.", debounce_ms: "Coalesce volume input at this interval. Continuous dragging still sends the latest target, without overlapping commands.", backend: "Auto probes readable audio outputs, remembers the working backend, and falls back only for reads. PipeWire uses wpctl; PulseAudio uses pactl JSON.", pulse_info_command: "Read PulseAudio server/default-sink JSON. No sink or volume arguments are appended.", pulse_sinks_command: "Read PulseAudio sinks JSON, including per-channel volume and mute.", pulse_set_volume_command: "PulseAudio action prefix: Alure appends the observed sink index and volume percent. Empty disables adjustment.", pulse_mute_command: "PulseAudio action prefix: Alure appends the observed sink index and toggle. Empty disables mute control.", module_tooltips: "Show the full name and purpose when hovering palette and zone icons. Hidden while dragging.", focus_on_click: "Deliver the notification default action and focus its identifiable Niri sender window. Ambiguous matches do not focus unrelated windows.", niri_socket: "Empty uses NIRI_SOCKET. Applies only to notification click-to-focus.", window_gap: "Extra panel reservation. Negative values compensate Niri's own window gaps; popup_gap only controls dropdowns.", database_path: "Existing cliphist DB. Empty uses CLIPHIST_DB_PATH or XDG cache; no recording is started.", cursor_fallback: "Standalone cursor lookup timeout: center on configured output, or cancel.", persist_dnd: "Save dropdown DND changes to TOML so they survive a reload/restart. Off keeps the switch session-only.", preferred_player: "Empty chooses a playing player first. Or enter a service prefix, e.g. org.mpris.MediaPlayer2.musicfox.", artwork_remote: "Allow HTTP(S) album covers while the media dropdown is open.", active_indicator: "Workspace selection style. The workspace icon is shown once at the start of the module.", toggle_on_click: "Press the same module button again to close its dropdown.", layout: "Linear keeps one ordered strip; three-zone centers the middle group independently.", spacer_size: "Logical pixels for each fixed spacer. Flexible space shares the remaining room.", close_on_focus_loss: "Extra focus-loss dismissal. Native Wayland popups always dismiss on outside interaction.", popup_alignment: "Outer bounds aligned to the clicked item; visible card inset by popup gap.", popup_direction: "Inward opens away from the bar edge. Compositor may flip or slide to fit.", popup_gap: "Logical-pixel transparent padding on each side; capped to a quarter of the smaller output dimension.", opacity: "0 is transparent · 1 is opaque", length: "0 fills the output; otherwise logical pixels", exclusive_zone: "−1 reserves panel thickness · 0 reserves nothing", output: "primary, * for all outputs, or a screen name", icon_theme: "Empty uses the desktop default", close_shortcut: "Qt portable shortcut, for example Ctrl+W. Empty disables.", controls_style: "Requires restarting settings and panels", first_day_of_week: "0 = Sunday · 1 = Monday"}
    if (path.startsWith("modules.brightness.")) {
        help.scroll_enabled = "Adjust the selected brightness kind by scrolling over the panel module. Keyboard input accumulates full notches."
        help.scroll_inverted = "Reverse the delivered wheel direction. Off: positive/up increases brightness."
        help.debounce_ms = "Rate-limit one brightness command and one latest pending target across both kinds; continuous input is not postponed indefinitely."
        help.sysfs_path = "Absolute sysfs class directory. Alternative paths are for fixtures; the writer command must also target your fixture, not real hardware."
    }
    const group = path.startsWith("modules.brightness.behavior.screen.") ? "Display backlight" : path.startsWith("modules.brightness.behavior.keyboard.") ? "Keyboard backlight" : path.indexOf("theme.palette.") === 0 ? "Theme colors" : path.indexOf("ui.osd.") === 0 ? "Volume / brightness OSD" : path.indexOf("ui.toast.") === 0 ? "Notification banners" : path.indexOf(".margins.") >= 0 ? "Panel margins" : ""
    return {path: path, value: value, group: group, showGroup: false, label: path.startsWith("modules.brightness.") && key === "sysfs_path" ? "Backlight class directory" : label(key), kind: kind, options: options, low: range ? range[0] : 0, high: range ? range[1] : 1000000, step: range && range[2] ? range[2] : 1, help: color ? "Choose a color or enter #RRGGBB / #AARRGGBB; empty module colors inherit." : kind === "argv" ? "Enter a command, with quotes for arguments containing spaces. No TOML syntax; pipes require explicit sh -c." : help[key] || (kind === "number" ? (key.endsWith("_ms") ? "Milliseconds" : key.indexOf("size") >= 0 || key.indexOf("width") >= 0 || key.indexOf("height") >= 0 || ["spacing", "padding", "radius", "thickness"].indexOf(key) >= 0 || path.indexOf(".margins.") >= 0 ? "Logical pixels" : "") : "")}
}
function fields(map, prefix) {
    // QVariantList sequences are not JavaScript Arrays; normalize this read-only view.
    const result = Ui.fields(JSON.parse(JSON.stringify(map || {})), prefix).map(f => describe(f.path, f.value))
    const order = ["theme.name", "theme.font", "theme.font_size", "theme.opacity", "theme.spacing", "theme.padding", "theme.radius", "theme.icon_size", "theme.icon_mode", "theme.icon_theme"]
    // Full-path ties keep nested groups contiguous even with an unstable JS sort.
    result.sort((a,b) => {
        const priority = (order.indexOf(a.path) < 0 ? 100 : order.indexOf(a.path)) - (order.indexOf(b.path) < 0 ? 100 : order.indexOf(b.path))
        return priority || (a.path < b.path ? -1 : a.path > b.path ? 1 : 0)
    })
    result.forEach((field, index) => { field.showGroup = field.group.length > 0 && (index === 0 || result[index - 1].group !== field.group) })
    return result
}

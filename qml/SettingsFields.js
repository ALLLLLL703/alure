.pragma library
.import "Ui.js" as Ui

function label(key) {
    const names = {name: "Theme", font: "Font family", font_size: "Font size", icon_mode: "Icon source", icon_size: "Icon size", icon_theme: "Desktop icon theme", opacity: "Opacity", exclusive_zone: "Reserved screen space", sysfs_path: "Power supply directory", socket_path: "Niri socket", ordering: "Workspace order", id: "Panel name", output: "Output", format: "Label format", command: "Read command (advanced argv)", close_shortcut: "Close keyboard shortcut"}
    return names[key] || key.replace(/_/g, " ").replace(/^./, c => c.toUpperCase())
}
function describe(path, value) {
    const key = path.split(".").pop()
    let options = []
    if (path === "theme.name") options = ["midnight", "dawn", "forest"]
    if (key === "icon_mode") options = ["builtin", "theme"]
    if (key === "controls_style") options = ["Basic", "Fusion"]
    if (key === "edge") options = path.indexOf("ui.toast") === 0 ? ["top", "bottom"] : ["top", "bottom", "left", "right"]
    if (key === "layer") options = ["background", "bottom", "top", "overlay"]
    if (key === "ordering") options = ["output-index", "provider"]
    const ranges = {font_size: [6,72], editor_font_size: [6,72], spacing: [0,128], padding: [0,128], radius: [0,128], border_width: [0,16], opacity: [0,1,0.01], icon_size: [8,128], panel_padding: [0,128], module_height: [16,256], popup_width: [240,1920], popup_height: [240,2160], popup_gap: [0,256], animation_ms: [0,2000], thickness: [16,512], length: [0,32768], exclusive_zone: [-1,32768], min_width: [16,1024], max_width: [16,2048], interval_ms: [100,86400000], timeout_ms: [100,600000], first_day_of_week: [0,1], max_percent: [1,150], debounce_ms: [10,2000], history_limit: [1,1000], default_expire_ms: [100,86400000], max_expire_ms: [100,86400000], reload_delay_ms: [50,10000]}
    let range = ranges[key]
    if (path.indexOf(".margins.") >= 0 || path === "ui.toast.margin") range = [0,4096]
    if (path === "ui.toast.width") range = [240,1920]
    if (path === "ui.toast.height") range = [80,1080]
    if (path === "ui.toast.duration_ms") range = [100,600000]
    if (path === "settings.width") range = [400,7680]
    if (path === "settings.height") range = [300,4320]
    const color = path.indexOf("theme.palette.") === 0 || /\.style\.(foreground|background)$/.test(path)
    const kind = path === "theme.name" ? "theme" : options.length ? "enum" : color ? "color" : key === "font" || key === "editor_font" ? "font" : key === "modules" && path.indexOf("panels.") === 0 ? "modules" : typeof value === "boolean" ? "boolean" : typeof value === "number" ? "number" : Array.isArray(value) ? "argv" : "string"
    const help = {opacity: "0 is transparent · 1 is opaque", length: "0 fills the output; otherwise logical pixels", exclusive_zone: "−1 reserves panel thickness · 0 reserves nothing", output: "primary, * for all outputs, or a screen name", icon_theme: "Empty uses the desktop default", close_shortcut: "Qt portable shortcut, for example Ctrl+W. Empty disables.", controls_style: "Requires restarting settings and panels", first_day_of_week: "0 = Sunday · 1 = Monday"}
    const group = path.indexOf("theme.palette.") === 0 ? "Theme colors" : path.indexOf("ui.toast.") === 0 ? "Notification banners" : path.indexOf(".margins.") >= 0 ? "Panel margins" : ""
    return {path: path, value: value, group: group, showGroup: false, label: label(key), kind: kind, options: options, low: range ? range[0] : 0, high: range ? range[1] : 1000000, step: range && range[2] ? range[2] : 1, help: color ? "Choose a color or enter #RRGGBB / #AARRGGBB; empty module colors inherit." : kind === "argv" ? "Advanced TOML argv array; arguments are never shell text." : help[key] || (kind === "number" ? (key.endsWith("_ms") ? "Milliseconds" : key.indexOf("size") >= 0 || key.indexOf("width") >= 0 || key.indexOf("height") >= 0 || ["spacing", "padding", "radius", "thickness"].indexOf(key) >= 0 || path.indexOf(".margins.") >= 0 ? "Logical pixels" : "") : "")}
}
function fields(map, prefix) {
    // QVariantList sequences are not JavaScript Arrays; normalize this read-only view.
    const result = Ui.fields(JSON.parse(JSON.stringify(map || {})), prefix).map(f => describe(f.path, f.value))
    const order = ["theme.name", "theme.font", "theme.font_size", "theme.opacity", "theme.spacing", "theme.padding", "theme.radius", "theme.icon_size", "theme.icon_mode", "theme.icon_theme"]
    result.sort((a,b) => (order.indexOf(a.path) < 0 ? 100 : order.indexOf(a.path)) - (order.indexOf(b.path) < 0 ? 100 : order.indexOf(b.path)))
    result.forEach((field, index) => { field.showGroup = field.group.length > 0 && (index === 0 || result[index - 1].group !== field.group) })
    return result
}

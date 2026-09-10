.pragma library
function title(name) {
    const names = {workspaces: "Workspaces", media: "Now playing", tray: "System tray", volume: "Sound", updates: "Updates", wifi: "Wi-Fi", bluetooth: "Bluetooth", notifications: "Notifications", calendar: "Calendar", battery: "Battery"}
    return names[name] || name
}
function format(pattern, data) {
    return pattern.replace(/\{([A-Za-z_]+)\}/g, function(match, key) {
        const value = data[key]
        return value === undefined || value === null ? "—" : Array.isArray(value) ? value.join(", ") : String(value)
    })
}
function values(name, state, items) {
    const result = Object.assign({}, state)
    if (name === "media" && items.length) Object.assign(result, items[0])
    if (name === "bluetooth") result.status = state.connectedCount ? state.connectedCount + " connected" : "Ready"
    if (name === "wifi" && !state.connected) result.ssid = state.powered ? "Not connected" : "Radio off"
    return result
}
function literal(value) {
    if (typeof value === "string") return JSON.stringify(value)
    if (Array.isArray(value)) return "[" + value.map(literal).join(", ") + "]"
    if (typeof value === "object" && value !== null) return "{ " + Object.keys(value).map(k => JSON.stringify(k) + " = " + literal(value[k])).join(", ") + " }"
    return String(value)
}
function fields(map, prefix) {
    let result = []
    Object.keys(map || {}).sort().forEach(function(key) {
        const value = map[key], path = prefix ? prefix + "." + key : key
        if (typeof value === "object" && value !== null && !Array.isArray(value)) result = result.concat(fields(value, path))
        else result.push({path: path, value: value, literal: literal(value), boolean: typeof value === "boolean"})
    })
    return result
}
function monthCells(year, month, firstDay) {
    const shift = (new Date(year, month, 1).getDay() - firstDay + 7) % 7
    let cells = []
    for (let i = 0; i < 42; ++i) cells.push(new Date(year, month, i - shift + 1, 12))
    return cells
}
function sameDay(a, b) { return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate() }

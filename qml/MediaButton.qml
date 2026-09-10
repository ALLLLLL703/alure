import QtQuick
import QtQuick.Shapes

ShellButton {
    id: control
    required property string symbol
    property real glyphSize: Config.model.modules.media.behavior.control_icon_size
    implicitWidth: Config.model.modules.media.behavior.control_size
    implicitHeight: implicitWidth
    contentItem: Item {
        opacity: control.enabled ? 1 : 0.45
        Shape {
            objectName: control.objectName + "-glyph"
            anchors.centerIn: parent
            width: 24; height: 24
            scale: Math.min(control.glyphSize, control.availableWidth, control.availableHeight) / 24
            ShapePath {
                strokeColor: control.accent ? control.theme.palette.accent : control.foreground
                fillColor: ["play", "previous", "next"].includes(control.symbol) ? strokeColor : "transparent"
                strokeWidth: control.symbol === "pause" ? 3 : 1.8
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: ({
                        play: "M7 4 L20 12 L7 20 Z",
                        pause: "M8 5 V19 M16 5 V19",
                        previous: "M5 5 V19 M19 5 L8 12 L19 19 Z",
                        next: "M19 5 V19 M5 5 L16 12 L5 19 Z",
                        shuffle: "M3 6 H5 C10 6 14 18 19 18 H21 M18 15 L21 18 L18 21 M3 18 H5 C7 18 8 16 9 14 M15 8 C17 6 18 6 21 6 M18 3 L21 6 L18 9",
                        repeat: "M4 9 V7 Q4 5 6 5 H20 M17 2 L20 5 L17 8 M20 15 V17 Q20 19 18 19 H4 M7 16 L4 19 L7 22",
                        "repeat-one": "M4 9 V7 Q4 5 6 5 H20 M17 2 L20 5 L17 8 M20 15 V17 Q20 19 18 19 H4 M7 16 L4 19 L7 22 M10 10 L12 9 V15 M10 15 H14"
                    })[control.symbol] || ""
                }
            }
        }
    }
}

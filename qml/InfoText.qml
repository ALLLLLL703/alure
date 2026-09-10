import QtQuick
import QtQuick.Controls

Label {
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
    color: Config.model.theme.palette.foreground
    font.family: Config.model.theme.font
    font.pixelSize: Config.model.theme.font_size
}

import QtQuick

FocusScope {
    id: root
    required property string moduleName
    required property int popupPadding
    Loader {
        anchors.fill: parent
        anchors.margins: root.popupPadding
        sourceComponent: root.moduleName === "media" ? media : root.moduleName === "clipboard" ? clipboard : details
        focus: true
    }
    Component { id: media; MediaPopup { focus: true } }
    Component { id: clipboard; ClipboardPopup { focus: true } }
    Component { id: details; Popup { moduleName: root.moduleName; focus: true } }
}

import QtQuick

FocusScope {
    id: root
    required property string moduleName
    required property int popupPadding
    Popup {
        anchors.fill: parent
        anchors.margins: root.popupPadding
        moduleName: root.moduleName
        focus: true
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import FrameTrellis

// Text button in three flavours: normal, primary (accent) and destructive.
Button {
    id: control

    property bool primary: false
    property bool destructive: false
    property string iconName

    implicitHeight: 32
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: Theme.fontSize
    icon.source: iconName.length > 0 ? "qrc:/qt/qml/FrameTrellis/icons/" + iconName + ".svg" : ""
    icon.width: 16
    icon.height: 16
    icon.color: label.color
    Accessible.name: text

    contentItem: Row {
        spacing: 6
        IconImage {
            anchors.verticalCenter: parent.verticalCenter
            source: control.icon.source
            sourceSize.width: 16
            sourceSize.height: 16
            color: label.color
            visible: control.iconName.length > 0
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: !control.enabled ? Theme.textFaint
                   : control.primary ? Theme.accentText
                   : control.destructive ? Theme.danger : Theme.text
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: {
            if (!control.enabled)
                return Theme.raised
            if (control.primary)
                return control.down ? Theme.accentStrong : (control.hovered ? Theme.accentStrong : Theme.accent)
            return control.down ? Theme.pressed : (control.hovered ? Theme.hover : Theme.raised)
        }
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.focus : (control.primary ? "transparent" : Theme.border)
    }
}

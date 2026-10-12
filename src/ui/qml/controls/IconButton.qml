// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import FrameTrellis

// Compact tool button with an icon, tooltip, accessible name and visible keyboard focus.
ToolButton {
    id: control

    property string iconName
    property string tip
    property bool active: false
    property color tint: active ? Theme.accent : (enabled ? Theme.text : Theme.textFaint)

    implicitWidth: text.length > 0 ? contentItem.implicitWidth + leftPadding + rightPadding : 32
    implicitHeight: 32
    leftPadding: text.length > 0 ? 8 : 6
    rightPadding: text.length > 0 ? 10 : 6
    padding: 6
    focusPolicy: Qt.TabFocus
    display: text.length > 0 ? AbstractButton.TextBesideIcon : AbstractButton.IconOnly
    icon.source: iconName.length > 0 ? "qrc:/qt/qml/FrameTrellis/icons/" + iconName + ".svg" : ""
    icon.width: 18
    icon.height: 18
    icon.color: tint

    Accessible.name: tip.length > 0 ? tip : text
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip

    contentItem: Row {
        spacing: 6
        IconImage {
            anchors.verticalCenter: parent.verticalCenter
            source: control.icon.source
            sourceSize.width: control.icon.width
            sourceSize.height: control.icon.height
            color: control.icon.color
            visible: control.icon.source.toString().length > 0
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            visible: control.text.length > 0
            color: control.tint
            font.pixelSize: Theme.fontSize
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down ? Theme.pressed : (control.hovered || control.active ? Theme.hover : "transparent")
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.focus
    }
}

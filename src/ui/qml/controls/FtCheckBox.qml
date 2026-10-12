// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import FrameTrellis

CheckBox {
    id: box
    spacing: 6
    font.pixelSize: Theme.fontSize
    indicator: Rectangle {
        x: box.leftPadding
        y: box.topPadding + (box.availableHeight - height) / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 3
        color: box.checked ? Theme.accent : Theme.panelAlt
        border.width: box.visualFocus ? 2 : 1
        border.color: box.visualFocus ? Theme.focus : (box.checked ? Theme.accent : Theme.borderStrong)
        IconImage {
            anchors.centerIn: parent
            visible: box.checked
            source: "qrc:/qt/qml/FrameTrellis/icons/check.svg"
            sourceSize.width: 12
            sourceSize.height: 12
            color: Theme.accentText
        }
    }
    contentItem: Text {
        leftPadding: box.indicator.width + box.spacing
        text: box.text
        font: box.font
        color: box.enabled ? Theme.text : Theme.textFaint
        verticalAlignment: Text.AlignVCenter
    }
}

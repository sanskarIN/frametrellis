// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import FrameTrellis

// Friendly placeholder for empty panels with an optional call to action.
ColumnLayout {
    id: state

    property string iconName: "info"
    property string title
    property string message
    property string actionText
    signal actionTriggered()

    spacing: Theme.spacingS
    width: Math.min(parent ? parent.width - 2 * Theme.spacingL : 320, 340)

    IconImage {
        Layout.alignment: Qt.AlignHCenter
        source: "qrc:/qt/qml/FrameTrellis/icons/" + state.iconName + ".svg"
        sourceSize.width: 36
        sourceSize.height: 36
        color: Theme.textFaint
    }
    Text {
        Layout.fillWidth: true
        text: state.title
        color: Theme.text
        font.pixelSize: Theme.fontSizeLarge
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        text: state.message
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: text.length > 0
    }
    FtButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.spacingS
        primary: true
        text: state.actionText
        visible: state.actionText.length > 0
        onClicked: state.actionTriggered()
    }
}

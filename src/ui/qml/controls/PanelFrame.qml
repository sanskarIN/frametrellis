// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Layouts
import FrameTrellis

// A titled panel. Toolbar items placed in "tools" appear on the right of the header.
Rectangle {
    id: frame

    property string title
    property alias tools: toolRow.data
    default property alias content: body.data

    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: Theme.panelAlt
            radius: Theme.radius
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.radius; color: parent.color }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingXs
                spacing: Theme.spacingXs
                Text {
                    text: frame.title
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }
                Row {
                    id: toolRow
                    spacing: 2
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}

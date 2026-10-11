// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import FrameTrellis

// First-run guide shown once.
FtDialog {
    id: dialog

    required property AppController app
    signal importRequested()
    implicitWidth: 600
    closePolicy: Popup.CloseOnEscape
    onClosed: app.finishFirstRun()

    contentItem: ColumnLayout {
        spacing: Theme.spacingL
        RowLayout {
            spacing: Theme.spacingM
            Image { source: "qrc:/qt/qml/FrameTrellis/icons/app-icon.svg"; sourceSize.width: 48; sourceSize.height: 48 }
            ColumnLayout {
                spacing: 2
                Text { text: qsTr("Welcome to FrameTrellis"); color: Theme.text; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.Bold }
                Text { text: qsTr("Everything stays on your computer. No account needed."); color: Theme.textMuted; font.pixelSize: Theme.fontSize }
            }
        }
        RowLayout {
            spacing: Theme.spacingM
            Repeater {
                model: [
                    ["import", qsTr("1. Import"), qsTr("Add video, audio and images to the media bin. Files are referenced, never copied or changed.")],
                    ["split", qsTr("2. Edit"), qsTr("Drag media onto tracks, then move, trim and split clips. Every edit can be undone.")],
                    ["export", qsTr("3. Export"), qsTr("Render an MP4 (H.264 + AAC) in the background while you keep working.")]
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    radius: Theme.radius
                    color: Theme.panelAlt
                    border.color: Theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        spacing: Theme.spacingS
                        IconImage { source: "qrc:/qt/qml/FrameTrellis/icons/" + modelData[0] + ".svg"; sourceSize.width: 24; sourceSize.height: 24; color: Theme.accent }
                        Text { text: modelData[1]; color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold }
                        Text { text: modelData[2]; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Explore first"); onClicked: dialog.close() }
        FtButton {
            text: qsTr("Import media")
            primary: true
            iconName: "import"
            onClicked: { dialog.close(); dialog.importRequested() }
        }
    }
}

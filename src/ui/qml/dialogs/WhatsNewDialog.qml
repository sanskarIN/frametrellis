// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Bundled release highlights: always readable offline, with no startup popup or network request.
// Update the 1.2.3 highlights alongside the verified release notes before publishing the tag.
FtDialog {
    id: dialog

    required property AppController app
    readonly property string shownVersion: app.version
    readonly property bool showing123: shownVersion === "1.2.3"

    title: qsTr("What's New")
    implicitWidth: 620
    closePolicy: Popup.CloseOnEscape

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        Text {
            text: qsTr("Release highlights")
            color: Theme.text
            font.pixelSize: Theme.fontSizeTitle
            font.weight: Font.Bold
            Accessible.role: Accessible.Heading
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("See the improvements shipped with FrameTrellis. These notes are available offline.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize
            wrapMode: Text.WordWrap
        }

        Rectangle {
            visible: dialog.showing123
            Layout.fillWidth: true
            implicitHeight: latestContent.implicitHeight + Theme.spacingL * 2
            radius: Theme.radius
            color: Theme.panelAlt
            border.color: Theme.border

            ColumnLayout {
                id: latestContent
                anchors.fill: parent
                anchors.margins: Theme.spacingL
                spacing: Theme.spacingS
                Text {
                    text: qsTr("New in 1.2.3")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Open Help → What's New to review recent improvements without leaving the editor.")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: previousContent.implicitHeight + Theme.spacingL * 2
            radius: Theme.radius
            color: Theme.panelAlt
            border.color: Theme.border

            ColumnLayout {
                id: previousContent
                anchors.fill: parent
                anchors.margins: Theme.spacingL
                spacing: Theme.spacingM

                Text {
                    text: dialog.showing123 ? qsTr("Previously in 1.2.2") : qsTr("Highlights from 1.2.2")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: [
                        qsTr("Quick H.264 project-size and up-to-1080p export profiles."),
                        qsTr("Turbo up-to-720p preview encoding and an option to avoid upscaling."),
                        qsTr("Live render-speed feedback and output-size verification.")
                    ]
                    delegate: RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: Theme.spacingS
                        Text {
                            text: "•"
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeLarge
                            Layout.alignment: Qt.AlignTop
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontSize
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Full release notes and known limitations are available on GitHub.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
    }

    footer: DialogButtons {
        FtButton {
            text: qsTr("Release notes")
            onClicked: dialog.app.openUrl("https://github.com/sanskarIN/frametrellis/releases")
        }
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Close"); primary: true; onClicked: dialog.close() }
    }
}

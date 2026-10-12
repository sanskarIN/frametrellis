// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog

    required property AppController app
    implicitWidth: 520

    contentItem: ColumnLayout {
        spacing: Theme.spacingM
        RowLayout {
            spacing: Theme.spacingM
            Image {
                source: "qrc:/qt/qml/FrameTrellis/icons/app-icon.svg"
                sourceSize.width: 64
                sourceSize.height: 64
                Accessible.name: qsTr("FrameTrellis logo")
            }
            ColumnLayout {
                spacing: 2
                Text { text: "FrameTrellis"; color: Theme.text; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.Bold }
                Text { text: qsTr("Version %1").arg(dialog.app.version); color: Theme.textMuted; font.pixelSize: Theme.fontSize }
                Text {
                    text: qsTr("A local-first, open-source desktop video editor for precise, multi-track editing.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Copyright © 2026 Sanskar and FrameTrellis contributors. Free software under the GNU General Public License, version 3 or later. It comes with ABSOLUTELY NO WARRANTY. FrameTrellis uses Qt, MLT and FFmpeg under their own licenses; see THIRD_PARTY_NOTICES.md.")
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
        GridLayout {
            columns: 2
            columnSpacing: Theme.spacingM
            rowSpacing: 2
            Repeater {
                model: [
                    [qsTr("Qt"), dialog.app.about.qt],
                    [qsTr("MLT"), dialog.app.about.mlt],
                    [qsTr("Audio output"), dialog.app.about.audio],
                    [qsTr("System"), dialog.app.about.platform]
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.columnSpan: 2
                    Text { text: modelData[0]; color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall; Layout.preferredWidth: 100 }
                    Text { text: modelData[1]; color: Theme.text; font.pixelSize: Theme.fontSizeSmall }
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            Repeater {
                model: dialog.app.links
                delegate: FtButton {
                    required property var modelData
                    text: modelData.name
                    onClicked: dialog.app.openUrl(modelData.url)
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.url
                }
            }
        }
    }

    footer: DialogButtons {
        FtButton {
            text: qsTr("Copy diagnostics")
            onClicked: { dialog.app.copyText(dialog.app.diagnostics()); dialog.app.showStatus(qsTr("Diagnostics copied to the clipboard.")) }
        }
        FtButton { text: qsTr("Open log folder"); onClicked: dialog.app.openLogFolder() }
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Close"); primary: true; onClicked: dialog.close() }
    }
}

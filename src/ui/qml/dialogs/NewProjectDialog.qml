// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog

    required property AppController app
    title: qsTr("New project")
    implicitWidth: 460

    onOpened: { nameField.text = qsTr("Untitled"); nameField.selectAll(); nameField.forceActiveFocus() }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS
        Label { text: qsTr("Name"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall }
        TextField {
            id: nameField
            Layout.fillWidth: true
            maximumLength: 120
            Accessible.name: qsTr("Project name")
            onAccepted: createButton.clicked()
        }
        Label { text: qsTr("Format"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; Layout.topMargin: Theme.spacingS }
        ComboBox {
            id: preset
            Layout.fillWidth: true
            model: dialog.app.project.presets
            textRole: "name"
            Accessible.name: qsTr("Sequence format")
        }
        Text {
            text: preset.currentIndex >= 0 ? dialog.app.project.presets[preset.currentIndex].summary : ""
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingS
            text: qsTr("The frame rate is fixed once clips are on the timeline; the resolution can be changed later when exporting.")
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Cancel"); onClicked: dialog.close() }
        FtButton {
            id: createButton
            text: qsTr("Create")
            primary: true
            onClicked: { dialog.app.project.newProject(preset.currentIndex, nameField.text); dialog.close() }
        }
    }
}

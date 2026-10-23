// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog
    required property AppController app

    title: qsTr("Rename project")
    implicitWidth: 440

    function applyRename() {
        if (dialog.app.project.renameProject(nameField.text))
            dialog.close()
    }

    onOpened: {
        nameField.text = dialog.app.project.name
        nameField.selectAll()
        nameField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS
        Label {
            text: qsTr("Project name")
            color: Theme.textMuted
        }
        TextField {
            id: nameField
            objectName: "renameProjectField"
            Layout.fillWidth: true
            maximumLength: 120
            Accessible.name: qsTr("New project name")
            onAccepted: dialog.applyRename()
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            text: qsTr("The project name changes in saved project metadata. The existing file on disk is not renamed. You can undo this change.")
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Cancel"); onClicked: dialog.close() }
        FtButton {
            text: qsTr("Rename")
            primary: true
            enabled: nameField.text.trim().length > 0
            onClicked: dialog.applyRename()
        }
    }
}

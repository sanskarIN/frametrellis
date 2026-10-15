// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Basic raster titles are transparent PNG assets stored beside a saved project.
FtDialog {
    id: dialog
    required property AppController app
    readonly property var titleColors: ["#ffffff", "#fff18c", "#87eaff", "#ffc58f"]

    title: qsTr("Create title overlay")
    implicitWidth: 500

    onOpened: {
        caption.text = ""
        caption.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Label { text: qsTr("Title text"); color: Theme.textMuted }
        TextField {
            id: caption
            objectName: "titleTextField"
            Layout.fillWidth: true
            placeholderText: qsTr("Enter a short title")
            maximumLength: 200
            Accessible.name: qsTr("Title text")
            onAccepted: if (addButton.enabled) addButton.clicked()
        }

        Label {
            Layout.topMargin: Theme.spacingS
            text: qsTr("Font size (pixels)")
            color: Theme.textMuted
        }
        SpinBox {
            id: fontSize
            from: 16
            to: 240
            value: 88
            editable: true
            Accessible.name: qsTr("Title font size")
        }

        Label { text: qsTr("Text colour"); color: Theme.textMuted }
        RowLayout {
            spacing: Theme.spacingS
            ComboBox {
                id: titleColor
                model: [qsTr("White"), qsTr("Soft yellow"), qsTr("Cyan"), qsTr("Peach")]
                Accessible.name: qsTr("Title text colour")
            }
            Rectangle {
                width: 26
                height: 26
                radius: 4
                color: dialog.titleColors[titleColor.currentIndex]
                border.color: Theme.borderStrong
                Accessible.name: qsTr("Selected text colour")
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingS
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            text: !dialog.app.project.hasFilePath
                  ? qsTr("Save this project first to create a title image beside the project file.")
                  : !dialog.app.timeline.canAppendTitle
                    ? qsTr("Unlock or add a video track before creating a title. No image will be created until a track is available.")
                    : qsTr("A transparent PNG is saved in the “FrameTrellis Titles” folder alongside your project and added to the highest unlocked video track. Move or trim it like any image clip.")
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Cancel"); onClicked: dialog.close() }
        FtButton {
            id: addButton
            objectName: "createTitleButton"
            text: qsTr("Create title")
            primary: true
            enabled: dialog.app.project.hasFilePath && dialog.app.timeline.canAppendTitle
                     && caption.text.trim().length > 0
            onClicked: {
                const assetId = dialog.app.media.createTitle(caption.text, fontSize.value,
                                                               dialog.titleColors[titleColor.currentIndex])
                if (assetId.length > 0 && dialog.app.timeline.appendTitleAsset(assetId))
                    dialog.close()
            }
        }
    }
}

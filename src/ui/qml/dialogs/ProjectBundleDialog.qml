// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog
    required property AppController app
    readonly property ProjectBundleController bundles: app.bundles
    readonly property var state: bundles.progress
    readonly property var outcome: bundles.result
    property string parentFolder: ""
    property string verifyFolder: ""
    title: qsTr("Portable project bundle")
    implicitWidth: 760
    height: Math.min(650, parent ? parent.height - 24 : 650)
    closePolicy: Popup.CloseOnEscape
    objectName: "projectBundleDialog"

    onOpened: {
        if (!parentFolder.length)
            parentFolder = app.project.projectFolder()
        if (!verifyFolder.length)
            verifyFolder = parentFolder
    }

    FolderDialog {
        id: destinationPicker
        title: qsTr("Select a parent folder for the new bundle")
        currentFolder: dialog.app.fileUrl(dialog.parentFolder)
        onAccepted: dialog.parentFolder = dialog.app.localPath(selectedFolder)
    }

    FolderDialog {
        id: verificationPicker
        title: qsTr("Select an existing bundle folder")
        currentFolder: dialog.app.fileUrl(dialog.verifyFolder)
        onAccepted: dialog.verifyFolder = dialog.app.localPath(selectedFolder)
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingM
        Text {
            Layout.fillWidth: true
            text: qsTr("Create an independent copy of the current editing state and its source media. The original project, media files and undo history are not changed.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            enabled: !dialog.bundles.busy
            TabButton { text: qsTr("Create bundle") }
            TabButton { text: qsTr("Verify bundle") }
        }
        StackLayout {
            Layout.fillWidth: true
            currentIndex: tabs.currentIndex
            enabled: !dialog.bundles.busy
            ColumnLayout {
                spacing: Theme.spacingS
                Label { text: qsTr("Parent folder"); color: Theme.textMuted }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        text: dialog.parentFolder
                        readOnly: true
                        Accessible.name: qsTr("Bundle parent folder")
                    }
                    FtButton { text: qsTr("Browse…"); onClicked: destinationPicker.open() }
                }
                Label { text: qsTr("New folder name"); color: Theme.textMuted }
                TextField {
                    id: folderName
                    objectName: "projectBundleName"
                    Layout.fillWidth: true
                    text: "FrameTrellis-bundle"
                    maximumLength: 80
                    validator: RegularExpressionValidator {
                        regularExpression: /^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$/
                    }
                    Accessible.name: qsTr("New bundle folder name")
                }
                CheckBox {
                    id: usedOnly
                    text: qsTr("Include only media used on the timeline")
                    checked: false
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("All imported media is copied by default. Used-only mode removes unused references from the new project only. Missing or changed included media stops the operation. Ensure enough disk space.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }
                FtButton {
                    text: qsTr("Create portable project")
                    objectName: "projectBundleCreate"
                    primary: true
                    enabled: folderName.acceptableInput && dialog.parentFolder.length > 0
                    onClicked: dialog.bundles.createBundle(dialog.parentFolder,
                                                          folderName.text, usedOnly.checked)
                }
            }
            ColumnLayout {
                spacing: Theme.spacingS
                Label { text: qsTr("Bundle folder"); color: Theme.textMuted }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        text: dialog.verifyFolder
                        Accessible.name: qsTr("Bundle folder to verify")
                    }
                    FtButton { text: qsTr("Browse…"); onClicked: verificationPicker.open() }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Verify the recorded SHA-256 checksums of the project document and media. Saving edits to the bundled project will normally change its project checksum.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }
                FtButton {
                    text: qsTr("Verify recorded files")
                    primary: true
                    enabled: dialog.verifyFolder.length > 0
                    onClicked: dialog.bundles.verifyBundle(dialog.verifyFolder)
                }
                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            visible: dialog.bundles.busy
            Layout.fillWidth: true
            implicitHeight: progressContent.implicitHeight + 24
            color: Theme.panelAlt
            border.color: Theme.border
            radius: Theme.radius
            ColumnLayout {
                id: progressContent
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                spacing: Theme.spacingS
                Text {
                    Layout.fillWidth: true
                    text: dialog.bundles.cancelling ? qsTr("Cancelling…")
                        : dialog.state.operation === "verify" ? qsTr("Verifying checksums…")
                        : dialog.state.phase === "copying" ? qsTr("Copying media…")
                        : dialog.state.phase === "finalizing" ? qsTr("Finalizing bundle…")
                        : qsTr("Checking source media…")
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: dialog.state.fraction || 0
                    indeterminate: dialog.state.operation === "verify"
                        || dialog.state.phase !== "copying"
                }
                Text {
                    Layout.fillWidth: true
                    visible: dialog.state.operation === "create"
                    text: qsTr("%1 / %2 files · %3 / %4")
                        .arg(dialog.state.filesCompleted || 0)
                        .arg(dialog.state.totalFiles || 0)
                        .arg(dialog.state.copied || "")
                        .arg(dialog.state.total || "")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        Rectangle {
            visible: !dialog.bundles.busy && !!dialog.outcome.message
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.panelAlt
            border.color: Theme.border
            radius: Theme.radius
            clip: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                spacing: Theme.spacingS
                Text {
                    Layout.fillWidth: true
                    text: dialog.outcome.message || ""
                    color: !dialog.outcome.success ? Theme.danger
                        : dialog.outcome.matches === false ? Theme.warning : Theme.success
                    font.weight: Font.DemiBold
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!dialog.outcome.directory
                    text: dialog.outcome.directory || ""
                    color: Theme.textMuted
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!dialog.outcome.size
                    text: qsTr("%1 media files · %2").arg(dialog.outcome.files || 0)
                        .arg(dialog.outcome.size || "")
                    color: Theme.textMuted
                }
                Text {
                    Layout.fillWidth: true
                    visible: dialog.outcome.operation === "verify" && dialog.outcome.success
                    text: dialog.outcome.projectMatches ? qsTr("Original project checksum matches.")
                        : qsTr("The project document differs from the original bundle.")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: (dialog.outcome.problems || []).slice(0, 100)
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Text {
                        required property string modelData
                        width: ListView.view.width
                        text: modelData
                        color: Theme.warning
                        font.pixelSize: Theme.fontSizeSmall
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: (dialog.outcome.problems || []).length > 100
                    text: qsTr("Showing the first 100 differing paths.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }
                FtButton {
                    visible: !!dialog.outcome.projectFile
                    text: qsTr("Use new bundle for verification")
                    onClicked: {
                        dialog.verifyFolder = dialog.outcome.directory
                        tabs.currentIndex = 1
                    }
                }
            }
        }
        Item {
            Layout.fillHeight: true
            visible: dialog.bundles.busy || !dialog.outcome.message
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton {
            text: qsTr("Cancel operation")
            visible: dialog.bundles.busy
            enabled: !dialog.bundles.cancelling
            destructive: true
            onClicked: dialog.bundles.cancel()
        }
        FtButton {
            text: dialog.bundles.busy ? qsTr("Hide") : qsTr("Close")
            onClicked: dialog.close()
        }
    }
}

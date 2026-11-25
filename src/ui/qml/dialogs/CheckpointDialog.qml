// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog
    required property AppController app
    property var history: ({})
    readonly property var entries: history.entries || []
    readonly property var selectedEntry: historyList.currentIndex >= 0
        && historyList.currentIndex < entries.length
        ? entries[historyList.currentIndex] : null
    readonly property bool validHistory: !history.error && !!history.projectId
    property string pendingDeleteId: ""
    property string pendingDeleteProjectId: ""
    signal restoreRequested(string projectId, string checkpointId)

    title: qsTr("Project checkpoints")
    implicitWidth: 780
    height: Math.min(650, parent ? parent.height - 24 : 650)
    closePolicy: Popup.CloseOnEscape
    objectName: "checkpointDialog"

    function refresh(preferredId) {
        const previous = preferredId || (selectedEntry ? selectedEntry.id : "")
        history = app.project.checkpointHistory()
        let index = -1
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].id === previous) {
                index = i
                break
            }
        }
        historyList.currentIndex = index >= 0 ? index : (entries.length ? 0 : -1)
    }

    function createVersion() {
        const label = checkpointName.text.trim()
        if (!validHistory || !label.length || entries.length >= history.limit || history.truncated)
            return
        if (app.project.createCheckpoint(label)) {
            checkpointName.clear()
            historyList.currentIndex = -1
            refresh("")
        }
    }

    function compareSelected() {
        if (!selectedEntry || !selectedEntry.readable)
            return
        comparisonDialog.showReport(
            app.project.compareCheckpoint(history.projectId, selectedEntry.id), selectedEntry.label)
    }

    function requestRestore() {
        if (!selectedEntry || !selectedEntry.readable)
            return
        const projectId = history.projectId
        const checkpointId = selectedEntry.id
        close()
        restoreRequested(projectId, checkpointId)
    }

    function requestDelete() {
        if (!selectedEntry)
            return
        pendingDeleteId = selectedEntry.id
        pendingDeleteProjectId = history.projectId
        deleteConfirm.message = qsTr("Delete checkpoint “%1”? This removes only the stored version, not your project or source media.")
            .arg(selectedEntry.label)
        deleteConfirm.open()
    }

    onOpened: {
        app.playback.pause()
        checkpointName.clear()
        historyList.currentIndex = -1
        refresh("")
        checkpointName.forceActiveFocus()
    }

    Connections {
        target: dialog.app.project
        function onDocumentReplaced() {
            if (dialog.visible)
                dialog.close()
        }
    }

    CheckpointCompareDialog { id: comparisonDialog }

    ConfirmDialog {
        id: deleteConfirm
        title: qsTr("Delete checkpoint?")
        acceptText: qsTr("Delete checkpoint")
        showDiscard: false
        objectName: "checkpointDeleteConfirmation"
        onSaveRequested: {
            const projectId = dialog.pendingDeleteProjectId
            const checkpointId = dialog.pendingDeleteId
            dialog.pendingDeleteId = ""
            dialog.pendingDeleteProjectId = ""
            if (dialog.app.project.removeCheckpoint(projectId, checkpointId))
                dialog.refresh("")
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS
        Text {
            Layout.fillWidth: true
            text: qsTr("Save named, local versions of “%1” before major edits. Checkpoints contain project instructions and media references, not copies of video, audio or images.")
                .arg(dialog.app.project.name)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: checkpointName
                objectName: "checkpointName"
                Layout.fillWidth: true
                maximumLength: 120
                placeholderText: qsTr("For example: Before colour grading")
                Accessible.name: qsTr("Checkpoint name")
                enabled: dialog.validHistory && !dialog.history.truncated
                    && dialog.entries.length < dialog.history.limit
                onAccepted: dialog.createVersion()
            }
            FtButton {
                objectName: "checkpointCreate"
                text: qsTr("Create checkpoint")
                primary: true
                enabled: checkpointName.enabled && checkpointName.text.trim().length > 0
                onClicked: dialog.createVersion()
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !!dialog.history.error || !!dialog.history.truncated
                || (dialog.validHistory && dialog.entries.length >= dialog.history.limit)
            text: dialog.history.error || (dialog.history.truncated
                ? qsTr("Too many history files were found. Only a bounded subset is shown. Remove unwanted checkpoints explicitly.")
                : qsTr("Checkpoint limit reached. Remove an unwanted version before creating another."))
            color: dialog.history.error ? Theme.danger : Theme.warning
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            textFormat: Text.PlainText
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Saved versions"); color: Theme.text }
            Item { Layout.fillWidth: true }
            Text {
                text: dialog.validHistory
                    ? qsTr("%1 / %2 versions").arg(dialog.entries.length).arg(dialog.history.limit) : ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
            FtButton {
                text: qsTr("Refresh")
                objectName: "checkpointRefresh"
                onClicked: dialog.refresh("")
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.panelAlt
            border.color: Theme.border
            radius: Theme.radius
            clip: true

            ListView {
                id: historyList
                objectName: "checkpointHistoryList"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                spacing: 1
                model: dialog.entries
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: versionDelegate
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    padding: Theme.spacingM
                    highlighted: historyList.currentIndex === versionDelegate.index
                    onClicked: historyList.currentIndex = versionDelegate.index
                    Accessible.name: versionDelegate.modelData.label || ""
                    background: Rectangle {
                        color: versionDelegate.highlighted ? Theme.raised
                            : versionDelegate.hovered ? Theme.hover : Theme.panelAlt
                        border.color: versionDelegate.activeFocus ? Theme.focus : Theme.border
                    }
                    contentItem: ColumnLayout {
                        spacing: Theme.spacingXs
                        Text {
                            Layout.fillWidth: true
                            text: versionDelegate.modelData.label || ""
                            color: versionDelegate.modelData.readable ? Theme.text : Theme.danger
                            font.weight: Font.DemiBold
                            font.pixelSize: Theme.fontSize
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%1 · %2 KiB").arg(versionDelegate.modelData.displayDate || "")
                                .arg(Math.ceil((versionDelegate.modelData.bytes || 0) / 1024))
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !!versionDelegate.modelData.error
                            text: versionDelegate.modelData.error || ""
                            color: Theme.danger
                            font.pixelSize: Theme.fontSizeSmall
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: historyList.count === 0
                text: qsTr("No checkpoints yet. Create one to preserve the current state.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !!dialog.selectedEntry && !!dialog.selectedEntry.originalPath
            text: qsTr("Original project: %1").arg(dialog.selectedEntry
                ? dialog.selectedEntry.originalPath : "")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Opening a checkpoint replaces the current editor session after the normal unsaved-changes prompt. The restored project is unsaved; choose Save As to keep it. No checkpoint is deleted automatically.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
    }
    footer: DialogButtons {
        FtButton {
            text: qsTr("Delete")
            objectName: "checkpointDelete"
            destructive: true
            enabled: !!dialog.selectedEntry
            onClicked: dialog.requestDelete()
        }
        FtButton {
            text: qsTr("Compare with current")
            objectName: "checkpointCompare"
            enabled: !!dialog.selectedEntry && dialog.selectedEntry.readable === true
            onClicked: dialog.compareSelected()
        }
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Close"); onClicked: dialog.close() }
        FtButton {
            text: qsTr("Open as unsaved version")
            objectName: "checkpointRestore"
            primary: true
            enabled: !!dialog.selectedEntry && dialog.selectedEntry.readable === true
            onClicked: dialog.requestRestore()
        }
    }
}

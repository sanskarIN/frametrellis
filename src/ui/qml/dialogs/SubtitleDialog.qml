// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog
    required property AppController app
    readonly property SubtitleController subtitles: app.subtitles
    property bool dirty: false
    property bool loading: false
    property var pendingAction: null
    signal importRequested()
    signal exportRequested()

    title: qsTr("Subtitles")
    implicitWidth: 1000
    height: Math.min(700, parent ? parent.height - 48 : 700)
    closePolicy: Popup.NoAutoClose

    function loadDraft() {
        loading = true
        subtitles.clearError()
        const cue = subtitles.selectedCue
        startField.text = cue.start || ""
        endField.text = cue.end || ""
        captionField.text = cue.text || ""
        dirty = false
        loading = false
    }
    function applyIfDirty() {
        if (!dirty)
            return true
        if (!subtitles.updateSelected(startField.text, endField.text, captionField.text))
            return false
        loadDraft()
        return true
    }
    function guarded(action) {
        if (!dirty) {
            action()
            return
        }
        pendingAction = action
        draftGuard.open()
    }
    function runPending() {
        const action = pendingAction
        pendingAction = null
        if (action)
            action()
    }
    function select(id) {
        if (id !== subtitles.selectedCue.id)
            guarded(() => subtitles.selectCue(id))
    }
    onOpened: { app.playback.pause(); loadDraft() }
    onClosed: pendingAction = null
    Connections {
        target: dialog.subtitles
        function onSelectedCueChanged() { if (dialog.visible) dialog.loadDraft() }
    }
    Shortcut {
        sequence: "Escape"
        enabled: dialog.visible && !draftGuard.visible
        onActivated: dialog.guarded(() => dialog.close())
    }
    Shortcut { sequence: "Ctrl+Return"; enabled: dialog.visible && !draftGuard.visible; onActivated: dialog.applyIfDirty() }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS
        RowLayout {
            Layout.fillWidth: true
            FtButton { text: qsTr("Import SRT…"); objectName: "subtitleImport"; onClicked: dialog.guarded(() => dialog.importRequested()) }
            FtButton { text: qsTr("Export SRT…"); enabled: dialog.subtitles.count > 0; onClicked: dialog.guarded(() => dialog.exportRequested()) }
            FtButton { text: qsTr("+ At playhead"); objectName: "subtitleAdd"; enabled: dialog.subtitles.canAdd; onClicked: dialog.guarded(() => dialog.subtitles.addAtPlayhead()) }
            Item { Layout.fillWidth: true }
            FtButton { text: qsTr("Undo"); objectName: "subtitleUndo"; enabled: dialog.app.project.canUndo; onClicked: dialog.guarded(() => dialog.app.project.undo()) }
            FtButton { text: qsTr("Redo"); enabled: dialog.app.project.canRedo; onClicked: dialog.guarded(() => dialog.app.project.redo()) }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Import appends cues. Times use HH:MM:SS,mmm; overlapping cues are refused. Subtitles stay at their sequence times when clips move.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            FtCheckBox {
                text: qsTr("Burn into preview and video")
                objectName: "subtitleBurnIn"
                checked: dialog.subtitles.burnIn
                onToggled: dialog.subtitles.setAppearance(checked, dialog.subtitles.fontPercent, dialog.subtitles.background)
            }
            Label { text: qsTr("Text size"); color: Theme.textMuted }
            SpinBox {
                objectName: "subtitleSize"
                from: 2; to: 10; value: dialog.subtitles.fontPercent
                Accessible.name: qsTr("Subtitle size as percent of video height")
                onValueModified: dialog.subtitles.setAppearance(dialog.subtitles.burnIn, value, dialog.subtitles.background)
            }
            Label { text: "%"; color: Theme.textMuted }
            FtCheckBox {
                text: qsTr("Dark background")
                checked: dialog.subtitles.background
                onToggled: dialog.subtitles.setAppearance(dialog.subtitles.burnIn, dialog.subtitles.fontPercent, checked)
            }
            Item { Layout.fillWidth: true }
            FtButton { text: qsTr("Reset style"); onClicked: dialog.subtitles.resetAppearance() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingM
            ColumnLayout {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                spacing: Theme.spacingS
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.panelAlt
                    border.color: Theme.border
                    radius: Theme.radius
                    ListView {
                        id: cueList
                        objectName: "subtitleList"
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        model: dialog.subtitles
                        currentIndex: dialog.subtitles.selectedIndex
                        keyNavigationEnabled: false
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        Keys.onUpPressed: dialog.guarded(() => dialog.subtitles.previousCue())
                        Keys.onDownPressed: dialog.guarded(() => dialog.subtitles.nextCue())
                        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                        delegate: Rectangle {
                            id: cueRow
                            required property string cueId
                            required property string startLabel
                            required property string endLabel
                            required property string caption
                            required property bool active
                            required property bool selected
                            required property bool outside
                            width: cueList.width
                            height: 70
                            color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14) : (rowMouse.containsMouse ? Theme.hover : "transparent")
                            Rectangle { width: 3; height: parent.height; color: Theme.accent; visible: cueRow.active }
                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.spacingS
                                spacing: 4
                                Text {
                                    width: parent.width
                                    text: cueRow.startLabel + " → " + cueRow.endLabel
                                    color: cueRow.selected ? Theme.accent : Theme.textMuted
                                    font.family: Theme.monoFamily
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                                Text {
                                    width: parent.width
                                    text: cueRow.caption.replace(/\n/g, " · ")
                                    textFormat: Text.PlainText
                                    color: cueRow.outside ? Theme.textMuted : Theme.text
                                    font.pixelSize: Theme.fontSize
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: cueRow.outside
                                    text: qsTr("No frame inside this video · kept for SRT")
                                    color: Theme.warning
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                            }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: { cueList.forceActiveFocus(); dialog.select(cueRow.cueId) }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 32
                            visible: dialog.subtitles.count === 0
                            text: qsTr("Import an SRT file, or pause in a free gap and add a subtitle at the playhead.")
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
                FrameView {
                    objectName: "subtitlePreview"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 170
                    visible: dialog.height >= 600
                    source: dialog.visible ? dialog.app.playback : null
                    backgroundColor: Theme.monitorBackground
                    Accessible.name: qsTr("Applied subtitle preview")
                }
                Label {
                    visible: dialog.height >= 600
                    text: qsTr("Applied preview · %1").arg(dialog.app.playback.timecode)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: dialog.subtitles.selectedIndex >= 0 ? qsTr("Cue %1 of %2").arg(dialog.subtitles.selectedIndex + 1).arg(dialog.subtitles.count) : qsTr("No cue selected")
                        color: Theme.text
                    }
                    FtButton { text: qsTr("Previous"); enabled: dialog.subtitles.selectedIndex > 0; onClicked: dialog.guarded(() => dialog.subtitles.previousCue()) }
                    FtButton { text: qsTr("Next"); enabled: dialog.subtitles.selectedIndex + 1 < dialog.subtitles.count; onClicked: dialog.guarded(() => dialog.subtitles.nextCue()) }
                }
                GridLayout {
                    columns: 3
                    Layout.fillWidth: true
                    enabled: dialog.subtitles.selectedIndex >= 0
                    Label { text: qsTr("Start"); color: Theme.textMuted }
                    TextField {
                        id: startField
                        objectName: "subtitleStart"
                        Layout.fillWidth: true
                        font.family: Theme.monoFamily
                        maximumLength: 13
                        Accessible.name: qsTr("Subtitle start time")
                        onTextEdited: if (!dialog.loading) dialog.dirty = true
                    }
                    FtButton { text: qsTr("Use playhead"); onClicked: { startField.text = dialog.subtitles.playheadTimestamp(); dialog.dirty = true } }
                    Label { text: qsTr("End"); color: Theme.textMuted }
                    TextField {
                        id: endField
                        objectName: "subtitleEnd"
                        Layout.fillWidth: true
                        font.family: Theme.monoFamily
                        maximumLength: 13
                        Accessible.name: qsTr("Subtitle end time (exclusive)")
                        onTextEdited: if (!dialog.loading) dialog.dirty = true
                    }
                    FtButton { text: qsTr("Use playhead"); onClicked: { endField.text = dialog.subtitles.playheadTimestamp(); dialog.dirty = true } }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        id: captionField
                        objectName: "subtitleText"
                        enabled: dialog.subtitles.selectedIndex >= 0
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTr("Subtitle text (plain text, up to 2,000 characters)")
                        selectByMouse: true
                        font.pixelSize: Theme.fontSize + 2
                        color: Theme.text
                        background: Rectangle { color: Theme.panelAlt; border.color: captionField.activeFocus ? Theme.focus : Theme.border; radius: Theme.radius }
                        Accessible.name: qsTr("Subtitle text")
                        onTextChanged: if (!dialog.loading) dialog.dirty = true
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    FtButton { text: qsTr("Preview cue"); enabled: dialog.subtitles.canSeek; onClicked: dialog.subtitles.seekSelected() }
                    Label { text: qsTr("%1 / 2,000").arg(captionField.length); color: captionField.length > 2000 ? Theme.danger : Theme.textMuted; font.pixelSize: Theme.fontSizeSmall }
                    Item { Layout.fillWidth: true }
                    FtButton { text: qsTr("Remove cue"); enabled: dialog.subtitles.selectedIndex >= 0; onClicked: dialog.guarded(() => dialog.subtitles.removeSelected()) }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.subtitles.error.length > 0
            text: dialog.subtitles.error
            textFormat: Text.PlainText
            color: Theme.danger
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Cues do not extend the video. SRT export keeps every cue. Install fonts for the languages you use; long text shrinks to fit the picture.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
        }
    }
    footer: DialogButtons {
        FtButton { text: qsTr("Clear track"); enabled: dialog.subtitles.count > 0; onClicked: dialog.guarded(() => dialog.subtitles.clearAll()) }
        Label { text: dialog.dirty ? qsTr("Unapplied changes") : qsTr("All cue changes applied"); color: dialog.dirty ? Theme.warning : Theme.textMuted; font.pixelSize: Theme.fontSizeSmall }
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Close"); objectName: "subtitleClose"; onClicked: dialog.guarded(() => dialog.close()) }
        FtButton { text: qsTr("Apply cue"); objectName: "subtitleApply"; primary: true; enabled: dialog.dirty; onClicked: dialog.applyIfDirty() }
    }
    ConfirmDialog {
        id: draftGuard
        objectName: "subtitleDraftGuard"
        title: qsTr("Unapplied subtitle changes")
        message: qsTr("Apply the changes to this cue before continuing?")
        acceptText: qsTr("Apply")
        discardText: qsTr("Discard changes")
        onSaveRequested: if (dialog.applyIfDirty()) dialog.runPending()
        onDiscardRequested: { dialog.loadDraft(); dialog.runPending() }
    }
}

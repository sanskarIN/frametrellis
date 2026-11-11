// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Independent, silent source monitor. Its position and marks do not affect the program playhead.
// Only Append Range, Place at Playhead, Ripple Insert, Place V+A and Replace selected modify the timeline.
// Match clip length and all monitor navigation affect source marks only.
PanelFrame {
    id: panel
    objectName: "sourceMonitorPanel"
    required property AppController app

    property string assetId: ""
    property string sourceName: ""
    property int maxFrames: 0
    property int previewFrame: 0
    property int pictureFrame: 0
    property int markInFrame: 0
    property int markOutFrame: 0
    property bool hasPicture: false
    property bool hasSourceAudio: false
    property string selectedTrackId: ""
    property var destinationTracks: []
    readonly property bool loaded: assetId.length > 0
    readonly property bool selectedTrackAvailable: selectedTrackId.length === 0
                                                   || destinationTracks.some(
                                                       track => track.id === selectedTrackId
                                                                && !track.locked)
    readonly property bool validRange: loaded && markOutFrame > markInFrame
                                       && markOutFrame <= maxFrames
    readonly property var selectedTimelineClip: app.timeline.selectedClip
    readonly property bool selectedClipCompatible: loaded && selectedTimelineClip.count === 1
                                                   && !selectedTimelineClip.locked
                                                   && (selectedTimelineClip.trackKind === "video"
                                                       ? hasPicture : hasSourceAudio)
    // The Out mark is exclusive, and the In mark always wins: never shift a
    // deliberately chosen source start to make a too-short remainder fit.
    readonly property bool canMatchClipLength: selectedClipCompatible
                                                && Number.isInteger(selectedTimelineClip.duration)
                                                && selectedTimelineClip.duration > 0
                                                && markInFrame >= 0
                                                && markInFrame <=
                                                   maxFrames - selectedTimelineClip.duration
    readonly property bool canReplaceSelected: validRange && canMatchClipLength
                                               && markOutFrame ===
                                                  markInFrame + selectedTimelineClip.duration

    title: loaded ? qsTr("Source — %1").arg(sourceName) : qsTr("Source Monitor")

    function refreshDestinationTracks() {
        const tracks = [{id: "", label: qsTr("Automatic (first free)"), locked: false}]
        if (loaded) {
            const available = app.timeline.sourceDestinationTracks(assetId)
            for (let i = 0; i < available.length; ++i)
                tracks.push(available[i])
        }
        // A removed or newly locked target must never silently redirect an edit.
        if (selectedTrackId.length > 0 && !tracks.some(track => track.id === selectedTrackId))
            tracks.push({id: selectedTrackId, label: qsTr("Selected track unavailable"),
                         locked: true})
        destinationTracks = tracks
    }

    function clear() {
        decodeTimer.stop()
        selectedTrackId = ""
        assetId = ""
        sourceName = ""
        maxFrames = 0
        previewFrame = 0
        pictureFrame = 0
        markInFrame = 0
        markOutFrame = 0
        hasPicture = false
        hasSourceAudio = false
        refreshDestinationTracks()
    }

    function openFor(id) {
        const facts = app.media.mediaProperties(id)
        const duration = Math.floor(app.timeline.assetDuration(id))
        if (!facts.assetId || (facts.kind !== "video" && facts.kind !== "audio")
                || facts.status === "Offline (missing)" || !Number.isFinite(duration)
                || duration < 1 || duration > 2147483647) {
            app.showStatus(qsTr("This source cannot be opened in the Source Monitor."))
            return false
        }
        decodeTimer.stop()
        assetId = id
        sourceName = facts.name
        maxFrames = duration
        hasPicture = facts.kind === "video"
        hasSourceAudio = facts.hasAudio === true
        markInFrame = 0
        markOutFrame = duration
        previewFrame = 0
        pictureFrame = 0
        selectedTrackId = ""
        refreshDestinationTracks()
        return true
    }

    function step(frames) {
        if (!loaded || frames === 0)
            return false
        const target = Math.max(0, Math.min(maxFrames - 1, previewFrame + frames))
        if (target === previewFrame)
            return false
        previewFrame = target
        return true
    }

    function markIn() {
        if (!loaded || previewFrame >= markOutFrame)
            return false
        markInFrame = previewFrame
        return true
    }

    function markOut() {
        if (!loaded || previewFrame < markInFrame)
            return false
        // The Out boundary is exclusive. The displayed picture itself is included.
        markOutFrame = previewFrame + 1
        return true
    }

    function resetMarks() {
        if (!loaded || (markInFrame === 0 && markOutFrame === maxFrames))
            return false
        markInFrame = 0
        markOutFrame = maxFrames
        return true
    }

    function appendRange() {
        if (!validRange || !selectedTrackAvailable)
            return false
        return selectedTrackId.length > 0
               ? app.timeline.appendAssetRangeOnTrack(
                     assetId, selectedTrackId, markInFrame, markOutFrame)
               : app.timeline.appendAssetRange(assetId, markInFrame, markOutFrame)
    }

    function placeAtPlayhead() {
        if (!validRange || !selectedTrackAvailable)
            return false
        return selectedTrackId.length > 0
               ? app.timeline.placeAssetRangeOnTrackAtPlayhead(
                     assetId, selectedTrackId, markInFrame, markOutFrame)
               : app.timeline.placeAssetRangeAtPlayhead(assetId, markInFrame, markOutFrame)
    }

    function rippleInsertRange() {
        return validRange && selectedTrackAvailable
               && app.timeline.rippleInsertAssetRangeAtPlayhead(
                      assetId, selectedTrackId, markInFrame, markOutFrame)
    }

    function placeVideoAndAudio() {
        return validRange && hasPicture && hasSourceAudio && selectedTrackAvailable
               && app.timeline.placeSynchronizedAssetRangeAtPlayhead(
                      assetId, selectedTrackId, markInFrame, markOutFrame)
    }
    function rippleInsertVideoAndAudio() {
        return validRange && hasPicture && hasSourceAudio && selectedTrackAvailable
               && app.timeline.rippleInsertSynchronizedAssetRangeAtPlayhead(
                      assetId, selectedTrackId, markInFrame, markOutFrame)
    }

    function matchSelectedClipLength() {
        if (!canMatchClipLength)
            return false
        const end = markInFrame + selectedTimelineClip.duration
        if (markOutFrame === end)
            return false
        markOutFrame = end
        return true
    }

    function replaceSelected() {
        return canReplaceSelected
               && app.timeline.replaceSelectionWithAssetRange(
                      assetId, markInFrame, markOutFrame)
    }

    // Clear the source on project replacement or removal, so stale asset IDs cannot
    // become an accidental edit in a different document.
    Connections {
        target: panel.app.project
        function onDocumentReplaced() { panel.clear() }
        function onProjectChanged() {
            if (panel.loaded && !panel.app.media.mediaProperties(panel.assetId).assetId)
                panel.clear()
            else if (panel.loaded)
                panel.refreshDestinationTracks()
        }
    }

    Timer {
        id: decodeTimer
        interval: 100
        repeat: false
        onTriggered: panel.pictureFrame = panel.previewFrame
    }
    onPreviewFrameChanged: {
        if (loaded)
            decodeTimer.restart()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle {
                anchors.fill: parent
                color: Theme.monitorBackground
                radius: Theme.radiusSmall
            }
            Image {
                id: sourceImage
                objectName: "sourceMonitorFrameImage"
                anchors.fill: parent
                anchors.margins: Theme.spacingS
                source: panel.loaded && panel.hasPicture
                        ? panel.app.media.sourcePreviewUrl(panel.assetId, panel.pictureFrame) : ""
                sourceSize.width: 640
                sourceSize.height: 360
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Source frame %1").arg(panel.pictureFrame)
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: sourceImage.status === Image.Loading
                visible: running
            }
            Label {
                anchors.centerIn: parent
                visible: !panel.loaded
                text: qsTr("Right-click a video or audio file in Media to open it here.")
                color: Theme.textMuted
                width: Math.max(0, parent.width - 32)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            Label {
                anchors.centerIn: parent
                visible: panel.loaded && !panel.hasPicture
                text: qsTr("Audio source — no picture")
                color: Theme.textMuted
            }
            Label {
                anchors.centerIn: parent
                visible: panel.loaded && panel.hasPicture && sourceImage.status === Image.Error
                text: qsTr("Could not decode this source frame.")
                color: Theme.danger
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingS
            Layout.rightMargin: Theme.spacingS
            spacing: Theme.spacingS
            IconButton {
                objectName: "sourceMonitorFirst"
                iconName: "skip-start"
                tip: qsTr("First source frame")
                enabled: panel.loaded && panel.previewFrame > 0
                onClicked: panel.previewFrame = 0
            }
            IconButton {
                objectName: "sourceMonitorBack"
                iconName: "step-back"
                tip: qsTr("Previous source frame")
                enabled: panel.loaded && panel.previewFrame > 0
                onClicked: panel.step(-1)
            }
            SpinBox {
                id: frameSpin
                objectName: "sourceMonitorFrame"
                from: 0
                to: Math.max(0, panel.maxFrames - 1)
                value: panel.previewFrame
                editable: true
                enabled: panel.loaded
                Layout.fillWidth: true
                Accessible.name: qsTr("Source preview frame")
                onValueModified: panel.previewFrame = value
            }
            IconButton {
                objectName: "sourceMonitorForward"
                iconName: "step-forward"
                tip: qsTr("Next source frame")
                enabled: panel.loaded && panel.previewFrame < panel.maxFrames - 1
                onClicked: panel.step(1)
            }
            IconButton {
                objectName: "sourceMonitorLast"
                iconName: "skip-end"
                tip: qsTr("Last source frame")
                enabled: panel.loaded && panel.previewFrame < panel.maxFrames - 1
                onClicked: panel.previewFrame = panel.maxFrames - 1
            }
        }
        Slider {
            objectName: "sourceMonitorScrubber"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingS
            Layout.rightMargin: Theme.spacingS
            from: 0
            to: Math.max(0, panel.maxFrames - 1)
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: panel.previewFrame
            enabled: panel.loaded
            onMoved: panel.previewFrame = Math.round(value)
            Accessible.name: qsTr("Scrub source independently from the sequence")
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingS
            Layout.rightMargin: Theme.spacingS
            spacing: Theme.spacingS
            FtButton {
                objectName: "sourceMonitorMarkIn"
                text: qsTr("Mark In")
                enabled: panel.loaded && panel.previewFrame < panel.markOutFrame
                onClicked: panel.markIn()
            }
            Label {
                color: Theme.textMuted
                font.family: Theme.monoFamily
                text: panel.markInFrame + " – " + panel.markOutFrame
                Accessible.name: qsTr("In %1, exclusive Out %2").arg(panel.markInFrame).arg(panel.markOutFrame)
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
            FtButton {
                objectName: "sourceMonitorMarkOut"
                text: qsTr("Mark Out")
                enabled: panel.loaded && panel.previewFrame >= panel.markInFrame
                onClicked: panel.markOut()
            }
            FtButton {
                objectName: "sourceMonitorResetMarks"
                text: qsTr("Reset marks")
                enabled: panel.loaded && (panel.markInFrame !== 0
                                          || panel.markOutFrame !== panel.maxFrames)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Restore the complete source In/Out range without editing the timeline.")
                onClicked: panel.resetMarks()
            }
            FtButton {
                objectName: "sourceMonitorMatchClipLength"
                text: qsTr("Match clip length")
                visible: panel.loaded && panel.selectedTimelineClip.count === 1
                enabled: panel.canMatchClipLength
                         && panel.markOutFrame !== panel.markInFrame
                                                  + panel.selectedTimelineClip.duration
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Set exclusive source Out to In plus the selected timeline clip's duration. Never move In or modify the timeline.")
                Accessible.name: qsTr("Match source range to selected timeline clip length")
                onClicked: panel.matchSelectedClipLength()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingS
            Layout.rightMargin: Theme.spacingS
            spacing: Theme.spacingS
            Label {
                text: qsTr("Target track")
                color: Theme.textMuted
            }
            ComboBox {
                id: destinationPicker
                objectName: "sourceMonitorTargetTrack"
                Layout.fillWidth: true
                enabled: panel.loaded
                model: panel.destinationTracks
                textRole: "label"
                valueRole: "id"
                currentIndex: Math.max(0, panel.destinationTracks.findIndex(
                                           track => track.id === panel.selectedTrackId))
                onActivated: index => panel.selectedTrackId = panel.destinationTracks[index].id
                Accessible.name: qsTr("Source placement destination track")
            }
            FtButton {
                objectName: "sourceMonitorPlaceVideoAudio"
                text: qsTr("Place V+A")
                visible: panel.loaded && panel.hasPicture && panel.hasSourceAudio
                enabled: panel.validRange && panel.selectedTrackAvailable
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Place matching picture and sound on free tracks in one undoable edit; clips are not permanently linked.")
                onClicked: panel.placeVideoAndAudio()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingS
            Layout.rightMargin: Theme.spacingS
            spacing: Theme.spacingS
            Label {
                Layout.fillWidth: true
                color: Theme.textMuted
                text: qsTr("Moves later clips across tracks; requires a clean cut.")
                elide: Text.ElideRight
            }
            FtButton {
                objectName: "sourceMonitorRippleInsert"
                text: qsTr("Ripple Insert")
                enabled: panel.validRange && panel.selectedTrackAvailable
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Insert at the Program playhead; shift later clips, markers and eligible subtitle cues together. Refuses cuts through clips, unsafe transitions and subtitles spanning the cut.")
                Accessible.name: qsTr("Ripple insert marked source range at playhead")
                onClicked: panel.rippleInsertRange()
            }
            FtButton {
                objectName: "sourceMonitorRippleVideoAudio"
                text: qsTr("Ripple V+A")
                visible: panel.loaded && panel.hasPicture && panel.hasSourceAudio
                enabled: panel.validRange && panel.selectedTrackAvailable
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Insert separate picture and sound with one undo step. Mutes picture audio so sound plays once; shifts later clips, markers and eligible captions together. Refuses unsafe cuts.")
                Accessible.name: qsTr("Ripple insert synchronized picture and audio at playhead")
                onClicked: panel.rippleInsertVideoAndAudio()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingS
            Layout.rightMargin: Theme.spacingS
            Layout.bottomMargin: Theme.spacingS
            spacing: Theme.spacingS
            Label {
                Layout.fillWidth: true
                color: Theme.textFaint
                text: panel.loaded
                      ? qsTr("%1 of %2 source frames (Out exclusive)")
                            .arg(panel.markOutFrame - panel.markInFrame).arg(panel.maxFrames)
                      : qsTr("Choose media to preview.")
                elide: Text.ElideRight
            }
            FtButton {
                objectName: "sourceMonitorReplaceSelected"
                text: qsTr("Replace selected")
                visible: panel.loaded
                enabled: panel.canReplaceSelected
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Replace one selected unlocked timeline clip. Source range must match its duration. Keeps its cut and scalar adjustments; clears source-anchored automation.")
                onClicked: panel.replaceSelected()
            }
            FtButton {
                objectName: "sourceMonitorPlace"
                text: qsTr("Place at playhead")
                primary: true
                enabled: panel.validRange && panel.selectedTrackAvailable
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Place without rippling or overwriting clips; requires free space on an unlocked track.")
                onClicked: panel.placeAtPlayhead()
            }
            FtButton {
                objectName: "sourceMonitorAppend"
                text: qsTr("Append range")
                enabled: panel.validRange && panel.selectedTrackAvailable
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Append to the selected track or use the first compatible track with room.")
                onClicked: {
                    if (!panel.appendRange())
                        panel.app.showStatus(qsTr("Could not append this source range."))
                }
            }
        }
    }
}

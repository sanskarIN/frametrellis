// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Multi-track timeline with ruler, track headers, clips, playhead and drop targets.
PanelFrame {
    id: panel

    required property AppController app
    readonly property TimelineController timeline: app.timeline
    readonly property real ppf: timeline.pixelsPerFrame
    property int trackHeight: 60
    readonly property int headerWidth: 176
    readonly property int rulerHeight: 28
    readonly property alias lanesItem: lanesContent

    title: qsTr("Timeline")

    function trackAtY(y) {
        const row = Math.floor(y / trackHeight)
        if (row < 0 || row >= laneRepeater.count)
            return ""
        return laneRepeater.itemAt(row).trackId
    }
    function rowOfTrack(trackId) { return timeline.model.rowOfTrack(trackId) }
    function showGhost(trackId, start, duration, valid) {
        const row = rowOfTrack(trackId)
        ghost.x = start * ppf
        ghost.width = Math.max(2, duration * ppf)
        ghost.y = row * trackHeight + 3
        ghost.valid = valid
        ghost.visible = row >= 0
    }
    function hideGhost() { ghost.visible = false }
    function ensurePlayheadVisible() {
        const x = app.playback.position * ppf
        if (x < lanes.contentX || x > lanes.contentX + lanes.width - 20)
            lanes.contentX = Math.max(0, Math.min(x - lanes.width * 0.1, lanes.contentWidth - lanes.width))
    }
    function zoomAround(factor, anchorX) {
        const frame = (lanes.contentX + anchorX) / ppf
        timeline.pixelsPerFrame = ppf * factor
        lanes.contentX = Math.max(0, frame * timeline.pixelsPerFrame - anchorX)
    }
    function centerOnPlayhead() {
        const target = panel.app.playback.position * panel.ppf - lanes.width * 0.5
        lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width, target))
    }
    function zoomToSelection() {
        const range = timeline.selectedFrameRange()
        if (range.start === undefined || range.end === undefined || lanes.width <= 0)
            return

        // Fit the whole selected span, not just the playhead or the first selected clip.
        // A small margin keeps edge handles visible at the sides of the viewport.
        const span = Math.max(1, range.end - range.start)
        const margin = Math.min(64, Math.max(24, lanes.width * 0.06))
        timeline.pixelsPerFrame = Math.max(1, lanes.width - 2 * margin) / span

        const centerX = (range.start + range.end) * 0.5 * panel.ppf
        lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width,
                                             centerX - lanes.width * 0.5))
    }

    tools: [
        IconButton { iconName: "track-video"; text: qsTr("+ Video"); tip: qsTr("Add a video track"); onClicked: panel.timeline.addTrack("video") },
        IconButton { iconName: "audio"; text: qsTr("+ Audio"); tip: qsTr("Add an audio track"); onClicked: panel.timeline.addTrack("audio") },
        Rectangle { width: 1; height: 20; color: Theme.border; anchors.verticalCenter: parent ? parent.verticalCenter : undefined },
        IconButton { iconName: "split"; tip: qsTr("Split at playhead (S)"); enabled: !panel.timeline.empty; onClicked: panel.timeline.splitAtPlayhead() },
        IconButton { iconName: "duplicate"; tip: qsTr("Duplicate selected clip to end of track (Ctrl+D)"); enabled: panel.timeline.selection.length === 1; onClicked: panel.timeline.duplicateSelection() },
        IconButton { iconName: "trash"; tip: qsTr("Delete selected clips (Delete)"); enabled: panel.timeline.selection.length > 0; onClicked: panel.timeline.deleteSelection(false) },
        IconButton { iconName: "ripple"; tip: qsTr("Ripple delete: remove and close the gap (Shift+Delete)"); enabled: panel.timeline.selection.length === 1; onClicked: panel.timeline.deleteSelection(true) },
        Rectangle { width: 1; height: 20; color: Theme.border; anchors.verticalCenter: parent ? parent.verticalCenter : undefined },
        IconButton { iconName: "magnet"; tip: panel.timeline.snapping ? qsTr("Snapping on (N)") : qsTr("Snapping off (N)"); active: panel.timeline.snapping; onClicked: panel.timeline.snapping = !panel.timeline.snapping },
        IconButton { iconName: "zoom-out"; tip: qsTr("Zoom out (−)"); onClicked: panel.zoomAround(1 / 1.4, lanes.width / 2) },
        FtSlider {
            id: zoomSlider
            width: 110
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            from: Math.log(panel.timeline.minPixelsPerFrame)
            to: Math.log(panel.timeline.maxPixelsPerFrame)
            value: Math.log(panel.ppf)
            onMoved: panel.zoomAround(Math.exp(value) / panel.ppf, lanes.width / 2)
            Accessible.name: qsTr("Timeline zoom")
        },
        IconButton { iconName: "zoom-in"; tip: qsTr("Zoom in (+)"); onClicked: panel.zoomAround(1.4, lanes.width / 2) },
        IconButton { iconName: "zoom-fit"; tip: qsTr("Fit the whole sequence"); onClicked: { panel.timeline.zoomToFit(lanes.width); lanes.contentX = 0 } },
        IconButton { iconName: "zoom-in"; tip: qsTr("Fit selected clips (Ctrl+Shift+0)"); enabled: panel.timeline.selection.length > 0; onClicked: panel.zoomToSelection() }
    ]

    Item {
        id: body
        anchors.fill: parent

        // Corner above the track headers: playhead timecode.
        Rectangle {
            width: panel.headerWidth
            height: panel.rulerHeight
            color: Theme.panelAlt
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            Text {
                anchors.centerIn: parent
                text: panel.app.playback.timecode
                color: Theme.accent
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSize
            }
        }

        Item {
            x: panel.headerWidth
            width: parent.width - panel.headerWidth
            height: panel.rulerHeight
            clip: true
            TimeRuler {
                anchors.fill: parent
                app: panel.app
                contentX: lanes.contentX
                pixelsPerFrame: panel.ppf
                onScrubbed: frame => panel.app.playback.seek(frame)
            }
            Rectangle { // playhead marker in the ruler
                x: panel.app.playback.position * panel.ppf - lanes.contentX - 6
                y: parent.height - 10
                width: 12
                height: 10
                color: Theme.playhead
                radius: 2
                visible: x > -12 && x < parent.width
            }
        }

        // Track headers follow the vertical scroll of the lanes.
        Item {
            y: panel.rulerHeight
            width: panel.headerWidth
            height: parent.height - panel.rulerHeight
            clip: true
            Column {
                y: -lanes.contentY
                width: parent.width
                Repeater {
                    model: panel.timeline.model
                    delegate: TrackHeader {
                        width: panel.headerWidth
                        height: panel.trackHeight
                        app: panel.app
                    }
                }
            }
        }

        Flickable {
            id: lanes
            objectName: "timelineLanes"
            x: panel.headerWidth
            y: panel.rulerHeight
            width: parent.width - panel.headerWidth
            height: parent.height - panel.rulerHeight
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width, (panel.timeline.duration + panel.timeline.frameRate * 30) * panel.ppf)
            contentHeight: Math.max(height, laneRepeater.count * panel.trackHeight)
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            WheelHandler {
                acceptedModifiers: Qt.ControlModifier
                onWheel: event => panel.zoomAround(event.angleDelta.y > 0 ? 1.25 : 0.8, point.position.x)
            }
            WheelHandler {
                acceptedModifiers: Qt.ShiftModifier
                onWheel: event => lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width,
                                                                       lanes.contentX - (event.angleDelta.y || event.angleDelta.x)))
            }
            WheelHandler {
                acceptedModifiers: Qt.NoModifier
                onWheel: event => {
                    lanes.contentY = Math.max(0, Math.min(lanes.contentHeight - lanes.height, lanes.contentY - event.angleDelta.y))
                    lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width, lanes.contentX - event.angleDelta.x))
                }
            }

            Item {
                id: lanesContent
                width: lanes.contentWidth
                height: lanes.contentHeight

                // Background clicks move the playhead and clear the selection.
                MouseArea {
                    anchors.fill: parent
                    onPressed: mouse => {
                        panel.timeline.clearSelection()
                        panel.app.playback.seek(mouse.x / panel.ppf)
                    }
                    onPositionChanged: mouse => { if (pressed) panel.app.playback.seek(Math.max(0, mouse.x / panel.ppf)) }
                }

                Column {
                    width: parent.width
                    Repeater {
                        id: laneRepeater
                        model: panel.timeline.model
                        delegate: Rectangle {
                            id: lane
                            required property string trackId
                            required property string kind
                            required property bool locked
                            required property bool hidden
                            required property bool muted
                            required property var clips
                            required property var transitions
                            width: lanesContent.width
                            height: panel.trackHeight
                            color: kind === "audio" ? Theme.trackAudio : Theme.trackVideo
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

                            Repeater {
                                model: lane.clips
                                delegate: ClipItem {
                                    y: 3
                                    height: panel.trackHeight - 7
                                    app: panel.app
                                    timelinePanel: panel
                                    trackId: lane.trackId
                                    trackLocked: lane.locked
                                    opacity: (lane.hidden || lane.muted) ? 0.55 : (lane.locked ? 0.6 : 1)
                                }
                            }

                            // Transitions sit on top of the clips they blend.
                            Repeater {
                                model: lane.transitions
                                delegate: Rectangle {
                                    id: transitionItem
                                    required property var modelData
                                    objectName: "transition:" + modelData.transitionId
                                    x: modelData.start * panel.ppf
                                    y: 3
                                    width: Math.max(8, modelData.duration * panel.ppf)
                                    height: panel.trackHeight - 7
                                    radius: 4
                                    color: modelData.selected ? Qt.rgba(Theme.selection.r, Theme.selection.g, Theme.selection.b, 0.35) : "#66000000"
                                    border.width: modelData.selected ? 2 : 1
                                    border.color: modelData.selected ? Theme.selection : Theme.clipText
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Transition, %1 long").arg(panel.timeline.clock(modelData.duration))

                                    // A bow tie marks the blend between the two clips.
                                    Canvas {
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            ctx.reset()
                                            ctx.strokeStyle = String(Theme.clipText)
                                            ctx.globalAlpha = 0.8
                                            ctx.lineWidth = 1.2
                                            ctx.beginPath()
                                            ctx.moveTo(0, 0); ctx.lineTo(width, height)
                                            ctx.moveTo(0, height); ctx.lineTo(width, 0)
                                            ctx.stroke()
                                        }
                                        onWidthChanged: requestPaint()
                                        onHeightChanged: requestPaint()
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onPressed: panel.timeline.selectTransition(transitionItem.modelData.transitionId)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: ghost
                    property bool valid: true
                    visible: false
                    height: panel.trackHeight - 7
                    radius: 4
                    color: valid ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.25) : Qt.rgba(1, 0.3, 0.3, 0.25)
                    border.width: 2
                    border.color: valid ? Theme.accent : Theme.danger
                    z: 10
                }

                Rectangle {
                    id: playhead
                    x: panel.app.playback.position * panel.ppf - 1
                    width: 2
                    height: parent.height
                    color: Theme.playhead
                    z: 20
                    Accessible.ignored: true
                }

                // Media dragged from the bin.
                DropArea {
                    anchors.fill: parent
                    keys: ["ft-asset"]
                    property real dropFrame: 0
                    property string dropTrack: ""
                    function update(drag) {
                        const assetId = drag.source ? drag.source.assetId : ""
                        dropTrack = panel.trackAtY(drag.y)
                        const length = panel.timeline.assetDuration(assetId)
                        dropFrame = panel.timeline.snapClipStart(Math.max(0, drag.x / panel.ppf), length, "", 8 / panel.ppf)
                        const ok = dropTrack.length > 0 && panel.timeline.acceptsAsset(dropTrack, assetId)
                                   && panel.timeline.canPlace(dropTrack, dropFrame, length, "")
                        if (dropTrack.length > 0)
                            panel.showGhost(dropTrack, dropFrame, length, ok)
                        else
                            panel.hideGhost()
                    }
                    onEntered: drag => update(drag)
                    onPositionChanged: drag => update(drag)
                    onExited: panel.hideGhost()
                    onDropped: drop => {
                        panel.hideGhost()
                        if (drop.source && dropTrack.length > 0)
                            panel.timeline.dropAsset(drop.source.assetId, dropTrack, dropFrame)
                    }
                }

                // Files dragged in from the operating system are imported into the bin.
                DropArea {
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: drop => { if (drop.hasUrls) panel.app.media.importFiles(drop.urls) }
                }
            }
        }

        EmptyState {
            anchors.centerIn: lanes
            visible: panel.timeline.empty
            iconName: "film"
            title: qsTr("The timeline is empty")
            message: qsTr("Drag clips here from the media bin. Video files go on V tracks, sounds on A tracks.")
        }
    }

    Connections {
        target: panel.app.playback
        function onPositionChanged() { if (panel.app.playback.playing) panel.ensurePlayheadVisible() }
    }
}

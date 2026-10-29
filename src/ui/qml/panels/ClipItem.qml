// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import FrameTrellis

// One clip on a track lane: click to select, drag to move (also to other tracks), drag the
// edges to trim. The timeline panel shows a ghost of the result and validates placement.
Rectangle {
    id: clip

    required property AppController app
    required property Item timelinePanel
    required property string trackId
    required property bool trackLocked
    required property string clipId
    required property string name
    required property real start
    required property real duration
    required property real sourceIn
    required property real sourceLength
    required property string kind
    required property bool selected
    required property bool offline
    required property bool muted
    required property real fadeIn
    required property real fadeOut
    required property string thumbnail
    required property string waveform
    required property real gain
    required property var audioEnvelope
    required property var opacityEnvelope
    required property real viewportLeft
    required property real viewportWidth

    readonly property real ppf: app.timeline.pixelsPerFrame
    readonly property real snapTolerance: 8 / Math.max(ppf, 0.0001)

    objectName: "clip:" + clipId
    x: start * ppf
    width: Math.max(2, duration * ppf)
    radius: 4
    color: Theme.clipColor(kind, offline)
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.selection : Theme.clipEdge(kind, offline)
    opacity: trackLocked ? 0.6 : 1
    clip: true

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Clip %1, starts %2, %3 long").arg(name).arg(app.timeline.timecode(start)).arg(app.timeline.clock(duration))
    Accessible.selected: selected

    // Thumbnail strip for pictures, a stylised bar for audio.
    Image {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 2
        width: Math.min(parent.width - 4, height * 16 / 9)
        source: clip.width > 40 ? clip.thumbnail : ""
        sourceSize.width: 160
        sourceSize.height: 90
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        opacity: 0.85
        visible: source.toString().length > 0
    }
    // The real waveform of the clip's sound, rendered in the background near the clip's width
    // (rounded up to a power of two so zooming does not re-render every step).
    Image {
        id: waveformImage
        objectName: "waveform"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 18
        anchors.bottomMargin: 2
        visible: clip.waveform.length > 0 && clip.width > 4 && height > 4
        readonly property int renderWidth: Math.min(4096, Math.max(64, Math.pow(2, Math.ceil(Math.log(Math.max(1, clip.width)) / Math.LN2))))
        source: visible ? "image://ft-waveform/%1/%2/%3".arg(clip.waveform).arg(Theme.waveformHex).arg(Math.round(clip.gain * 100)) : ""
        sourceSize.width: renderWidth
        sourceSize.height: Math.max(16, Math.round(height))
        fillMode: Image.Stretch
        asynchronous: true
        smooth: true
        opacity: clip.muted ? 0.35 : 0.9
    }
    IconImage {
        visible: clip.kind === "audio" && clip.width > 30 && waveformImage.status !== Image.Ready
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 8
        anchors.left: parent.left
        anchors.leftMargin: 8
        source: "qrc:/qt/qml/FrameTrellis/icons/audio.svg"
        sourceSize.width: 20
        sourceSize.height: 20
        color: Theme.clipAudioEdge
        opacity: clip.muted ? 0.35 : 0.8
    }

    // White diamonds are volume points; accent squares are opacity points.
    AutomationEnvelope {
        objectName: "audioEnvelope:" + clip.clipId
        points: clip.audioEnvelope
        clipOffset: clip.x
        clipWidth: clip.width
        pixelsPerFrame: clip.ppf
        viewportLeft: clip.viewportLeft
        viewportWidth: clip.viewportWidth
        lineColor: Theme.clipText
        valueField: "decibels"
        minimum: -60
        maximum: 12
        highlighted: clip.selected
        height: clip.height
    }
    AutomationEnvelope {
        objectName: "opacityEnvelope:" + clip.clipId
        points: clip.opacityEnvelope
        clipOffset: clip.x
        clipWidth: clip.width
        pixelsPerFrame: clip.ppf
        viewportLeft: clip.viewportLeft
        viewportWidth: clip.viewportWidth
        lineColor: Theme.accent
        valueField: "opacity"
        diamonds: false
        highlighted: clip.selected
        height: clip.height
    }

    // Fade ramps
    Canvas {
        anchors.fill: parent
        visible: clip.fadeIn > 0 || clip.fadeOut > 0
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "rgba(0,0,0,0.35)"
            const fi = clip.fadeIn * clip.ppf
            const fo = clip.fadeOut * clip.ppf
            if (fi > 0) { ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(fi, 0); ctx.lineTo(0, height); ctx.closePath(); ctx.fill() }
            if (fo > 0) { ctx.beginPath(); ctx.moveTo(width, 0); ctx.lineTo(width - fo, 0); ctx.lineTo(width, height); ctx.closePath(); ctx.fill() }
        }
        onWidthChanged: requestPaint()
        Connections { target: clip; function onFadeInChanged() { parent.requestPaint() } function onFadeOutChanged() { parent.requestPaint() } }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 18
        color: "#66000000"
        visible: clip.width > 24
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            IconImage {
                visible: clip.offline || clip.muted
                source: "qrc:/qt/qml/FrameTrellis/icons/" + (clip.offline ? "warning" : "volume-off") + ".svg"
                sourceSize.width: 12
                sourceSize.height: 12
                color: clip.offline ? Theme.danger : Theme.clipText
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: clip.offline ? qsTr("Offline: %1").arg(clip.name) : clip.name
                color: Theme.clipText
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
                width: Math.max(0, clip.width - 30)
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---- Interaction ----
    property real pressSceneX: 0
    property bool dragging: false
    property real candidateStart: start
    property string candidateTrack: trackId
    property bool candidateValid: true

    MouseArea {
        id: body
        anchors.fill: parent
        anchors.leftMargin: edgeLeft.visible ? edgeLeft.width : 0
        anchors.rightMargin: edgeRight.visible ? edgeRight.width : 0
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: clip.dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        onPressed: mouse => {
            clip.pressSceneX = mapToItem(null, mouse.x, mouse.y).x
            clip.dragging = false
            if (mouse.button === Qt.LeftButton)
                clip.app.timeline.select(clip.clipId, (mouse.modifiers & Qt.ControlModifier) !== 0)
            else if (!clip.selected)
                clip.app.timeline.select(clip.clipId, false)
        }
        onPositionChanged: mouse => {
            if (!(pressedButtons & Qt.LeftButton) || clip.trackLocked)
                return
            const scene = mapToItem(null, mouse.x, mouse.y)
            const dx = scene.x - clip.pressSceneX
            if (!clip.dragging && Math.abs(dx) < 4 && Math.abs(mouse.y - height / 2) < height / 2)
                return
            clip.dragging = true
            const raw = Math.max(0, clip.start + dx / clip.ppf)
            clip.candidateStart = clip.app.timeline.snapClipStart(raw, clip.duration, clip.clipId, clip.snapTolerance)
            const lanePoint = mapToItem(clip.timelinePanel.lanesItem, mouse.x, mouse.y)
            const target = clip.timelinePanel.trackAtY(lanePoint.y)
            clip.candidateTrack = target.length > 0 ? target : clip.trackId
            clip.candidateValid = clip.app.timeline.canPlace(clip.candidateTrack, clip.candidateStart, clip.duration, clip.clipId)
            clip.timelinePanel.showGhost(clip.candidateTrack, clip.candidateStart, clip.duration, clip.candidateValid)
        }
        onReleased: mouse => {
            if (clip.dragging) {
                clip.timelinePanel.hideGhost()
                if (clip.candidateValid)
                    clip.app.timeline.moveClip(clip.clipId, clip.candidateTrack, clip.candidateStart)
                else
                    clip.app.showStatus(qsTr("The clip cannot go there: it would overlap another clip or the track does not accept it."))
                clip.dragging = false
            } else if (mouse.button === Qt.RightButton) {
                menu.popup()
            }
        }
        onDoubleClicked: clip.app.playback.seek(clip.start)
    }

    MouseArea {
        id: edgeLeft
        visible: clip.width > 18 && !clip.trackLocked
        width: 6
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeHorCursor
        property real candidate: clip.start
        onPressed: mouse => { clip.pressSceneX = mapToItem(null, mouse.x, mouse.y).x; clip.app.timeline.select(clip.clipId, false) }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            const dx = mapToItem(null, mouse.x, mouse.y).x - clip.pressSceneX
            const earliest = clip.start - clip.sourceIn
            let frame = clip.app.timeline.snapFrame(clip.start + dx / clip.ppf, clip.clipId, clip.snapTolerance)
            frame = Math.max(Math.max(0, earliest), Math.min(frame, clip.start + clip.duration - 1))
            candidate = frame
            const end = clip.start + clip.duration
            clip.timelinePanel.showGhost(clip.trackId, frame, end - frame,
                                         clip.app.timeline.canPlace(clip.trackId, frame, end - frame, clip.clipId))
        }
        onReleased: {
            clip.timelinePanel.hideGhost()
            if (candidate !== clip.start)
                clip.app.timeline.trimStart(clip.clipId, candidate)
        }
    }

    MouseArea {
        id: edgeRight
        visible: clip.width > 18 && !clip.trackLocked
        width: 6
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        cursorShape: Qt.SizeHorCursor
        property real candidate: clip.start + clip.duration
        onPressed: mouse => { clip.pressSceneX = mapToItem(null, mouse.x, mouse.y).x; clip.app.timeline.select(clip.clipId, false) }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            const dx = mapToItem(null, mouse.x, mouse.y).x - clip.pressSceneX
            const latest = clip.sourceLength >= 0 ? clip.start + clip.sourceLength - clip.sourceIn : Number.MAX_SAFE_INTEGER
            let frame = clip.app.timeline.snapFrame(clip.start + clip.duration + dx / clip.ppf, clip.clipId, clip.snapTolerance)
            frame = Math.min(latest, Math.max(frame, clip.start + 1))
            candidate = frame
            clip.timelinePanel.showGhost(clip.trackId, clip.start, frame - clip.start,
                                         clip.app.timeline.canPlace(clip.trackId, clip.start, frame - clip.start, clip.clipId))
        }
        onReleased: {
            clip.timelinePanel.hideGhost()
            if (candidate !== clip.start + clip.duration)
                clip.app.timeline.trimEnd(clip.clipId, candidate)
        }
    }

    Menu {
        id: menu
        MenuItem { text: qsTr("Split at playhead"); onTriggered: clip.app.timeline.splitAtPlayhead() }
        MenuItem { text: clip.muted ? qsTr("Unmute clip") : qsTr("Mute clip"); onTriggered: { clip.app.timeline.setClipProperty(clip.clipId, "muted", !clip.muted); clip.app.timeline.endEdit() } }
        MenuItem { text: qsTr("Move playhead to clip start"); onTriggered: clip.app.playback.seek(clip.start) }
        MenuSeparator {}
        MenuItem { text: qsTr("Delete"); onTriggered: clip.app.timeline.deleteSelection(false) }
        MenuItem { text: qsTr("Ripple delete"); onTriggered: clip.app.timeline.deleteSelection(true) }
    }
}

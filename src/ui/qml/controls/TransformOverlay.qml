// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import FrameTrellis

// Geometry uses the monitor's picture rectangle, excluding its letterbox margins.
Item {
    id: overlay
    required property AppController app
    readonly property var automation: app.timeline.transformAutomation
    readonly property string token: (automation.clipId || "") + ":" + automation.sourceFrame
    visible: app.timeline.transformHandlesVisible && (automation.canEdit || false) && width > 0 && height > 0
    clip: true

    property string editToken: ""
    property string mode: ""
    property point pressPoint
    property var initial: ({x: 0, y: 0, scale: 1, rotation: 0})

    function bounded(value, low, high) { return Math.max(low, Math.min(high, value)) }
    function beginDrag(kind, area, mouse) {
        app.timeline.endEdit()
        initial = {x: automation.x, y: automation.y, scale: automation.scale, rotation: automation.rotation}
        pressPoint = area.mapToItem(overlay, mouse.x, mouse.y)
        editToken = token
        mode = kind
    }
    function finishDrag() {
        if (mode.length > 0) app.timeline.endEdit()
        mode = ""
    }
    function updateDrag(area, mouse) {
        if (mode.length === 0 || editToken !== token || !(automation.canEdit || false)) {
            finishDrag()
            return
        }
        const point = area.mapToItem(overlay, mouse.x, mouse.y)
        const cx = (0.5 + initial.x) * width
        const cy = (0.5 + initial.y) * height
        let x = initial.x
        let y = initial.y
        let scale = initial.scale
        let rotation = initial.rotation
        if (mode === "move") {
            x += (point.x - pressPoint.x) / width
            y += (point.y - pressPoint.y) / height
        } else if (mode === "scale") {
            const before = Math.hypot(pressPoint.x - cx, pressPoint.y - cy)
            if (before < 1) return
            scale *= Math.hypot(point.x - cx, point.y - cy) / before
        } else if (mode === "rotate") {
            const before = Math.atan2(pressPoint.y - cy, pressPoint.x - cx)
            const after = Math.atan2(point.y - cy, point.x - cx)
            let delta = (after - before) * 180 / Math.PI
            if (delta > 180) delta -= 360
            if (delta < -180) delta += 360
            rotation += delta
        }
        app.timeline.setTransformAtPlayhead(bounded(x, -4, 4), bounded(y, -4, 4),
                                             bounded(scale, 0.01, 10), bounded(rotation, -3600, 3600))
    }
    onTokenChanged: if (mode.length > 0 && token !== editToken) finishDrag()
    onVisibleChanged: if (!visible) finishDrag()

    Rectangle {
        id: box
        objectName: "transformBounds"
        x: (0.5 + (overlay.automation.x || 0)) * overlay.width - width / 2
        y: (0.5 + (overlay.automation.y || 0)) * overlay.height - height / 2
        width: overlay.width * (overlay.automation.scale === undefined ? 1 : overlay.automation.scale)
        height: overlay.height * (overlay.automation.scale === undefined ? 1 : overlay.automation.scale)
        rotation: overlay.automation.rotation || 0
        color: "transparent"
        border.color: Theme.accent
        border.width: 1.5

        MouseArea {
            id: moveArea
            objectName: "transformMoveArea"
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            preventStealing: true
            onPressed: mouse => overlay.beginDrag("move", moveArea, mouse)
            onPositionChanged: mouse => { if (pressed) overlay.updateDrag(moveArea, mouse) }
            onReleased: overlay.finishDrag()
            onCanceled: overlay.finishDrag()
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Move selected clip in the canvas")
        }
        Rectangle {
            anchors.centerIn: parent
            width: 8; height: 8; radius: 4
            color: Theme.accent
        }
        Rectangle {
            width: 14; height: 14
            x: parent.width - width / 2
            y: parent.height - height / 2
            radius: 2
            color: Theme.accent
            border.color: Theme.panel
            MouseArea {
                id: scaleArea
                objectName: "transformScaleHandle"
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                preventStealing: true
                onPressed: mouse => overlay.beginDrag("scale", scaleArea, mouse)
                onPositionChanged: mouse => { if (pressed) overlay.updateDrag(scaleArea, mouse) }
                onReleased: overlay.finishDrag()
                onCanceled: overlay.finishDrag()
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Scale selected clip uniformly")
            }
        }
        Rectangle {
            width: 16; height: 16; radius: 8
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            color: Theme.panel
            border.color: Theme.accent
            border.width: 2
            MouseArea {
                id: rotateArea
                objectName: "transformRotateHandle"
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                preventStealing: true
                onPressed: mouse => overlay.beginDrag("rotate", rotateArea, mouse)
                onPositionChanged: mouse => { if (pressed) overlay.updateDrag(rotateArea, mouse) }
                onReleased: overlay.finishDrag()
                onCanceled: overlay.finishDrag()
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Rotate selected clip clockwise")
            }
        }
    }
}

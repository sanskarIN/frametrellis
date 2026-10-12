// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import FrameTrellis

// The project's media. Tiles can be dragged onto timeline tracks; files dropped from the
// operating system are imported.
PanelFrame {
    id: panel

    required property AppController app
    signal importRequested()
    signal relinkRequested(string assetId, string name)

    title: qsTr("Media") + (app.media.model.count > 0 ? "  ·  " + app.media.model.count : "")

    tools: [
        BusyIndicator {
            running: panel.app.media.importing
            visible: running
            width: 24
            height: 24
            Accessible.name: qsTr("Importing media")
        },
        IconButton {
            iconName: "import"
            tip: qsTr("Import media… (Ctrl+I)")
            onClicked: panel.importRequested()
        }
    ]

    DropArea {
        id: fileDrop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (drop.hasUrls) {
                panel.app.media.importFiles(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.radius
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        visible: fileDrop.containsDrag
    }

    EmptyState {
        anchors.centerIn: parent
        visible: panel.app.media.model.count === 0
        iconName: "film"
        title: qsTr("No media yet")
        message: qsTr("Import video, audio or image files, or drop them here. Your files stay where they are; FrameTrellis only references them.")
        actionText: qsTr("Import media")
        onActionTriggered: panel.importRequested()
    }

    GridView {
        id: grid
        objectName: "mediaGrid"
        anchors.fill: parent
        anchors.margins: Theme.spacingS
        clip: true
        visible: count > 0
        model: panel.app.media.model
        cellWidth: Math.max(140, Math.floor(width / Math.max(1, Math.floor(width / 150))))
        cellHeight: cellWidth * 9 / 16 + 46
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        focus: true
        currentIndex: -1
        ScrollBar.vertical: ScrollBar {}
        Accessible.name: qsTr("Media bin")

        delegate: Item {
            id: tile
            required property int index
            required property string assetId
            required property string name
            required property string kind
            required property string duration
            required property string details
            required property string status
            required property string filePath
            required property string thumbnail
            required property int usage
            required property string tooltip

            width: grid.cellWidth
            height: grid.cellHeight

            Accessible.role: Accessible.ListItem
            Accessible.name: name + ", " + kind + ", " + duration + (status === "missing" ? ", offline" : "")

            Rectangle {
                id: card
                anchors.fill: parent
                anchors.margins: 4
                radius: Theme.radius
                color: tileMouse.containsMouse || grid.currentIndex === tile.index ? Theme.hover : Theme.panelAlt
                border.width: grid.currentIndex === tile.index ? 2 : 1
                border.color: tile.status === "missing" ? Theme.danger
                              : grid.currentIndex === tile.index ? Theme.accent : Theme.border

                Rectangle {
                    id: thumbFrame
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 4
                    height: width * 9 / 16
                    radius: Theme.radiusSmall
                    color: Theme.monitorBackground
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: tile.thumbnail
                        sourceSize.width: 256
                        sourceSize.height: 144
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        visible: source.toString().length > 0
                    }
                    IconImage {
                        anchors.centerIn: parent
                        visible: tile.kind === "audio" || tile.status === "missing"
                        source: "qrc:/qt/qml/FrameTrellis/icons/" + (tile.status === "missing" ? "warning" : "audio") + ".svg"
                        sourceSize.width: 32
                        sourceSize.height: 32
                        color: tile.status === "missing" ? Theme.danger : Theme.amber
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        radius: 3
                        color: "#cc0b0d10"
                        width: durationLabel.implicitWidth + 8
                        height: durationLabel.implicitHeight + 4
                        Text {
                            id: durationLabel
                            anchors.centerIn: parent
                            text: tile.duration
                            color: "#f4f6f8"
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.monoFamily
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 4
                        width: 8
                        height: 8
                        radius: 4
                        color: tile.kind === "audio" ? Theme.amber : (tile.kind === "image" ? Theme.iris : Theme.accent)
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: thumbFrame.bottom
                    anchors.margins: 6
                    text: tile.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideMiddle
                }
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    text: tile.status === "missing" ? qsTr("Offline — relink") : tile.details
                    color: tile.status === "missing" ? Theme.danger : Theme.textFaint
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            // Drag proxy that carries the asset id to timeline drop areas.
            Rectangle {
                id: dragProxy
                property string assetId: tile.assetId
                property string kind: tile.kind
                width: dragLabel.implicitWidth + 16
                height: 26
                radius: 13
                color: Theme.accent
                opacity: tileMouse.drag.active ? 0.95 : 0
                Drag.active: tileMouse.drag.active
                Drag.keys: ["ft-asset"]
                Drag.source: dragProxy
                Drag.hotSpot.x: 0
                Drag.hotSpot.y: 0
                Text {
                    id: dragLabel
                    anchors.centerIn: parent
                    text: tile.name
                    color: Theme.accentText
                    font.pixelSize: Theme.fontSizeSmall
                }
                // While dragging, float above the whole window instead of being clipped by the grid.
                states: State {
                    when: tileMouse.drag.active
                    ParentChange { target: dragProxy; parent: panel.Overlay.overlay }
                }
            }

            MouseArea {
                id: tileMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: tile.status === "missing" ? null : dragProxy
                drag.threshold: 6
                ToolTip.visible: containsMouse && !drag.active
                ToolTip.delay: 900
                ToolTip.text: tile.tooltip
                onPressed: mouse => {
                    grid.currentIndex = tile.index
                    grid.forceActiveFocus()
                    dragProxy.x = mouse.x
                    dragProxy.y = mouse.y
                }
                onReleased: {
                    if (drag.active)
                        dragProxy.Drag.drop()
                    dragProxy.x = 0
                    dragProxy.y = 0
                }
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton)
                        contextMenu.popup()
                }
                onDoubleClicked: {
                    if (tile.status === "missing")
                        panel.relinkRequested(tile.assetId, tile.name)
                    else
                        panel.app.timeline.appendAsset(tile.assetId)
                }
            }

            Keys.onReturnPressed: panel.app.timeline.appendAsset(tile.assetId)

            Menu {
                id: contextMenu
                MenuItem {
                    text: qsTr("Add to timeline")
                    enabled: tile.status !== "missing"
                    onTriggered: panel.app.timeline.appendAsset(tile.assetId)
                }
                MenuItem {
                    text: tile.status === "missing" ? qsTr("Relink missing file…") : qsTr("Replace file…")
                    onTriggered: panel.relinkRequested(tile.assetId, tile.name)
                }
                MenuItem {
                    text: qsTr("Show in folder")
                    enabled: tile.status !== "missing"
                    onTriggered: panel.app.media.revealInFolder(tile.assetId)
                }
                MenuSeparator {}
                MenuItem {
                    text: tile.usage > 0 ? qsTr("Remove from project (used %1×)").arg(tile.usage) : qsTr("Remove from project")
                    enabled: tile.usage === 0
                    onTriggered: panel.app.media.remove(tile.assetId)
                }
            }
        }
    }
}

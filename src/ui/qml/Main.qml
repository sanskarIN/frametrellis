// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import FrameTrellis

ApplicationWindow {
    id: window

    required property AppController app
    property bool allowClose: false
    property var pendingAction: null
    // Single-key editing shortcuts are disabled while a dialog is open, so that keys such as
    // Space, Delete or the arrows act on the dialog instead of the timeline.
    readonly property bool dialogOpen: newDialog.visible || titleDialog.visible || exportDialog.visible || aboutDialog.visible
                                       || shortcutsDialog.visible || goToTimecodeDialog.visible || markerDialog.visible || welcome.visible || messageBox.visible
                                       || unsavedDialog.visible || exportRunningDialog.visible || recoveryDialog.visible
                                       || subtitleDialog.visible || subtitleImportDialog.visible || subtitleExportDialog.visible
                                       || previewFrameDialog.visible || clipListDialog.visible || mediaPanel.excerptDialogOpen
    readonly property bool editKeys: !dialogOpen

    width: Math.min(1440, Screen.desktopAvailableWidth - 40)
    height: Math.min(900, Screen.desktopAvailableHeight - 40)
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: app.project.title + " — FrameTrellis"
    color: Theme.window

    palette.window: Theme.panel
    palette.windowText: Theme.text
    palette.base: Theme.panelAlt
    palette.alternateBase: Theme.raised
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.light: Theme.hover
    palette.midlight: Theme.raised
    palette.mid: Theme.border
    palette.dark: Theme.borderStrong
    palette.shadow: "#000000"
    palette.toolTipBase: Theme.raised
    palette.toolTipText: Theme.text
    palette.placeholderText: Theme.textFaint
    palette.brightText: Theme.text
    palette.link: Theme.accent

    Binding { target: Theme; property: "mode"; value: window.app.theme }

    // ---------- Unsaved-changes guard ----------
    function guarded(action, verb) {
        if (subtitleDialog.visible && subtitleDialog.dirty) {
            subtitleDialog.guarded(() => window.guarded(action, verb))
            return
        }
        if (subtitleDialog.visible)
            subtitleDialog.close()
        if (!app.project.modified) {
            action()
            return
        }
        pendingAction = action
        unsavedDialog.message = qsTr("Do you want to save the changes to “%1” before %2?").arg(app.project.name).arg(verb)
        unsavedDialog.open()
    }
    function runPending() {
        const action = pendingAction
        pendingAction = null
        if (action)
            action()
    }
    function save(thenRun) {
        if (subtitleDialog.visible && !subtitleDialog.applyIfDirty())
            return
        if (app.project.hasFilePath) {
            if (app.project.save() && thenRun)
                runPending()
        } else {
            saveDialog.runAfterSave = thenRun
            saveDialog.open()
        }
    }

    onClosing: close => {
        if (allowClose)
            return
        close.accepted = false
        const quit = () => { app.prepareQuit(false); allowClose = true; window.close() }
        if (app.exporter.busy) {
            exportRunningDialog.open()
            return
        }
        guarded(quit, qsTr("closing"))
    }

    Component.onCompleted: {
        if (!app.engineReady)
            messageBox.show(qsTr("The media engine could not start"),
                            qsTr("FrameTrellis could not load the MLT multimedia framework, so media cannot be played or exported.\n\n%1\n\nReinstalling FrameTrellis usually fixes this.").arg(app.engineMessage))
        else if (app.project.recovery.autosavePath)
            recoveryDialog.open()
        else if (app.firstRun)
            welcome.open()
    }

    Connections {
        target: window.app
        function onErrorOccurred(title, message) { messageBox.show(title, message) }
    }

    // ---------- Menus ----------
    menuBar: MenuBar {
        background: Rectangle { color: Theme.panel; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border } }

        Menu {
            title: qsTr("&File")
            Action { text: qsTr("&New Project…"); shortcut: StandardKey.New; onTriggered: window.guarded(() => newDialog.open(), qsTr("creating a new project")) }
            Action { text: qsTr("&Open Project…"); shortcut: StandardKey.Open; onTriggered: window.guarded(() => openDialog.open(), qsTr("opening another project")) }
            Menu {
                id: recentMenu
                title: qsTr("Open &Recent")
                enabled: window.app.project.recentProjects.length > 0
                Instantiator {
                    model: window.app.project.recentProjects
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: window.guarded(() => window.app.project.openProject(modelData), qsTr("opening another project"))
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
                MenuSeparator {}
                Action { text: qsTr("Clear Recent Projects"); onTriggered: window.app.project.clearRecentProjects() }
            }
            MenuSeparator {}
            Action { text: qsTr("&Save"); shortcut: StandardKey.Save; onTriggered: window.save(false) }
            Action { text: qsTr("Save &As…"); shortcut: "Ctrl+Shift+S"; onTriggered: { if (subtitleDialog.visible && !subtitleDialog.applyIfDirty()) return; saveDialog.runAfterSave = false; saveDialog.open() } }
            MenuSeparator {}
            Action { text: qsTr("&Import Media…"); shortcut: "Ctrl+I"; onTriggered: importDialog.open() }
            Action {
                text: qsTr("Export Media Audit (JSON)…")
                enabled: window.app.media.model.count > 0
                onTriggered: mediaAuditDialog.open()
            }
            Action {
                text: qsTr("Import Media &Folder…")
                enabled: !window.app.media.importing
                onTriggered: importFolderDialog.open()
            }
            Action {
                text: qsTr("Remove Unused Media from Project")
                enabled: window.app.media.model.count > 0 && !window.app.media.importing
                onTriggered: window.app.media.removeUnused()
            }
            Action { text: qsTr("Create &Title…"); onTriggered: titleDialog.open() }
            Action {
                text: qsTr("Save Preview &Frame as PNG…")
                enabled: window.app.playback.hasFrame
                onTriggered: previewFrameDialog.open()
            }
            Action { text: qsTr("&Subtitles…"); shortcut: "Ctrl+Alt+S"; enabled: window.editKeys; onTriggered: subtitleDialog.open() }
            Action { text: qsTr("Copy Picture EDL (CMX3600)"); enabled: window.editKeys && !window.app.timeline.empty; onTriggered: window.app.timeline.copyPictureAsEdl() }
            Action { text: qsTr("&Export…"); shortcut: "Ctrl+E"; enabled: !window.app.timeline.empty || window.app.exporter.busy; onTriggered: exportDialog.open() }
            Action {
                text: qsTr("Export Clip List (CSV)…")
                enabled: !window.app.timeline.empty
                onTriggered: clipListDialog.open()
            }
            MenuSeparator {}
            Action { text: qsTr("&Quit"); shortcut: StandardKey.Quit; onTriggered: window.close() }
        }
        Menu {
            title: qsTr("&Edit")
            Action { text: window.app.project.canUndo ? qsTr("&Undo %1").arg(window.app.project.undoText) : qsTr("&Undo"); shortcut: StandardKey.Undo; enabled: window.editKeys && window.app.project.canUndo; onTriggered: window.app.project.undo() }
            Action { text: window.app.project.canRedo ? qsTr("&Redo %1").arg(window.app.project.redoText) : qsTr("&Redo"); shortcut: "Ctrl+Shift+Z"; enabled: window.editKeys && window.app.project.canRedo; onTriggered: window.app.project.redo() }
            MenuSeparator {}
            Action { text: qsTr("&Split at Playhead"); shortcut: "S"; enabled: window.editKeys; onTriggered: window.app.timeline.splitAtPlayhead() }
            Action { text: qsTr("D&uplicate Selected Clips"); shortcut: "Ctrl+D"; enabled: window.editKeys && window.app.timeline.selection.length > 0; onTriggered: window.app.timeline.duplicateSelection() }
            Menu {
                title: qsTr("Nudge Selected Clips")
                enabled: window.editKeys && window.app.timeline.selection.length > 0
                Action { text: qsTr("Left 1 Frame"); shortcut: "Alt+Left"; onTriggered: window.app.timeline.nudgeSelection(-1) }
                Action { text: qsTr("Right 1 Frame"); shortcut: "Alt+Right"; onTriggered: window.app.timeline.nudgeSelection(1) }
                Action { text: qsTr("Left 10 Frames"); shortcut: "Alt+Shift+Left"; onTriggered: window.app.timeline.nudgeSelection(-10) }
                Action { text: qsTr("Right 10 Frames"); shortcut: "Alt+Shift+Right"; onTriggered: window.app.timeline.nudgeSelection(10) }
            }
            Menu {
                title: qsTr("Insert Gap at Playhead")
                enabled: window.editKeys && !window.app.timeline.empty
                Action { text: qsTr("1 Frame"); onTriggered: window.app.timeline.insertGapAtPlayhead(1) }
                Action { text: qsTr("10 Frames"); onTriggered: window.app.timeline.insertGapAtPlayhead(10) }
                Action { text: qsTr("About 1 Second"); onTriggered: window.app.timeline.insertGapAtPlayhead(Math.max(1, Math.round(window.app.timeline.frameRate))) }
                Action { text: qsTr("About 5 Seconds"); onTriggered: window.app.timeline.insertGapAtPlayhead(Math.max(1, Math.round(window.app.timeline.frameRate)) * 5) }
            }
            Menu {
                title: qsTr("Remove Empty Time at Playhead")
                enabled: window.editKeys && !window.app.timeline.empty
                Action { text: qsTr("1 Frame"); onTriggered: window.app.timeline.removeEmptyTimeAtPlayhead(1) }
                Action { text: qsTr("10 Frames"); onTriggered: window.app.timeline.removeEmptyTimeAtPlayhead(10) }
                Action { text: qsTr("About 1 Second"); onTriggered: window.app.timeline.removeEmptyTimeAtPlayhead(Math.max(1, Math.round(window.app.timeline.frameRate))) }
                Action { text: qsTr("About 5 Seconds"); onTriggered: window.app.timeline.removeEmptyTimeAtPlayhead(Math.max(1, Math.round(window.app.timeline.frameRate)) * 5) }
            }
            Menu {
                title: qsTr("Align Selected Clip to Playhead")
                enabled: window.editKeys && window.app.timeline.selection.length === 1
                Action { text: qsTr("Align Start"); onTriggered: window.app.timeline.alignSelectionToPlayhead(false) }
                Action { text: qsTr("Align End"); onTriggered: window.app.timeline.alignSelectionToPlayhead(true) }
            }
            Action {
                text: qsTr("Align Selection Start to Playhead")
                enabled: window.editKeys && window.app.timeline.selection.length > 0
                onTriggered: window.app.timeline.alignSelectionStartToPlayhead()
            }
            Menu {
                title: qsTr("Trim Selected Clip to Playhead")
                enabled: window.editKeys && window.app.timeline.selection.length === 1
                Action { text: qsTr("Trim Start to Playhead"); onTriggered: window.app.timeline.trimSelectionToPlayhead(false) }
                Action { text: qsTr("Trim End to Playhead"); onTriggered: window.app.timeline.trimSelectionToPlayhead(true) }
            }
            Menu {
                title: qsTr("Slip Selected Clips (Source)")
                enabled: window.editKeys && window.app.timeline.selection.length > 0
                Action { text: qsTr("Earlier 1 Frame"); shortcut: "Ctrl+Alt+Left"; onTriggered: window.app.timeline.slipSelection(-1) }
                Action { text: qsTr("Later 1 Frame"); shortcut: "Ctrl+Alt+Right"; onTriggered: window.app.timeline.slipSelection(1) }
                Action { text: qsTr("Earlier 10 Frames"); shortcut: "Ctrl+Alt+Shift+Left"; onTriggered: window.app.timeline.slipSelection(-10) }
                Action { text: qsTr("Later 10 Frames"); shortcut: "Ctrl+Alt+Shift+Right"; onTriggered: window.app.timeline.slipSelection(10) }
            }
            Menu {
                title: qsTr("Roll Cut After Selected Clip")
                enabled: window.editKeys && window.app.timeline.selection.length === 1
                Action { text: qsTr("Earlier 1 Frame"); onTriggered: window.app.timeline.rollSelection(-1) }
                Action { text: qsTr("Later 1 Frame"); onTriggered: window.app.timeline.rollSelection(1) }
                Action { text: qsTr("Earlier 10 Frames"); onTriggered: window.app.timeline.rollSelection(-10) }
                Action { text: qsTr("Later 10 Frames"); onTriggered: window.app.timeline.rollSelection(10) }
            }
            Action { text: qsTr("Copy Clip &Attributes"); shortcut: "Ctrl+Alt+C"; enabled: window.editKeys && window.app.timeline.selection.length === 1; onTriggered: window.app.timeline.copySelectionAttributes() }
            Action { text: qsTr("&Paste Clip Attributes"); shortcut: "Ctrl+Alt+V"; enabled: window.editKeys && window.app.timeline.selection.length > 0 && window.app.timeline.hasCopiedClipAttributes; onTriggered: window.app.timeline.pasteSelectionAttributes() }
            MenuSeparator {}
            Action { text: qsTr("&Mute Selected Clips"); shortcut: "Ctrl+Alt+M"; enabled: window.editKeys && window.app.timeline.selection.length > 0; onTriggered: window.app.timeline.muteSelection(true) }
            Action { text: qsTr("Unmute Selected Clips"); shortcut: "Ctrl+Alt+U"; enabled: window.editKeys && window.app.timeline.selection.length > 0; onTriggered: window.app.timeline.muteSelection(false) }
            Action { text: qsTr("&Reset Selected Picture Adjustments"); enabled: window.editKeys && window.app.timeline.selection.length > 0; onTriggered: window.app.timeline.resetSelectionPictures() }
            Action { text: qsTr("Add &Transition"); shortcut: "T"; enabled: window.editKeys; onTriggered: window.app.timeline.addTransition() }
            Action { text: qsTr("&Delete"); shortcut: StandardKey.Delete; enabled: window.editKeys; onTriggered: window.app.timeline.deleteSelection(false) }
            Action { text: qsTr("Ripple De&lete"); shortcut: "Shift+Delete"; enabled: window.editKeys; onTriggered: window.app.timeline.deleteSelection(true) }
            MenuSeparator {}
            Action { text: qsTr("Select &All Clips"); shortcut: StandardKey.SelectAll; enabled: window.editKeys; onTriggered: window.app.timeline.selectAll() }
            Action { text: qsTr("Select Clips at &Playhead"); shortcut: "Ctrl+Shift+J"; enabled: window.editKeys && !window.app.timeline.empty; onTriggered: window.app.timeline.selectClipsAtPlayhead() }
            Action { text: qsTr("Select All Occurrences of &Media"); shortcut: "Ctrl+Alt+R"; enabled: window.editKeys && window.app.timeline.selection.length === 1; onTriggered: window.app.timeline.selectAllMediaOccurrences() }
            Action { text: qsTr("Clear Selection"); onTriggered: window.app.timeline.clearSelection() }
            MenuSeparator {}
            Action { text: qsTr("Lock All Tracks"); enabled: window.editKeys; onTriggered: window.app.timeline.setAllTracksLocked(true) }
            Action { text: qsTr("Unlock All Tracks"); enabled: window.editKeys; onTriggered: window.app.timeline.setAllTracksLocked(false) }
            MenuSeparator {}
            Action { text: qsTr("Check for Missing Media"); onTriggered: window.app.media.rescan() }
        }
        Menu {
            title: qsTr("&View")
            Action { text: qsTr("Zoom &In"); shortcut: "+"; enabled: window.editKeys; onTriggered: window.app.timeline.zoomIn() }
            Action { text: qsTr("Zoom &Out"); shortcut: "-"; enabled: window.editKeys; onTriggered: window.app.timeline.zoomOut() }
            Action { text: qsTr("Zoom to &Selection"); shortcut: "Ctrl+Shift+0"; enabled: window.editKeys && window.app.timeline.selection.length > 0; onTriggered: timelinePanel.zoomToSelection() }
            Action { text: qsTr("Center on &Playhead"); shortcut: "Ctrl+Shift+L"; enabled: window.editKeys && !window.app.timeline.empty; onTriggered: timelinePanel.centerOnPlayhead() }
            Action { text: qsTr("&Snapping"); shortcut: "N"; enabled: window.editKeys; checkable: true; checked: window.app.timeline.snapping; onTriggered: window.app.timeline.snapping = checked }
            MenuSeparator {}
            Action { text: qsTr("Compact Tracks"); checkable: true; checked: timelinePanel.trackHeight === 44; onTriggered: timelinePanel.trackHeight = 44 }
            Action { text: qsTr("Normal Tracks"); checkable: true; checked: timelinePanel.trackHeight === 60; onTriggered: timelinePanel.trackHeight = 60 }
            Action { text: qsTr("Tall Tracks"); checkable: true; checked: timelinePanel.trackHeight === 84; onTriggered: timelinePanel.trackHeight = 84 }
            MenuSeparator {}
            Action { text: qsTr("Dark Theme"); checkable: true; checked: window.app.theme === "dark"; onTriggered: window.app.theme = "dark" }
            Action { text: qsTr("Light Theme"); checkable: true; checked: window.app.theme === "light"; onTriggered: window.app.theme = "light" }
        }
        Menu {
            title: qsTr("&Playback")
            Action { text: window.app.playback.playing ? qsTr("&Pause") : qsTr("&Play"); shortcut: "Space"; enabled: window.editKeys; onTriggered: window.app.playback.togglePlay() }
            Action { text: qsTr("Previous Frame"); shortcut: "Left"; enabled: window.editKeys; onTriggered: window.app.playback.step(-1) }
            Action { text: qsTr("Next Frame"); shortcut: "Right"; enabled: window.editKeys; onTriggered: window.app.playback.step(1) }
            Action { text: qsTr("Back One Second"); shortcut: "Shift+Left"; enabled: window.editKeys; onTriggered: window.app.playback.step(-Math.round(window.app.timeline.frameRate)) }
            Action { text: qsTr("Forward One Second"); shortcut: "Shift+Right"; enabled: window.editKeys; onTriggered: window.app.playback.step(Math.round(window.app.timeline.frameRate)) }
            Action { text: qsTr("Previous Edit Point"); shortcut: "Up"; enabled: window.editKeys; onTriggered: window.app.timeline.previousEditPoint() }
            Action { text: qsTr("Next Edit Point"); shortcut: "Down"; enabled: window.editKeys; onTriggered: window.app.timeline.nextEditPoint() }
            Action { text: qsTr("&Go to Timecode…"); shortcut: "Ctrl+G"; enabled: window.editKeys && !window.app.timeline.empty; onTriggered: goToTimecodeDialog.open() }
            MenuSeparator {}
            Action { text: qsTr("Add Timeline &Marker…"); shortcut: "Shift+M"; enabled: window.editKeys; onTriggered: markerDialog.openNew(window.app.playback.position) }
            Action { text: qsTr("Previous Marker"); shortcut: "Ctrl+PgUp"; enabled: window.editKeys && window.app.timeline.markers.length > 0; onTriggered: window.app.timeline.previousMarker() }
            Action { text: qsTr("Next Marker"); shortcut: "Ctrl+PgDown"; enabled: window.editKeys && window.app.timeline.markers.length > 0; onTriggered: window.app.timeline.nextMarker() }
            Action { text: qsTr("Copy Markers as Video Chapters"); enabled: window.editKeys && window.app.timeline.markers.length > 0; onTriggered: window.app.timeline.copyMarkersAsChapters() }
            Action { text: qsTr("Copy Markers as WebVTT Chapters"); enabled: window.editKeys && window.app.timeline.markers.length > 0; onTriggered: window.app.timeline.copyMarkersAsWebVtt() }
            Action { text: qsTr("Go to Start"); shortcut: "Home"; enabled: window.editKeys; onTriggered: window.app.playback.goToStart() }
            Action { text: qsTr("Go to End"); shortcut: "End"; enabled: window.editKeys; onTriggered: window.app.playback.goToEnd() }
            MenuSeparator {}
            Action { text: qsTr("Mute Preview"); checkable: true; checked: window.app.playback.muted; enabled: window.app.playback.audioAvailable; onTriggered: window.app.playback.muted = checked }
        }
        Menu {
            title: qsTr("&Help")
            Action { text: qsTr("&Keyboard Shortcuts"); shortcut: "F1"; onTriggered: shortcutsDialog.open() }
            Action { text: qsTr("&Getting Started"); onTriggered: welcome.open() }
            Action { text: qsTr("&Documentation"); onTriggered: window.app.openUrl("https://sanskarin.github.io/frametrellis/docs/") }
            Action { text: qsTr("&Report an Issue"); onTriggered: window.app.openUrl("https://github.com/sanskarIN/frametrellis/issues") }
            MenuSeparator {}
            Action { text: qsTr("&About FrameTrellis"); onTriggered: aboutDialog.open() }
        }
    }

    // ---------- Toolbar ----------
    header: Rectangle {
        height: 44
        color: Theme.panel
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingS
            anchors.rightMargin: Theme.spacingS
            spacing: 2
            Image {
                source: "qrc:/qt/qml/FrameTrellis/icons/app-icon.svg"
                sourceSize.width: 24
                sourceSize.height: 24
                Layout.rightMargin: Theme.spacingS
                Accessible.ignored: true
            }
            IconButton { iconName: "file-new"; tip: qsTr("New project (Ctrl+N)"); onClicked: window.guarded(() => newDialog.open(), qsTr("creating a new project")) }
            IconButton { iconName: "folder-open"; tip: qsTr("Open project (Ctrl+O)"); onClicked: window.guarded(() => openDialog.open(), qsTr("opening another project")) }
            IconButton { iconName: "save"; tip: qsTr("Save (Ctrl+S)"); active: window.app.project.modified; onClicked: window.save(false) }
            Rectangle { width: 1; height: 22; color: Theme.border; Layout.leftMargin: 6; Layout.rightMargin: 6 }
            IconButton { iconName: "undo"; tip: window.app.project.canUndo ? qsTr("Undo %1 (Ctrl+Z)").arg(window.app.project.undoText) : qsTr("Nothing to undo"); enabled: window.app.project.canUndo; onClicked: window.app.project.undo() }
            IconButton { iconName: "redo"; tip: window.app.project.canRedo ? qsTr("Redo %1 (Ctrl+Shift+Z)").arg(window.app.project.redoText) : qsTr("Nothing to redo"); enabled: window.app.project.canRedo; onClicked: window.app.project.redo() }
            Rectangle { width: 1; height: 22; color: Theme.border; Layout.leftMargin: 6; Layout.rightMargin: 6 }
            IconButton { iconName: "import"; text: qsTr("Import"); tip: qsTr("Import media (Ctrl+I)"); onClicked: importDialog.open() }
            IconButton { iconName: "subtitles"; text: qsTr("Subtitles"); objectName: "subtitlesButton"; tip: qsTr("Edit subtitles (Ctrl+Alt+S)"); onClicked: subtitleDialog.open() }
            IconButton { iconName: "export"; text: qsTr("Export"); tip: qsTr("Export video (Ctrl+E)"); enabled: !window.app.timeline.empty || window.app.exporter.busy; onClicked: exportDialog.open() }
            Item { Layout.fillWidth: true }
            Text {
                text: window.app.project.title
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize
                elide: Text.ElideMiddle
                Layout.maximumWidth: 360
            }
            Item { Layout.fillWidth: true }
            IconButton { iconName: window.app.theme === "dark" ? "sun" : "moon"; tip: window.app.theme === "dark" ? qsTr("Use light theme") : qsTr("Use dark theme"); onClicked: window.app.theme = window.app.theme === "dark" ? "light" : "dark" }
            IconButton { iconName: "keyboard"; tip: qsTr("Keyboard shortcuts (F1)"); onClicked: shortcutsDialog.open() }
            IconButton { iconName: "info"; tip: qsTr("About FrameTrellis"); onClicked: aboutDialog.open() }
        }
    }

    // ---------- Workspace ----------
    SplitView {
        id: workspace
        anchors.fill: parent
        anchors.margins: Theme.spacingS
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 6
            color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : "transparent"
            opacity: 0.6
        }

        SplitView {
            SplitView.preferredHeight: workspace.height * 0.56
            SplitView.minimumHeight: 240
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 6
                color: SplitHandle.hovered || SplitHandle.pressed ? Theme.accent : "transparent"
                opacity: 0.6
            }

            MediaBinPanel {
                id: mediaPanel
                app: window.app
                SplitView.preferredWidth: 330
                SplitView.minimumWidth: 200
                onImportRequested: importDialog.open()
                onRelinkRequested: (assetId, name) => { relinkDialog.assetId = assetId; relinkDialog.title = qsTr("Locate “%1”").arg(name); relinkDialog.open() }
            }
            MonitorPanel {
                app: window.app
                SplitView.fillWidth: true
                SplitView.minimumWidth: 320
            }
            InspectorPanel {
                app: window.app
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 220
            }
        }

        TimelinePanel {
            onEditSubtitlesRequested: subtitleDialog.open()
            onMarkerAddRequested: frame => markerDialog.openNew(frame)
            onMarkerEditRequested: (id, frame, label) => markerDialog.openExisting(id, frame, label)
            id: timelinePanel
            app: window.app
            SplitView.fillHeight: true
            SplitView.minimumHeight: 180
        }
    }

    // ---------- Status bar ----------
    footer: Rectangle {
        height: 28
        color: Theme.panel
        Rectangle { width: parent.width; height: 1; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingM
            anchors.rightMargin: Theme.spacingM
            spacing: Theme.spacingM
            Text {
                Layout.fillWidth: true
                text: window.app.statusMessage.length > 0 ? window.app.statusMessage
                      : (window.app.media.importing ? qsTr("Importing %1 file(s)…").arg(window.app.media.pendingImports) : qsTr("Ready"))
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
                Accessible.role: Accessible.StatusBar
                Accessible.name: text
            }
            Row {
                visible: window.app.media.model.missingCount > 0
                spacing: 4
                IconImage { source: "qrc:/qt/qml/FrameTrellis/icons/warning.svg"; sourceSize.width: 14; sourceSize.height: 14; color: Theme.warning; anchors.verticalCenter: parent.verticalCenter }
                Text { text: qsTr("%1 offline").arg(window.app.media.model.missingCount); color: Theme.warning; font.pixelSize: Theme.fontSizeSmall; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                visible: window.app.exporter.busy
                implicitWidth: exportRow.implicitWidth
                implicitHeight: exportRow.implicitHeight
                cursorShape: Qt.PointingHandCursor
                onClicked: exportDialog.open()
                Row {
                    id: exportRow
                    spacing: 6
                    Text { text: qsTr("Exporting"); color: Theme.accent; font.pixelSize: Theme.fontSizeSmall; anchors.verticalCenter: parent.verticalCenter }
                    FtProgressBar { width: 120; value: window.app.exporter.progress; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Text {
                text: window.app.project.settingsSummary
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }

    // ---------- Dialogs ----------
    FileDialog {
        id: openDialog
        title: qsTr("Open project")
        nameFilters: [qsTr("FrameTrellis projects (*.ftproj)"), qsTr("All files (*)")]
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        onAccepted: window.app.project.openProject(window.app.localPath(selectedFile))
    }
    FileDialog {
        id: saveDialog
        property bool runAfterSave: false
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ftproj"
        nameFilters: [qsTr("FrameTrellis projects (*.ftproj)")]
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        selectedFile: window.app.fileUrl(window.app.project.projectFolder() + "/" + window.app.project.suggestedFileName())
        onAccepted: {
            if (window.app.project.saveAs(window.app.localPath(selectedFile)) && runAfterSave)
                window.runPending()
        }
    }
    FileDialog {
        id: mediaAuditDialog
        title: qsTr("Export media audit as JSON")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON reports (*.json)")]
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        onAccepted: window.app.media.exportMediaAudit(window.app.localPath(selectedFile))
    }
    FolderDialog {
        id: importFolderDialog
        title: qsTr("Import media from folder (including subfolders)")
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        onAccepted: window.app.media.importFolder(window.app.localPath(selectedFolder))
    }
    FileDialog {
        id: previewFrameDialog
        title: qsTr("Save current preview frame as PNG")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG images (*.png)")]
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        onAccepted: {
            if (window.app.playback.savePreviewFrame(window.app.localPath(selectedFile)))
                window.app.showStatus(qsTr("Saved current preview frame as PNG."))
        }
    }
    FileDialog {
        id: clipListDialog
        title: qsTr("Export timeline clip list")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV clip lists (*.csv)")]
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        onAccepted: window.app.project.exportClipListCsv(window.app.localPath(selectedFile))
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import media")
        fileMode: FileDialog.OpenFiles
        nameFilters: window.app.media.nameFilters
        onAccepted: window.app.media.importFiles(selectedFiles)
    }
    FileDialog {
        id: relinkDialog
        property string assetId
        fileMode: FileDialog.OpenFile
        nameFilters: window.app.media.nameFilters
        onAccepted: window.app.media.relink(assetId, window.app.localPath(selectedFile))
    }
    FileDialog {
        id: subtitleImportDialog
        title: qsTr("Import subtitles (append cues)")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SubRip subtitles (*.srt *.SRT)")]
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        onAccepted: window.app.subtitles.importSrt(window.app.localPath(selectedFile))
    }
    FileDialog {
        id: subtitleExportDialog
        title: qsTr("Export subtitles")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("SubRip subtitles (*.srt)")]
        currentFolder: window.app.fileUrl(window.app.project.projectFolder())
        selectedFile: window.app.fileUrl(window.app.project.projectFolder() + "/" + window.app.project.suggestedFileName().replace(/\.ftproj$/i, ".srt"))
        onAccepted: window.app.subtitles.exportSrt(window.app.localPath(selectedFile))
    }

    NewProjectDialog { id: newDialog; app: window.app }
    TitleDialog { id: titleDialog; objectName: "titleDialog"; app: window.app }
    ExportDialog { id: exportDialog; objectName: "exportDialog"; app: window.app }
    AboutDialog { id: aboutDialog; objectName: "aboutDialog"; app: window.app }
    ShortcutsDialog { id: shortcutsDialog }
    GoToTimecodeDialog { id: goToTimecodeDialog; objectName: "goToTimecodeDialog"; app: window.app }
    MarkerDialog { id: markerDialog; objectName: "markerDialog"; app: window.app }
    SubtitleDialog {
        id: subtitleDialog
        objectName: "subtitleDialog"
        app: window.app
        onImportRequested: subtitleImportDialog.open()
        onExportRequested: subtitleExportDialog.open()
    }
    WelcomeOverlay { id: welcome; objectName: "welcome"; app: window.app; onImportRequested: importDialog.open() }
    MessageBox { id: messageBox }

    ConfirmDialog {
        id: unsavedDialog
        title: qsTr("Unsaved changes")
        onSaveRequested: window.save(true)
        onDiscardRequested: window.runPending()
        onRejected: window.pendingAction = null
    }
    ConfirmDialog {
        id: exportRunningDialog
        title: qsTr("An export is running")
        message: qsTr("Quitting now cancels the export. The partially written file is removed.")
        acceptText: qsTr("Keep exporting")
        discardText: qsTr("Cancel export and quit")
        onDiscardRequested: { window.app.exporter.cancel(); window.guarded(() => { window.app.prepareQuit(false); window.allowClose = true; window.close() }, qsTr("closing")) }
    }
    ConfirmDialog {
        id: recoveryDialog
        title: qsTr("Recover unsaved work?")
        message: qsTr("FrameTrellis closed unexpectedly while “%1” had unsaved changes. An automatic backup from %2 is available.")
                 .arg(window.app.project.recovery.name || qsTr("a project")).arg(window.app.project.recovery.savedAt || "")
        acceptText: qsTr("Recover")
        discardText: qsTr("Discard backup")
        onSaveRequested: window.app.project.recoverAutosave()
        onDiscardRequested: window.app.project.discardAutosave()
    }
}

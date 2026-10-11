// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Dialogs
import QtQuick.Layouts
import FrameTrellis

// Export settings, live validation, progress and result.
FtDialog {
    id: dialog

    required property AppController app
    readonly property ExportController exporter: app.exporter
    readonly property bool showProgress: exporter.state !== "idle"
    property string outputPath: ""

    title: qsTr("Export video")
    implicitWidth: 560
    closePolicy: exporter.busy ? Popup.NoAutoClose : Popup.CloseOnEscape

    function settings() {
        return {
            presetId: presetBox.currentIndex >= 0 ? exporter.presets[presetBox.currentIndex].id : "match",
            outputPath: outputPath,
            crf: Math.round(quality.value),
            speedPreset: speedBox.currentText,
            audioBitrateKbps: parseInt(audioBox.currentText),
            includeAudio: audioCheck.checked
        }
    }
    function applyPreset(index) {
        const p = exporter.presets[index]
        quality.value = p.crf
        speedBox.currentIndex = Math.max(0, exporter.speedPresets.indexOf(p.speedPreset))
        audioBox.currentIndex = Math.max(0, audioBox.model.indexOf(String(p.audioBitrateKbps)))
    }
    function startExport() {
        if (exporter.fileExists(outputPath)) {
            overwrite.open()
            return
        }
        exporter.start(settings())
    }

    onOpened: {
        if (!exporter.busy && exporter.state !== "idle")
            exporter.reset()
        if (outputPath.length === 0)
            outputPath = exporter.suggestedOutputPath()
        applyPreset(presetBox.currentIndex)
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Export to")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("MP4 video (*.mp4)")]
        selectedFile: dialog.app.fileUrl(dialog.outputPath)
        onAccepted: dialog.outputPath = dialog.app.localPath(selectedFile)
    }

    ConfirmDialog {
        id: overwrite
        title: qsTr("Replace the existing file?")
        message: qsTr("“%1” already exists. It will be replaced only if the export succeeds.").arg(dialog.outputPath)
        acceptText: qsTr("Replace")
        showDiscard: false
        onSaveRequested: dialog.exporter.start(dialog.settings())
    }

    contentItem: ColumnLayout {

        // ---- Settings ----
        GridLayout {
            visible: !dialog.showProgress
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingM
            rowSpacing: Theme.spacingS

            Label { text: qsTr("Preset"); color: Theme.textMuted }
            ComboBox {
                id: presetBox
                Layout.fillWidth: true
                model: dialog.exporter.presets
                textRole: "name"
                onActivated: index => dialog.applyPreset(index)
                Accessible.name: qsTr("Export preset")
            }
            Item { width: 1; height: 1 }
            Text {
                Layout.fillWidth: true
                text: presetBox.currentIndex >= 0 ? dialog.exporter.presets[presetBox.currentIndex].description : ""
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Label { text: qsTr("Save to"); color: Theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: dialog.outputPath
                    onEditingFinished: dialog.outputPath = text
                    Accessible.name: qsTr("Output file")
                }
                FtButton { text: qsTr("Browse…"); onClicked: fileDialog.open() }
            }

            Label { text: qsTr("Quality"); color: Theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Best"); color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
                FtSlider {
                    id: quality
                    Layout.fillWidth: true
                    from: 14; to: 34; stepSize: 1; value: 20
                    Accessible.name: qsTr("Quality (constant rate factor)")
                }
                Text { text: qsTr("Smallest"); color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
                Text { text: "CRF " + quality.value; color: Theme.text; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSizeSmall }
            }

            Label { text: qsTr("Encoder speed"); color: Theme.textMuted }
            ComboBox {
                id: speedBox
                Layout.fillWidth: true
                model: dialog.exporter.speedPresets
                currentIndex: 5
                Accessible.name: qsTr("Encoder speed")
            }

            Label { text: qsTr("Audio"); color: Theme.textMuted }
            RowLayout {
                FtCheckBox { id: audioCheck; checked: true; text: qsTr("Include audio (AAC)") }
                ComboBox {
                    id: audioBox
                    enabled: audioCheck.checked
                    model: ["96", "128", "160", "192", "256", "320"]
                    currentIndex: 3
                    Accessible.name: qsTr("Audio bitrate in kilobits per second")
                }
                Text { text: qsTr("kbit/s"); color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
            }

            Item { width: 1; height: 1 }
            Text {
                Layout.fillWidth: true
                text: dialog.exporter.describeOutput(dialog.settings())
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { width: 1; height: 1 }
            Text {
                id: validation
                Layout.fillWidth: true
                text: dialog.opened ? dialog.exporter.validate(dialog.settings()) : ""
                visible: text.length > 0
                color: Theme.danger
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        // ---- Progress and result ----
        ColumnLayout {
            visible: dialog.showProgress
            Layout.fillWidth: true
            spacing: Theme.spacingM
            RowLayout {
                spacing: Theme.spacingS
                IconImage {
                    source: "qrc:/qt/qml/FrameTrellis/icons/" + (dialog.exporter.state === "succeeded" ? "check"
                            : dialog.exporter.state === "failed" ? "warning" : "export") + ".svg"
                    sourceSize.width: 22
                    sourceSize.height: 22
                    color: dialog.exporter.state === "succeeded" ? Theme.success
                         : dialog.exporter.state === "failed" ? Theme.danger : Theme.accent
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        switch (dialog.exporter.state) {
                        case "succeeded": return qsTr("Export finished")
                        case "failed": return qsTr("Export failed")
                        case "cancelled": return qsTr("Export cancelled")
                        case "cancelling": return qsTr("Cancelling…")
                        default: return qsTr("Exporting…")
                        }
                    }
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
            }
            Text {
                Layout.fillWidth: true
                text: dialog.exporter.outputPath
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WrapAnywhere
            }
            FtProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: dialog.exporter.progress
                indeterminate: dialog.exporter.state === "starting"
                Accessible.name: qsTr("Export progress")
            }
            Text {
                text: dialog.exporter.progressText + (dialog.exporter.encoder.length > 0 ? "  ·  " + dialog.exporter.encoder : "")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.exporter.state === "failed"
                text: dialog.exporter.errorMessage + (dialog.exporter.errorDetail.length > 0 ? "\n" + dialog.exporter.errorDetail : "")
                color: Theme.danger
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.exporter.busy
                text: qsTr("You can keep editing while the export runs; it uses a snapshot of the project.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton {
            text: qsTr("Show in folder")
            visible: dialog.exporter.state === "succeeded"
            onClicked: dialog.exporter.revealOutput()
        }
        FtButton {
            text: dialog.exporter.busy ? qsTr("Cancel export") : qsTr("Close")
            destructive: dialog.exporter.busy
            onClicked: dialog.exporter.busy ? dialog.exporter.cancel() : dialog.close()
        }
        FtButton {
            text: dialog.showProgress ? qsTr("New export") : qsTr("Export")
            primary: true
            visible: !dialog.exporter.busy
            enabled: dialog.showProgress || validation.text.length === 0
            iconName: "export"
            onClicked: dialog.showProgress ? dialog.exporter.reset() : dialog.startExport()
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog
    property var comparison: ({})
    property string checkpointLabel: ""
    readonly property bool validComparison: !comparison.error && comparison.schemaVersion === 1
    readonly property var categories: comparison.categories || []
    readonly property var details: comparison.details || []
    readonly property var categoryOptions: {
        const options = [{ code: "", title: qsTr("All categories") }]
        for (let i = 0; i < categories.length; ++i)
            options.push(categories[i])
        return options
    }
    readonly property var actionOptions: [
        { code: "", title: qsTr("All changes") },
        { code: "added", title: qsTr("Added") },
        { code: "removed", title: qsTr("Removed") },
        { code: "changed", title: qsTr("Changed") }
    ]
    readonly property var filteredDetails: {
        const category = categoryBox.currentIndex >= 0
            ? categoryOptions[categoryBox.currentIndex].code : ""
        const action = actionBox.currentIndex >= 0
            ? actionOptions[actionBox.currentIndex].code : ""
        const rows = []
        for (let i = 0; i < details.length; ++i) {
            const detail = details[i]
            if ((!category || detail.category === category) && (!action || detail.action === action))
                rows.push(detail)
        }
        return rows
    }

    title: qsTr("Checkpoint comparison")
    implicitWidth: 880
    height: Math.min(690, parent ? parent.height - 24 : 690)
    closePolicy: Popup.CloseOnEscape
    objectName: "checkpointCompareDialog"

    function showReport(report, label) {
        comparison = report
        checkpointLabel = label
        categoryBox.currentIndex = 0
        actionBox.currentIndex = 0
        tabs.currentIndex = 0
        open()
    }

    function actionLabel(code) {
        if (code === "added")
            return qsTr("Added")
        if (code === "removed")
            return qsTr("Removed")
        return qsTr("Changed")
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS
        Text {
            Layout.fillWidth: true
            text: qsTr("Checkpoint: %1").arg(dialog.checkpointLabel)
            color: Theme.text
            font.pixelSize: Theme.fontSizeLarge
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: dialog.comparison.error || qsTr("Changes are measured from the saved checkpoint to the current editing state. This comparison is read-only; opening it does not restore anything.")
            color: dialog.comparison.error ? Theme.danger : Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        Rectangle {
            visible: dialog.validComparison
            Layout.fillWidth: true
            implicitHeight: comparisonHeader.implicitHeight + 24
            color: Theme.panelAlt
            border.color: Theme.border
            radius: Theme.radius
            ColumnLayout {
                id: comparisonHeader
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                spacing: Theme.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%1 changed objects").arg(dialog.comparison.totalChanges || 0)
                    color: Theme.text
                    font.weight: Font.DemiBold
                    font.pixelSize: Theme.fontSizeTitle
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Checkpoint: %1 · %2 frames")
                        .arg(dialog.comparison.previousName || "")
                        .arg(dialog.comparison.previousDuration || 0)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Current: %1 · %2 frames")
                        .arg(dialog.comparison.currentName || "")
                        .arg(dialog.comparison.currentDuration || 0)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
            }
        }

        TabBar {
            id: tabs
            visible: dialog.validComparison
            Layout.fillWidth: true
            TabButton { text: qsTr("Summary") }
            TabButton { text: qsTr("Changes") }
        }
        StackLayout {
            visible: dialog.validComparison
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ListView {
                objectName: "checkpointComparisonCategories"
                clip: true
                spacing: Theme.spacingXs
                model: dialog.categories
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: categoryDelegate
                    required property var modelData
                    width: ListView.view.width
                    height: summaryRow.implicitHeight + 16
                    color: Theme.panelAlt
                    border.color: Theme.border
                    radius: Theme.radius
                    RowLayout {
                        id: summaryRow
                        anchors.fill: parent
                        anchors.margins: Theme.spacingS
                        Text {
                            Layout.fillWidth: true
                            text: categoryDelegate.modelData.title || ""
                            color: Theme.text
                            font.weight: Font.DemiBold
                            font.pixelSize: Theme.fontSize
                            elide: Text.ElideRight
                        }
                        Text {
                            text: qsTr("+%1").arg(categoryDelegate.modelData.added || 0)
                            color: Theme.success
                            font.family: Theme.monoFamily
                        }
                        Text {
                            text: qsTr("−%1").arg(categoryDelegate.modelData.removed || 0)
                            color: Theme.danger
                            font.family: Theme.monoFamily
                        }
                        Text {
                            text: qsTr("Changed %1").arg(categoryDelegate.modelData.changed || 0)
                            color: Theme.warning
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Text {
                            text: qsTr("Same %1").arg(categoryDelegate.modelData.unchanged || 0)
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        FtButton {
                            text: qsTr("View")
                            enabled: categoryDelegate.modelData.totalChanges > 0
                            onClicked: {
                                for (let i = 0; i < dialog.categoryOptions.length; ++i) {
                                    if (dialog.categoryOptions[i].code === categoryDelegate.modelData.code) {
                                        categoryBox.currentIndex = i
                                        break
                                    }
                                }
                                actionBox.currentIndex = 0
                                tabs.currentIndex = 1
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.spacingS
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: categoryBox
                        objectName: "checkpointComparisonCategoryFilter"
                        Layout.fillWidth: true
                        model: dialog.categoryOptions
                        textRole: "title"
                        Accessible.name: qsTr("Change category")
                    }
                    ComboBox {
                        id: actionBox
                        objectName: "checkpointComparisonActionFilter"
                        Layout.fillWidth: true
                        model: dialog.actionOptions
                        textRole: "title"
                        Accessible.name: qsTr("Change type")
                    }
                    Text {
                        text: qsTr("%1 shown").arg(dialog.filteredDetails.length)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: (dialog.comparison.omittedDetails || 0) > 0
                    text: qsTr("%1 additional changes are included in the totals but not the detail sample.")
                        .arg(dialog.comparison.omittedDetails || 0)
                    color: Theme.warning
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSizeSmall
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.panelAlt
                    border.color: Theme.border
                    radius: Theme.radius
                    clip: true
                    ListView {
                        id: detailList
                        objectName: "checkpointComparisonDetails"
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        spacing: Theme.spacingXs
                        model: dialog.filteredDetails
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: changeDelegate
                            required property var modelData
                            width: ListView.view.width
                            height: changeRow.implicitHeight + 16
                            color: Theme.panel
                            border.color: Theme.border
                            ColumnLayout {
                                id: changeRow
                                anchors.fill: parent
                                anchors.margins: Theme.spacingS
                                spacing: Theme.spacingXs
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: dialog.actionLabel(changeDelegate.modelData.action)
                                        color: changeDelegate.modelData.action === "added" ? Theme.success
                                            : changeDelegate.modelData.action === "removed" ? Theme.danger
                                            : Theme.warning
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · %2")
                                            .arg(changeDelegate.modelData.categoryTitle || "")
                                            .arg(changeDelegate.modelData.label || "")
                                        color: Theme.text
                                        font.weight: Font.DemiBold
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: (changeDelegate.modelData.fields || []).length > 0
                                    text: (changeDelegate.modelData.fields || []).join(" · ")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeSmall
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !!changeDelegate.modelData.before
                                    text: qsTr("Checkpoint: %1").arg(changeDelegate.modelData.before || "")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeSmall
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !!changeDelegate.modelData.after
                                    text: qsTr("Current: %1").arg(changeDelegate.modelData.after || "")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeSmall
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText
                                }
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        visible: detailList.count === 0
                        text: qsTr("No changes in this view.")
                        color: Theme.textMuted
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        Item { Layout.fillHeight: true; visible: !dialog.validComparison }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Close"); primary: true; onClicked: dialog.close() }
    }
}

import QtQuick 2.7
import QtQuick.Controls 1.5
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.2

import Qt.labs.platform 1.0 as Labs

import com.kajeka 1.0

import "Utils.js" as Utils

PluginContent
{
    id: root

    anchors.fill: parent
    minimumHeight: 320

    Action
    {
        id: toggleUiOrientationAction
        text: qsTr("Display UI &Side By Side")
        iconName: "list-add"
        checkable: true
        checked: true

        onCheckedChanged: { root.saveRequired = true; }
    }

    Action
    {
        id: resizeColumnsToContentsAction
        text: qsTr("&Resize Columns To Fit Contents")
        iconName: "format-justify-fill"
        onTriggered: tableView.resizeColumnsToContentsBugWorkaround();
    }

    Action
    {
        id: toggleColumnNamesAction
        text: qsTr("Show &Column Names")
        iconName: "format-text-bold"
        checkable: true
        checked: true

        onCheckedChanged: { root.saveRequired = true; }
    }

    Action
    {
        id: toggleCalculatedAttributes
        text: qsTr("&Show Calculated Attributes")
        iconName: "computer"
        checkable: true
        checked: true

        onCheckedChanged: { root.saveRequired = true; }
    }

    Action
    {
        id: savePlotImageAction
        text: qsTr("Save As &Image…")
        iconName: "edit-save"
        onTriggered: { imageSaveDialog.open(); }
    }

    function createMenu(index, menu)
    {
        switch(index)
        {
        case 0:
            tableView.populateTableMenu(menu);
            return true;

        case 1:
            menu.title = qsTr("&Plot");
            menu.addItem("").action = toggleColumnNamesAction;
            menu.addItem("").action = savePlotImageAction;

            Utils.cloneMenu(menu, plotContextMenu);
            return true;
        }

        return false;
    }

    toolStrip: RowLayout
    {
        anchors.fill: parent

        ToolButton { action: toggleUiOrientationAction }
        ToolButton { action: resizeColumnsToContentsAction }
        ToolButton { action: toggleColumnNamesAction }
        ToolButton { action: toggleCalculatedAttributes }
        Item { Layout.fillWidth: true }
    }

    SplitView
    {
        id: splitView

        orientation: toggleUiOrientationAction.checked ? Qt.Horizontal : Qt.Vertical

        anchors.fill: parent

        NodeAttributeTableView
        {
            id: tableView
            Layout.fillHeight: splitView.orientation === Qt.Vertical
            Layout.minimumHeight: splitView.orientation === Qt.Vertical ? 100 + (height - viewport.height) : -1
            Layout.minimumWidth: splitView.orientation === Qt.Horizontal ? 200 : -1

            nodeAttributesModel: plugin.model.nodeAttributeTableModel
            showCalculatedAttributes: toggleCalculatedAttributes.checked

            onVisibleRowsChanged:
            {
                selection.clear();

                if(rowCount > 0)
                    selection.selectAll();
            }

            onSortIndicatorColumnChanged: { root.saveRequired = true; }
            onSortIndicatorOrderChanged: { root.saveRequired = true; }
        }

        ColumnLayout
        {
            spacing: 0

            CorrelationPlot
            {
                id: plot

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: splitView.orientation === Qt.Vertical ? 100 : -1
                Layout.minimumWidth: splitView.orientation === Qt.Horizontal ? 200 : -1

                rowCount: plugin.model.rowCount
                columnCount: plugin.model.columnCount
                data: plugin.model.rawData
                columnNames: plugin.model.columnNames
                rowColors: plugin.model.nodeColors
                rowNames: plugin.model.rowNames
                selectedRows: tableView.selectedRows
                showColumnNames: toggleColumnNamesAction.checked

                elideLabelWidth:
                {
                    var newHeight = height * 0.25;
                    var quant = 20;
                    var quantised = Math.floor(newHeight / quant) * quant;

                    if(quantised < 40)
                        quantised = 0;

                    return quantised;
                }

                scrollAmount:
                {
                    return (scrollView.flickableItem.contentX) /
                            (scrollView.flickableItem.contentWidth - scrollView.viewport.width);
                }

                onRightClick: { plotContextMenu.popup(); }
            }

            ScrollView
            {
                id: scrollView
                visible: { return plot.rangeSize < 1.0 }
                verticalScrollBarPolicy: Qt.ScrollBarAlwaysOff
                implicitHeight: 15
                Layout.fillWidth: true
                Rectangle
                {
                    // This is a fake object to make native scrollbars appear
                    // Prevent Qt opengl texture overflow (2^14 pixels)
                    width: Math.min(plot.width / plot.rangeSize, 16383);
                    height: 1
                    color: "transparent"
                }
            }
        }
    }

    Menu { id: plotContextMenu }

    Labs.FileDialog
    {
        id: imageSaveDialog
        visible: false
        fileMode: Labs.FileDialog.SaveFile
        defaultSuffix: selectedNameFilter.extensions[0]
        selectedNameFilter.index: 1
        title: "Save Plot As Image"
        nameFilters: [ "PDF Document (*.pdf)", "PNG Image (*.png)", "JPEG Image (*.jpg *.jpeg)" ]
        onAccepted: { plot.savePlotImage(file, selectedNameFilter.extensions); }
    }

    property bool saveRequired: false

    function save()
    {
        var data =
        {
            "sideBySide": toggleUiOrientationAction.checked,
            "showCalculatedAttributes": toggleCalculatedAttributes.checked,
            "showColumnNames": toggleColumnNamesAction.checked,
            "sortColumn": tableView.sortIndicatorColumn,
            "sortOrder": tableView.sortIndicatorOrder,
            "hiddenColumns": tableView.hiddenColumns
        };

        return data;
    }

    function load(data, version)
    {
        if(data.sideBySide !== undefined)               toggleUiOrientationAction.checked = data.sideBySide;
        if(data.showCalculatedAttributes !== undefined) toggleCalculatedAttributes.checked = data.showCalculatedAttributes;
        if(data.showColumnNames !== undefined)          toggleColumnNamesAction.checked = data.showColumnNames;
        if(data.sortColumn !== undefined)               tableView.sortIndicatorColumn = data.sortColumn;
        if(data.sortOrder !== undefined)                tableView.sortIndicatorOrder = data.sortOrder;
        if(data.hiddenColumns !== undefined)            tableView.hiddenColumns = data.hiddenColumns;
    }
}



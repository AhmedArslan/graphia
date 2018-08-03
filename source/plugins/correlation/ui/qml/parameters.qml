import QtQuick 2.7
import QtQuick.Controls 1.5
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import com.kajeka 1.0

import "../../../../shared/ui/qml/Constants.js" as Constants
import "Controls"

ListTabDialog
{
    id: root
    //FIXME These should be set automatically by Wizard
    minimumWidth: 700
    minimumHeight: 500
    property int selectedRow: -1
    property int selectedCol: -1
    property bool _clickedCell: false

    // Work around for QTBUG-58594
    function resizeColumnsToContentsBugWorkaround(tableView)
    {
        for(var i = 0; i < tableView.columnCount; ++i)
        {
            var col = tableView.getColumn(i);
            var header = tableView.__listView.headerItem.headerRepeater.itemAt(i);
            if(col)
            {
                col.__index = i;
                col.resizeToContents();
                if(col.width < header.implicitWidth)
                    col.width = header.implicitWidth;
            }
        }
    }

    function isInsideRect(x, y, rect)
    {
        return  x >= rect.x &&
                x < rect.x + rect.width &&
                y >= rect.y &&
                y < rect.x + rect.height;
    }

    function scrollToCell(tableView, x, y)
    {
        if(x > 0)
            x--;
        if(y > 0)
            y--;

        var runningWidth = 0;
        for(var i = 0; i < x; ++i)
        {
            var col = tableView.getColumn(i);
            var header = tableView.__listView.headerItem.headerRepeater.itemAt(i);
            if(col !== null)
                runningWidth += col.width;
        }
        var runningHeight = 0;
        for(i = 0; i < y; ++i)
            runningHeight += dataRectView.__listView.contentItem.children[1].height;
        dataFrameAnimationX.to = runningWidth;
        dataFrameAnimationX.running = true;
        dataFrameAnimationY.to = runningHeight;
        dataFrameAnimationY.running = true;
    }

    ListTab
    {
        title: qsTr("Introduction")
        ColumnLayout
        {
            width: parent.width
            anchors.left: parent.left
            anchors.right: parent.right

            Text
            {
                text: qsTr("<h2>Correlation Graph Analysis</h2>")
                Layout.alignment: Qt.AlignLeft
                textFormat: Text.StyledText
            }

            RowLayout
            {
                Text
                {
                    text: qsTr("The correlation plugin creates graphs based on how similar row profiles are in a dataset.<br>" +
                               "<br>" +
                               "If specified, the input data will be scaled and normalised and a Pearson Correlation will be performed. " +
                               "The <a href=\"https://en.wikipedia.org/wiki/Pearson_correlation_coefficient\">Pearson Correlation coefficient</a> " +
                               "is effectively a measure of similarity between rows of data. It is used to determine " +
                               "whether or not an edge is created between rows.<br>" +
                               "<br>" +
                               "The edges may be filtered using transforms once the graph has been created.")
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    Layout.fillWidth: true

                    onLinkActivated: Qt.openUrlExternally(link);
                }

                Image
                {
                    Layout.minimumWidth: 100
                    Layout.minimumHeight: 100
                    sourceSize.width: 100
                    sourceSize.height: 100
                    source: "../plots.svg"
                }
            }
        }
    }

    ListTab
    {
        title: qsTr("Data Viewer")
        id: dataRectPage
        property bool _busy: preParser.isRunning || root.animating

        Connections
        {
            target: root
            onAnimatingChanged:
            {
                if(!root.animating && root.currentItem === dataRectPage)
                {
                    if(root.fileUrl !== "" && root.fileType !== "" && preParser.model.rowCount() === 0)
                        preParser.parse();
                }
            }
        }

        CorrelationPreParser
        {
            id: preParser
            fileType: root.fileType
            fileUrl: root.fileUrl
            onDataRectChanged:
            {
                parameters.dataFrame = dataRect;
                if(!isInsideRect(selectedCol, selectedRow, dataRect) &&
                        selectedCol >= 0 && selectedRow >= 0)
                {
                    scrollToCell(dataRectView, dataRect.x, dataRect.y)
                    tooltipNonNumerical.visible = _clickedCell;
                    _clickedCell = false;
                }
            }
            onDataLoaded:
            {
                repopulateTableView();
                Qt.callLater(scrollToCell, dataRectView, preParser.dataRect.x, preParser.dataRect.y);
            }
        }

        Component
        {
            id: columnComponent
            TableViewColumn { width: 200 }
        }

        ColumnLayout
        {
            anchors.fill: parent

            Text
            {
                text: qsTr("<h2>Data Viewer - Select and adjust</h2>")
                Layout.alignment: Qt.AlignLeft
                textFormat: Text.StyledText
            }

            Text
            {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("A contiguous numerical dataframe will automatically be selected from your dataset. " +
                           "If you would like to adjust the dataframe, select the new starting cell below.")
            }

            RowLayout
            {
                CheckBox
                {
                    id: transposeCheckBox

                    text: qsTr("Transpose Dataset")
                    onCheckedChanged:
                    {
                        parameters.transpose = checked;
                        preParser.transposed = checked;
                    }
                }
                HelpTooltip
                {
                    title: qsTr("Transpose")
                    Text
                    {
                        text: qsTr("Transpose will flip the data over it's diagonal. Moving rows to columns and vice versa")
                        Layout.alignment: Qt.AlignTop
                        textFormat: Text.StyledText
                    }
                }
            }

            Text
            {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("<b>Note:</b> Dataframes will always end at the last cell of the input")
            }

            TableView
            {
                // Hack to prevent TableView crawling when it adds a large number of columns
                // Should be fixed with new tableview?
                property int maxColumns: 200
                id: dataRectView
                headerVisible: false
                Layout.fillHeight: true
                Layout.fillWidth: true
                model: preParser.model
                selectionMode: SelectionMode.NoSelection
                enabled: !dataRectPage._busy

                PropertyAnimation
                {
                    id: dataFrameAnimationX
                    target: dataRectView.flickableItem
                    easing.type: Easing.InOutQuad
                    property: "contentX"
                    to: 0
                    duration: 750
                }

                PropertyAnimation
                {
                    id: dataFrameAnimationY
                    target: dataRectView.flickableItem
                    easing.type: Easing.InOutQuad
                    property: "contentY"
                    to: 0
                    duration: 750
                }

                Rectangle
                {
                    id: tooltipNonNumerical
                    color: sysPalette.light
                    border.color: sysPalette.mid
                    border.width: 1
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 25
                    anchors.horizontalCenter: parent.horizontalCenter
                    implicitWidth: messageText.width + 5
                    implicitHeight: messageText.height + 5
                    onVisibleChanged:
                    {
                        if(visible)
                            nonNumericalTimer.start();
                    }
                    visible: false

                    Timer
                    {
                        id: nonNumericalTimer
                        interval: 5000
                        onTriggered: { tooltipNonNumerical.visible = false }
                    }

                    Text
                    {
                        anchors.centerIn: parent
                        id: messageText
                        text: qsTr("Selected frame contains non-numerical data. " +
                                   "Next availaible frame selected");
                    }
                }

                BusyIndicator
                {
                    id: busyIndicator
                    anchors.centerIn: parent
                    running: dataRectPage._busy
                }

                SystemPalette
                {
                    id: sysPalette
                }

                itemDelegate: Item
                {
                    // Based on Qt source for BaseTableView delegate
                    height: Math.max(16, label.implicitHeight)
                    property int implicitWidth: label.implicitWidth + 16
                    clip: true

                    property var isInDataFrame:
                    {
                        return isInsideRect(styleData.column, styleData.row, preParser.dataRect);
                    }

                    Rectangle
                    {
                        Rectangle
                        {
                            anchors.right: parent.right
                            height: parent.height
                            width: 1
                            color: isInDataFrame ? sysPalette.light : sysPalette.mid
                        }

                        MouseArea
                        {
                            anchors.fill: parent
                            onClicked:
                            {
                                if(styleData.column === dataRectView.maxColumns)
                                    return;
                                tooltipNonNumerical.visible = false;
                                nonNumericalTimer.stop();
                                selectedCol = styleData.column
                                selectedRow = styleData.row
                                _clickedCell = true;
                                preParser.autoDetectDataRectangle(styleData.column, styleData.row);
                            }
                        }

                        width: parent.width
                        anchors.centerIn: parent
                        height: parent.height
                        color: isInDataFrame ? "lightblue" : "transparent"

                        Text
                        {
                            id: label
                            objectName: "label"
                            width: parent.width
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: styleData.hasOwnProperty("depth") && styleData.column === 0 ? 0 :
                                                                                                              horizontalAlignment === Text.AlignRight ? 1 : 8
                            anchors.rightMargin: (styleData.hasOwnProperty("depth") && styleData.column === 0)
                                                 || horizontalAlignment !== Text.AlignRight ? 1 : 8
                            horizontalAlignment: styleData.textAlignment
                            anchors.verticalCenter: parent.verticalCenter
                            elide: styleData.elideMode

                            text:
                            {
                                if(styleData.column >= dataRectView.maxColumns)
                                {
                                    if(styleData.row === 0)
                                    {
                                        return (preParser.model.columnCount() - dataRectView.maxColumns) +
                                                qsTr(" more columns...");
                                    }
                                    else
                                    {
                                        return "...";
                                    }
                                }

                                if(styleData.value === undefined)
                                    return "";

                                return styleData.value;
                            }

                            color: styleData.textColor
                            renderType: Text.NativeRendering
                        }
                    }
                }
            }

            Connections
            {
                target: preParser.model

                onModelReset:
                {
                    repopulateTableView();
                    selectedCol = 0;
                    selectedRow = 0;
                    preParser.autoDetectDataRectangle();
                }
            }
        }
    }

    function repopulateTableView()
    {
        while(dataRectView.columnCount > 0)
            dataRectView.removeColumn(0);

        dataRectView.model = null;
        dataRectView.model = preParser.model;
        for(var i = 0; i < preParser.model.columnCount(); i++)
        {
            dataRectView.addColumn(columnComponent.createObject(dataRectView,
                                                                {"role": i}));

            // Cap the column count since a huge number of columns causes a large slowdown
            if(i == dataRectView.maxColumns - 1)
            {
                // Add a blank
                dataRectView.addColumn(columnComponent.createObject(dataRectView));
                break;
            }
        }
        // Qt.callLater is used as the TableView will not generate the columns until
        // next loop has passed and there's no way to reliable listen for the change
        // (Thanks TableView)
        Qt.callLater(resizeColumnsToContentsBugWorkaround, dataRectView);
    }

    ListTab
    {
        title: qsTr("Correlation")
        ColumnLayout
        {
            anchors.left: parent.left
            anchors.right: parent.right

            Text
            {
                text: qsTr("<h2>Correlation - Adjust Thresholds</h2>")
                Layout.alignment: Qt.AlignLeft
                textFormat: Text.StyledText
            }

            ColumnLayout
            {
                spacing: 20

                Text
                {
                    text: qsTr("A Pearson Correlation will be performed on the dataset to provide a measure of correlation between rows of data. " +
                               "1.0 represents highly correlated rows and 0.0 represents no correlation. Negative correlation values are discarded. " +
                               "All values below the Minimum correlation value will also be discarded and will not create edges in the generated graph.<br>" +
                               "<br>" +
                               "By default a transform is created which will create edges for all values above the minimum correlation threshold. " +
                               "Is is not possible to create edges using values below the minimum correlation value.")
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    Layout.fillWidth: true
                }

                RowLayout
                {
                    Text
                    {
                        text: qsTr("Minimum Correlation:")
                        Layout.alignment: Qt.AlignRight
                    }

                    Item { Layout.fillWidth: true }

                    SpinBox
                    {
                        id: minimumCorrelationSpinBox

                        Layout.alignment: Qt.AlignLeft
                        implicitWidth: 70

                        minimumValue: 0.0
                        maximumValue: 1.0

                        decimals: 3
                        stepSize: (maximumValue - minimumValue) / 100.0

                        onValueChanged:
                        {
                            parameters.minimumCorrelation = value;
                            slider.value = value;
                        }
                    }

                    Slider
                    {
                        id: slider
                        minimumValue: 0.0
                        maximumValue: 1.0
                        onValueChanged:
                        {
                            minimumCorrelationSpinBox.value = value;
                        }
                    }
                }
            }
        }
    }

    ListTab
    {
        title: qsTr("Data Manipulation")
        ColumnLayout
        {
            anchors.left: parent.left
            anchors.right: parent.right

            Text
            {
                text: qsTr("<h2>Data Manipulation</h2>")
                Layout.alignment: Qt.AlignLeft
                textFormat: Text.StyledText
            }
            ColumnLayout
            {
                spacing: 10

                Text
                {
                    text: qsTr("Please select the required data manipulation. The manipulation " +
                               "will occur in the order it is displayed as below.")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                GridLayout
                {
                    columns: 3
                    Text
                    {
                        text: qsTr("Imputation:")
                        Layout.alignment: Qt.AlignLeft
                    }

                    ComboBox
                    {
                        id: missingDataMethod
                        Layout.alignment: Qt.AlignRight
                        model: ListModel
                        {
                            ListElement { text: qsTr("None");       value: MissingDataType.None }
                            ListElement { text: qsTr("Constant");   value: MissingDataType.Constant }
                        }
                        textRole: "text"

                        onCurrentIndexChanged:
                        {
                            parameters.missingDataType = model.get(currentIndex).value;
                        }
                    }

                    HelpTooltip
                    {
                        title: "Imputation"
                        GridLayout
                        {
                            columns: 2
                            Text
                            {
                                text: "<b>None:</b>"
                                textFormat: Text.StyledText
                                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                            }

                            Text
                            {
                                text: qsTr("All empty or missing values will be treated as zeroes");
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            Text
                            {
                                text: "<b>Constant:</b>"
                                textFormat: Text.StyledText
                                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                            }

                            Text
                            {
                                text: qsTr("Replace all missing values with a constant.");
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }

                    RowLayout
                    {
                        Layout.columnSpan: 3
                        Layout.alignment: Qt.AlignHCenter
                        visible: missingDataMethod.currentText === qsTr("Constant")

                        Text
                        {
                            text: qsTr("Value:")
                            Layout.alignment: Qt.AlignLeft
                        }

                        TextField
                        {
                            id: replacementConstant
                            validator: DoubleValidator{}

                            onTextChanged:
                            {
                                parameters.missingDataValue = text;
                            }

                            text: "0.0"
                        }
                    }

                    Text
                    {
                        text: qsTr("Scaling:")
                        Layout.alignment: Qt.AlignLeft
                    }

                    ComboBox
                    {
                        id: scaling
                        Layout.alignment: Qt.AlignRight
                        model: ListModel
                        {
                            ListElement { text: qsTr("None");          value: ScalingType.None }
                            ListElement { text: qsTr("Log2(𝒙 + ε)");   value: ScalingType.Log2 }
                            ListElement { text: qsTr("Log10(𝒙 + ε)");  value: ScalingType.Log10 }
                            ListElement { text: qsTr("AntiLog2(𝒙)");   value: ScalingType.AntiLog2 }
                            ListElement { text: qsTr("AntiLog10(𝒙)");  value: ScalingType.AntiLog10 }
                            ListElement { text: qsTr("Arcsin(𝒙)");     value: ScalingType.ArcSin }
                        }
                        textRole: "text"

                        onCurrentIndexChanged:
                        {
                            parameters.scaling = model.get(currentIndex).value;
                        }
                    }

                    HelpTooltip
                    {
                        title: qsTr("Scaling Types")
                        GridLayout
                        {
                            columns: 2
                            rowSpacing: 10

                            Text
                            {
                                text: "<b>Log</b>𝒃(𝒙 + ε):"
                                Layout.alignment: Qt.AlignTop
                                textFormat: Text.StyledText
                            }

                            Text
                            {
                                text: qsTr("Will perform a Log of 𝒙 + ε to base 𝒃, where 𝒙 is the input data and ε is a very small constant.");
                                wrapMode: Text.WordWrap
                                Layout.alignment: Qt.AlignTop
                                Layout.fillWidth: true
                            }

                            Text
                            {
                                text: "<b>AntiLog</b>𝒃(𝒙):"
                                Layout.alignment: Qt.AlignTop
                                textFormat: Text.StyledText
                            }

                            Text
                            {
                                text: qsTr("Will raise x to the power of 𝒃, where 𝒙 is the input data.");
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            Text
                            {
                                text: "<b>Arcsin</b>(𝒙):"
                                Layout.alignment: Qt.AlignTop
                                textFormat: Text.StyledText
                            }

                            Text
                            {
                                text: qsTr("Will perform an inverse sine function of 𝒙, where 𝒙 is the input data. This is useful when " +
                                           "you require a log transform but the dataset contains negative numbers or zeros.");
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                    }

                    Text
                    {
                        text: qsTr("Normalisation:")
                        Layout.alignment: Qt.AlignLeft
                    }

                    ComboBox
                    {
                        id: normalise
                        Layout.alignment: Qt.AlignRight
                        model: ListModel
                        {
                            ListElement { text: qsTr("None");       value: NormaliseType.None }
                            ListElement { text: qsTr("MinMax");     value: NormaliseType.MinMax }
                            ListElement { text: qsTr("Quantile");   value: NormaliseType.Quantile }
                        }
                        textRole: "text"

                        onCurrentIndexChanged:
                        {
                            parameters.normalise = model.get(currentIndex).value;
                        }
                    }

                    HelpTooltip
                    {
                        title: "Normalisation Types"
                        GridLayout
                        {
                            columns: 2
                            Text
                            {
                                text: "<b>MinMax:</b>"
                                textFormat: Text.StyledText
                                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                            }

                            Text
                            {
                                text: qsTr("Normalise the data so 1.0 is the maximum value of that column and 0.0 the minimum. " +
                                           "This is useful if the columns in the dataset have differing scales or units. " +
                                           "Note: If all elements in a column have the same value this will rescale the values to 0.0.");
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            Text
                            {
                                text: "<b>Quantile:</b>"
                                textFormat: Text.StyledText
                                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                            }

                            Text
                            {
                                text: qsTr("Normalise the data so that the columns have equal distributions.");
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }

    ListTab
    {
        title: "Initial Transforms"
        ColumnLayout
        {
            anchors.left: parent.left
            anchors.right: parent.right
            Text
            {
                text: qsTr("<h2>Graph Transforms</h2>")
                Layout.alignment: Qt.AlignLeft
                textFormat: Text.StyledText
            }

            Text
            {
                text: qsTr("Commonly used transforms can be automatically added to " +
                           "the graph from here")
                Layout.alignment: Qt.AlignLeft
            }

            GridLayout
            {
                columns: 3
                Text
                {
                    text: qsTr("Clustering:")
                    Layout.alignment: Qt.AlignLeft
                }

                ComboBox
                {
                    id: clustering
                    Layout.alignment: Qt.AlignRight
                    model: ListModel
                    {
                        ListElement { text: qsTr("None");  value: ClusteringType.None }
                        ListElement { text: qsTr("MCL");   value: ClusteringType.MCL }
                    }
                    textRole: "text"

                    onCurrentIndexChanged:
                    {
                        parameters.clusteringType = model.get(currentIndex).value;
                    }
                }

                HelpTooltip
                {
                    title: "Clustering"
                    GridLayout
                    {
                        columns: 2
                        Text
                        {
                            text: "<b>MCL:</b>"
                            textFormat: Text.StyledText
                            Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                        }

                        Text
                        {
                            text: qsTr("Markov clustering algorithm simulates stochastic flow within the generated graph to identify " +
                                       "distinct clusters. ");
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                Text
                {
                    text: qsTr("Edge Reduction:")
                    Layout.alignment: Qt.AlignLeft
                }

                ComboBox
                {
                    id: edgeReduction
                    Layout.alignment: Qt.AlignRight
                    model: ListModel
                    {
                        ListElement { text: qsTr("None");  value: EdgeReductionType.None }
                        ListElement { text: qsTr("K-NN");   value: EdgeReductionType.KNN }
                    }
                    textRole: "text"

                    onCurrentIndexChanged:
                    {
                        parameters.edgeReductionType = model.get(currentIndex).value;
                    }
                }

                HelpTooltip
                {
                    title: qsTr("Edge Reduction")
                    ColumnLayout
                    {
                        Text
                        {
                            text: "Edge-reduction attempts to reduce unnecessary or non-useful edges"
                            textFormat: Text.StyledText
                            Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                        }

                        GridLayout
                        {
                            columns: 2
                            Text
                            {
                                text: "<b>K-NN:</b>"
                                textFormat: Text.StyledText
                                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                            }

                            Text
                            {
                                text: qsTr("K-Nearest neighbour ranks node edges and only keeps <i>k</i> number of edges per node");
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }

    ListTab
    {
        title: "Summary"
        ColumnLayout
        {
            anchors.fill: parent

            Text
            {
                text: qsTr("<h2>Summary</h2>")
                Layout.alignment: Qt.AlignLeft
                textFormat: Text.StyledText
            }

            Text
            {
                text: qsTr("A graph will be created with the following parameters")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextArea
            {
                Layout.fillWidth: true
                Layout.fillHeight: true
                readOnly: true
                text:
                {
                    var summaryString = "";
                    summaryString += "Minimum Correlation: " + minimumCorrelationSpinBox.value + "\n";
                    if(preParser.dataRect != Qt.rect(0,0,0,0))
                    {
                        summaryString += "Data Frame:" +
                                " x: " + preParser.dataRect.x +
                                " y: " + preParser.dataRect.y +
                                " width: " + preParser.dataRect.width +
                                " height: " + preParser.dataRect.height + "\n";
                    }
                    summaryString += "Data Transpose: " + transposeCheckBox.checked + "\n";
                    summaryString += "Data Scaling: " + scaling.currentText + "\n";
                    summaryString += "Data Normalise: " + normalise.currentText + "\n";
                    summaryString += "Missing Data Replacement: " + missingDataMethod.currentText + "\n";
                    if(missingDataMethod.model.get(missingDataMethod.currentIndex).value === MissingDataType.Constant)
                        summaryString += "Replacement Constant: " + replacementConstant.text + "\n";
                    var transformString = ""
                    if(clustering.model.get(clustering.currentIndex).value !== ClusteringType.None)
                        transformString += "Clustering (" + clustering.currentText + ")\n";
                    if(edgeReduction.model.get(edgeReduction.currentIndex).value !== EdgeReductionType.None)
                        transformString += "Edge Reduction (" + edgeReduction.currentText + ")\n";
                    if(!transformString)
                        transformString = "None"
                    summaryString += "Intial Transforms: " + transformString;
                    return summaryString;
                }
            }
        }
    }

    Component.onCompleted: initialise();
    function initialise()
    {
        parameters = { minimumCorrelation: 0.7, transpose: false,
            scaling: ScalingType.None, normalise: NormaliseType.None,
            missingDataType: MissingDataType.None };

        minimumCorrelationSpinBox.value = 0.7;
        transposeCheckBox.checked = false;
    }
}

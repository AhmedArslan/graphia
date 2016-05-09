import QtQuick 2.6
import QtQuick.Controls 1.5
import QtQuick.Layouts 1.1
import QtQuick.Dialogs 1.2

import "../app/ui/qml/Constants.js" as Constants

ApplicationWindow
{
    id: window
    visible: true
    flags: Qt.Window|Qt.Dialog

    title: Qt.application.name + " " + qsTr("Crash Reporter")

    width: 640
    height: 480
    minimumWidth: 640
    minimumHeight: 480

    property bool enabled: true

    GridLayout
    {
        id: grid
        anchors.fill: parent
        anchors.margins: Constants.margin
        columns: 2

        Image
        {
            id: icon
            source: "icon.svg"
            sourceSize.width: 96
            sourceSize.height: 96
            Layout.margins: Constants.margin
            Layout.rowSpan: 2
        }

        Text
        {
            id: info
            text: qsTr("<b>Oops!</b> We're sorry, " + Qt.application.name + " has crashed. " +
                       "Please use the form below to let us know what happened. " +
                       "If we need more information, we may use your email address " +
                       "to contact you. Thanks.")
            wrapMode: Text.WordWrap
            Layout.margins: Constants.margin
            Layout.fillWidth: true
        }

        TextField
        {
            id: email
            enabled: window.enabled
            placeholderText: qsTr("Email address (optional)")
            validator: RegExpValidator { regExp:/\w+([-+.']\w+)*@\w+([-.]\w+)*\.\w+([-.]\w+)*/ }
            Layout.fillWidth: true
        }

        PlaceholderTextArea
        {
            id: description
            enabled: window.enabled
            placeholderText: qsTr("A detailed explanation of what you were doing immediately prior to the crash (optional)")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.columnSpan: 2
        }

        Button
        {
            enabled: window.enabled
            text: qsTr("Send Report")
            Layout.columnSpan: 2
            anchors.right: grid.right
            onClicked:
            {
                window.enabled = false;
                report.email = email.text;
                report.text = description.text;
                Qt.quit();
            }
        }
    }
}

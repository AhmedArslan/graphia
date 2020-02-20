/* Copyright © 2013-2020 Graphia Technologies Ltd.
 *
 * This file is part of Graphia.
 *
 * Graphia is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Graphia is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Graphia.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 2.7
import QtQuick.Controls 1.5
import QtQuick.Layouts 1.3

import "Controls"
import "../../../shared/ui/qml/Constants.js" as Constants

Rectangle
{
    id: root

    property var document

    readonly property bool showing: _visible
    property bool _visible: false

    width: row.width
    height: row.height

    border.color: document.contrastingColor
    border.width: 1
    radius: 4
    color: "white"

    Action
    {
        id: closeAction
        text: qsTr("Close")
        iconName: "emblem-unreadable"

        onTriggered:
        {
            _visible = false;
            hidden();
        }
    }

    RowLayout
    {
        id: row

        // The ColumnLayout in a RowLayout is just a hack to get some padding
        ColumnLayout
        {
            Layout.topMargin: Constants.padding
            Layout.bottomMargin: Constants.padding - root.parent.parent.anchors.bottomMargin
            Layout.leftMargin: Constants.padding + Constants.margin
            Layout.rightMargin: Constants.padding

            RowLayout
            {
                Label { font.bold: true; text: document.layoutDisplayName }
                Item { Layout.fillWidth: true }
                FloatingButton { action: closeAction }
            }

            Repeater
            {
                model: document.layoutSettings

                RowLayout
                {
                    Label
                    {
                        id: label

                        MouseArea
                        {
                            anchors.fill: parent

                            onDoubleClicked:
                            {
                                document.resetLayoutSettingValue(modelData);
                                var setting = document.layoutSetting(modelData);
                                slider.value = setting.normalisedValue;
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Slider
                    {
                        id: slider
                        minimumValue: 0.0
                        maximumValue: 1.0

                        onValueChanged:
                        {
                            if(pressed)
                            {
                                document.setLayoutSettingNormalisedValue(modelData, value);
                                root.valueChanged();
                            }
                        }
                    }

                    Component.onCompleted:
                    {
                        var setting = document.layoutSetting(modelData);
                        label.text = setting.displayName;
                        slider.value = setting.normalisedValue;
                    }
                }
            }
        }
    }

    function show()
    {
        root._visible = true;
        shown();
    }

    function hide()
    {
        closeAction.trigger();
    }

    function toggle()
    {
        if(root._visible)
            hide();
        else
            show();
    }

    signal shown();
    signal hidden();

    signal valueChanged();
}

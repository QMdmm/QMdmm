// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

// A push button drawn where it stands, rather than an image of one. The outline of the drawn
// shape is the outline of the button, so the area that takes a click is the area that is on
// screen -- the image this used to be was a pill inside a square, and it took clicks over the
// corners that are not part of the pill.
//
// `enabled` is Item's own, so the dimming below is all this adds: a disabled button's mouse
// area is disabled with it, and a press that lands on one reaches nothing.
Rectangle {
    id: qMdmmButton

    property bool checkable: false
    property bool checked: false
    property alias text: label.text

    signal clicked
    signal doubleClicked

    border.color: mouse.pressed || checked ? "#1d4518" : "#31682a"
    border.width: 2
    color: mouse.pressed ? "#2d6427" : checked ? "#3b7f32" : mouse.containsMouse ? "#67ba55" : "#529c45"
    opacity: enabled ? 1 : 0.4
    radius: Math.min(width, height) / 3

    Text {
        id: label

        anchors.centerIn: parent
        color: "white"
        fontSizeMode: Text.Fit
        height: parent.height / 8 * 7
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        width: parent.width / 4 * 3
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true

        onClicked: {
            if (checkable)
                qMdmmButton.checked = !qMdmmButton.checked;
            qMdmmButton.clicked();
        }
        onDoubleClicked: qMdmmButton.doubleClicked()
    }
}

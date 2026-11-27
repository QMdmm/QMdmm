// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

Item {
    property Loader contentLoader: contentLoader
    property string title

    Text {
        font.pixelSize: game.fontSizeBody
        text: title
    }

    Loader {
        id: contentLoader

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 25
        height: (parent.height - 25) / 16 * 15
        width: parent.width / 16 * 15
    }
}

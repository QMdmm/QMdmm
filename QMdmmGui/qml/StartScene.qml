// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

Item {
    id: scene

    signal aboutClicked
    signal configureClicked
    signal startGameClicked

    anchors.fill: parent

    Item {
        height: 384
        width: parent.width / 4
        x: parent.width / 8 * 3
        y: 512

        Grid {
            anchors.fill: parent
            columns: 1

            Button {
                height: parent.height / 3
                text: qsTr("Start game")
                width: parent.width

                onClicked: scene.startGameClicked()
            }

            Button {
                height: parent.height / 3
                text: qsTr("Configuration")
                width: parent.width

                onClicked: scene.configureClicked()
            }

            Button {
                height: parent.height / 3
                text: qsTr("About")
                width: parent.width

                onClicked: scene.aboutClicked()
            }
        }
    }
}

// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

Item {
    id: scene

    signal back
    signal connectOnline(string host, string name)
    signal startLocal(string name)

    anchors.fill: parent

    // dim background
    Rectangle {
        anchors.fill: parent
        color: "#66000000"
    }

    Item {
        anchors.centerIn: parent
        height: 560
        width: 720

        Rectangle {
            anchors.fill: parent
            border.color: "#555"
            color: "#1b1b1b"
            radius: 12
        }

        Text {
            id: title

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 24
            color: "white"
            font.pixelSize: 40
            text: qsTr("Connect Game")
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: title.bottom
            anchors.topMargin: 24
            color: "#ccc"
            text: qsTr("Your name")
        }

        Rectangle {
            id: nameBox

            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: title.bottom
            anchors.topMargin: 56
            border.color: "#666"
            color: "#000"
            height: 56
            radius: 6
            width: 600

            TextInput {
                id: nameInput

                anchors.fill: parent
                anchors.margins: 8
                color: "white"
                font.pixelSize: 28
                text: "You"
                verticalAlignment: TextInput.AlignVCenter
            }
        }

        // Local mode
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: nameBox.bottom
            anchors.topMargin: 28
            color: "#ccc"
            text: qsTr("Local play (in-process server + bots fill the room)")
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: nameBox.bottom
            anchors.topMargin: 64
            color: "white"
            font.pixelSize: 28
            text: qsTr("Players: ") + game.playerCount
        }

        Button {
            anchors.left: parent.left
            anchors.leftMargin: 200
            anchors.top: nameBox.bottom
            anchors.topMargin: 56
            height: 48
            text: "-"
            width: 48

            onClicked: game.playerCount = Math.max(1, game.playerCount - 1)
        }

        Button {
            anchors.left: parent.left
            anchors.leftMargin: 260
            anchors.top: nameBox.bottom
            anchors.topMargin: 56
            height: 48
            text: "+"
            width: 48

            onClicked: game.playerCount = Math.min(6, game.playerCount + 1)
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: 60
            anchors.top: nameBox.bottom
            anchors.topMargin: 52
            height: 56
            text: qsTr("Start local game")
            width: 220

            onClicked: scene.startLocal(nameInput.text)
        }

        // Online mode
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: parent.top
            anchors.topMargin: 300
            color: "#ccc"
            text: qsTr("Online mode (connect to an external server, e.g. qmdmm://host:6366)")
        }

        Rectangle {
            id: hostBox

            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: parent.top
            anchors.topMargin: 332
            border.color: "#666"
            color: "#000"
            height: 48
            radius: 6
            width: 420

            TextInput {
                id: hostInput

                anchors.fill: parent
                anchors.margins: 8
                color: "white"
                font.pixelSize: 22
                text: "qmdmm://localhost:6366"
                verticalAlignment: TextInput.AlignVCenter
            }
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: 60
            anchors.top: parent.top
            anchors.topMargin: 328
            height: 56
            text: qsTr("Connect")
            width: 180

            onClicked: scene.connectOnline(hostInput.text, nameInput.text)
        }

        Button {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            anchors.left: parent.left
            anchors.leftMargin: 60
            height: 48
            text: qsTr("Back")
            width: 120

            onClicked: scene.back()
        }
    }
}

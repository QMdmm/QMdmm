// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

Image {
    id: rootItem

    anchors.fill: parent
    source: "../assets/1.jpg"

    StartScene {
        id: startScene

        anchors.fill: parent
        visible: game.gameState === "start"

        onAboutClicked: aboutPopup.visible = true
        onConfigureClicked: configPopup.visible = true
        onStartGameClicked: connectScene.visible = true
    }

    ConnectScene {
        id: connectScene

        anchors.fill: parent
        visible: false

        onBack: visible = false
        onConnectOnline: (host, name) => {
            visible = false;
            game.connectOnline(host, name);
        }
        onStartLocal: name => {
            visible = false;
            game.startLocalGame(name);
        }
    }

    GameScene {
        id: gameScene

        anchors.fill: parent
        visible: game.gameState === "lobby" || game.gameState === "playing" || game.gameState === "gameover"
    }

    // The client's own status and its error reports. It lives here rather than in a
    // scene because either can happen while any of them is showing, and it has to stay
    // below the popups, which are modal.
    StatusBar {
        id: statusBar

        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 40
        width: parent.width / 2 - 50
    }

    // ---- simple popups ----------------------------------------------------

    Item {
        id: configPopup

        anchors.fill: parent
        visible: false

        Rectangle {
            anchors.fill: parent
            color: "#80000000"
        }

        Item {
            anchors.centerIn: parent
            height: 200
            width: 600

            Rectangle {
                anchors.fill: parent
                border.color: "#555"
                color: "#222"
                radius: 10
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 16
                color: "white"
                text: qsTr("Player count (local)")
            }

            Text {
                id: cfgCount

                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 48
                text: game.playerCount
            }

            Button {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                height: 64
                source: "../assets/btn.png"
                text: "-"
                width: 120

                onClicked: game.playerCount = Math.max(1, game.playerCount - 1)
            }

            Button {
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                height: 64
                source: "../assets/btn.png"
                text: "+"
                width: 120

                onClicked: game.playerCount = Math.min(6, game.playerCount + 1)
            }

            Button {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.horizontalCenter: parent.horizontalCenter
                height: 56
                source: "../assets/btn.png"
                text: qsTr("OK")
                width: 160

                onClicked: configPopup.visible = false
            }
        }
    }

    Item {
        id: aboutPopup

        anchors.fill: parent
        visible: false

        Rectangle {
            anchors.fill: parent
            color: "#80000000"

            MouseArea {
                anchors.fill: parent

                onClicked: aboutPopup.visible = false
            }
        }

        Item {
            anchors.centerIn: parent
            height: 360
            width: 700

            Rectangle {
                anchors.fill: parent
                border.color: "#555"
                color: "#222"
                radius: 10
            }

            Text {
                anchors.fill: parent
                anchors.margins: 24
                color: "white"
                text: qsTr(
                          "QMdmm - a playful brawler\n\nA turn-based multiplayer mini-game. Built with Qt 6 / C++20, AGPL-3.0.\n\nLocal mode starts an in-process server and fills the room with bots,\nso you can play a full match alone. Online mode connects to an external server.")
                wrapMode: Text.Wrap
            }

            Button {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.horizontalCenter: parent.horizontalCenter
                height: 56
                source: "../assets/btn.png"
                text: qsTr("Close")
                width: 160

                onClicked: aboutPopup.visible = false
            }
        }
    }
}

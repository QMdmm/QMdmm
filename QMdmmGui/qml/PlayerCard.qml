// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

import "."

Item {
    id: card

    // The state the protocol carries for this player, as the broadcast hands it over: a mask
    // (Data::AgentState) with online = 0x10, bot = 0x01, managed = 0x08. Only the number
    // reaches the view, so spelling it out is the card's job -- the managed flag has nowhere
    // else on screen to show up.
    property int agentState: 0
    // Only your own card carries the switch -- nobody hands another player over -- and only while
    // that player is online, since the declaration a click makes needs a connection to travel on.
    readonly property bool canManage: you && (agentState & stateOnline) !== 0
    property string displayName
    // The managed flag is the one bit of the state that is set from this side rather than only
    // read, and the switch beside the state line is where it is set. See game.setManaged.
    readonly property bool managed: (agentState & stateManaged) !== 0
    property var player
    readonly property int stateBot: 0x01
    readonly property int stateManaged: 0x08
    readonly property int stateOnline: 0x10
    property bool you

    function stateText(state) {
        const words = [(state & stateOnline) ? qsTr("Online") : qsTr("Offline")];
        if (state & stateBot)
            words.push(qsTr("Bot"));
        if (state & stateManaged)
            words.push(qsTr("Managed"));
        return words.join(", ");
    }

    height: 300
    width: 280

    Rectangle {
        anchors.fill: parent
        border.color: you ? "#7c4" : "#555"
        border.width: you ? 3 : 1
        color: you ? "#223322" : "#222222"
        radius: 10
    }

    Text {
        id: nameText

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 10
        color: "white"
        elide: Text.ElideRight
        font.pixelSize: 28
        horizontalAlignment: Text.AlignHCenter
        text: displayName + (you ? qsTr(" (you)") : "")
        width: parent.width - 20
    }

    // HP bar
    Rectangle {
        id: hpBack

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: nameText.bottom
        anchors.topMargin: 14
        color: "#000"
        height: 26
        radius: 4
        width: parent.width - 40

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.top: parent.top
            color: player.hp > player.maxHp * 0.3 ? "#4caf50" : "#e53935"
            radius: 4
            width: parent.width * Math.max(0, Math.min(1, player.hp / Math.max(1, player.maxHp)))
        }

        Text {
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 18
            text: player.hp + " / " + player.maxHp
        }
    }

    Row {
        id: itemsRow

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: hpBack.bottom
        anchors.topMargin: 16
        spacing: 12

        Image {
            fillMode: Image.PreserveAspectFit
            height: 48
            source: "../assets/knife.png"
            visible: player.hasKnife
            width: 48
        }

        Image {
            fillMode: Image.PreserveAspectFit
            height: 48
            source: "../assets/horse.png"
            visible: player.hasHorse
            width: 48
        }
    }

    Row {
        id: stateRow

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: itemsRow.bottom
        anchors.topMargin: 8
        spacing: 10

        Text {
            color: "#9fd0ff"
            font.pixelSize: 20
            height: 28
            text: card.stateText(card.agentState)
            verticalAlignment: Text.AlignVCenter
        }

        // Handing the player over: the fill shows the flag, the click asks for the opposite of
        // what is on show, and the card redraws from the state the server broadcasts back -- a
        // local flip would not survive the next broadcast.
        Rectangle {
            border.color: card.managed ? "#7c4" : "#888"
            border.width: 1
            color: card.managed ? "#2f5f2f" : "#444"
            height: 28
            objectName: "managedToggle"
            radius: 6
            visible: card.canManage
            width: toggleLabel.width + 16

            Text {
                id: toggleLabel

                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 16
                text: qsTr("Manage")
            }

            MouseArea {
                anchors.fill: parent

                onClicked: game.setManaged(!card.managed)
            }
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        anchors.horizontalCenter: parent.horizontalCenter
        color: "#ddd"
        font.pixelSize: 20
        text: qsTr("Place: %1").arg(game.placeName(player.place))
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        color: "#ddd"
        font.pixelSize: 20
        text: qsTr("Upgrade points: %1").arg(player.upgradePoint)
    }

    Rectangle {
        anchors.fill: parent
        color: "#aa000000"
        radius: 10
        visible: player.dead

        Text {
            anchors.centerIn: parent
            color: "#f55"
            font.pixelSize: 40
            text: qsTr("Out")
        }
    }
}

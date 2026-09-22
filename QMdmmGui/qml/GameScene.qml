// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

import "."

Item {
    id: scene

    property var actionOptions: []

    // ---- properties / logic ----------------------------------------------
    property string activeRequest: ""

    // What each player in the room is, as the server broadcasts it: the cards in the players
    // row spell out their own entry (see PlayerCard). The map follows every change -- a join,
    // a drop, the managed flag.
    property var agentStates: game.agentStates

    // A view-only log of what the other players did, built from the operation
    // broadcasts the client re-emits as result signals. The match itself is
    // driven by the requests above, which only ever show *your* turn -- without
    // this, everyone else's picks, actions and upgrades happen invisibly. The
    // start of the match and of each round go in as markers, so the lines below
    // them can be read per round (round over is the banner, not a line).
    property var matchLog: []
    // Total number of action orders, on top of the ones still free: the
    // selection overlay shows the range the picks come from.
    property int orderMaximum: 0
    property int orderNeed: 0
    property var orderOptions: []
    property int orderRemaining: 0
    property var orderSelected: []
    // The rock-paper-scissors request: the order the throw is for (0 = the right
    // to act this round) and the internal names of the players in that throw --
    // everyone still alive for 0, everyone striving for that order otherwise.
    property int rpsOrder: 0
    property var rpsRivals: []
    // The rules the match is played under, mirrored from the logic configuration
    // the server broadcasts when a player joins the room. Every field it carries
    // has to be readable on screen; the strip below the top bar spells them out.
    property var rules: game.logicConfiguration
    // The action order the action request is for, i.e. the turn being played.
    property int turnOrder: 0
    property int upgradeNeed: 0
    property var upgradeOptions: []
    property int upgradeRemaining: 0
    property var upgradeSelected: []

    // One action broadcast -> one line. Which of toPlayer / toPlace carries
    // the information is decided by the action: the wire leaves the other unset.
    function actionLine(playerName, action, toPlayer, toPlace) {
        const who = game.screenName(playerName);
        if (action === 1)
            return qsTr("%1 bought a knife").arg(who);
        if (action === 2)
            return qsTr("%1 bought a horse").arg(who);
        if (action === 3)
            return qsTr("%1 slashed %2").arg(who).arg(game.screenName(toPlayer));
        if (action === 4)
            return qsTr("%1 kicked %2").arg(who).arg(game.screenName(toPlayer));
        if (action === 5)
            return qsTr("%1 moved to %2").arg(who).arg(game.placeName(toPlace));
        if (action === 6)
            return qsTr("%1 moved %2 to %3").arg(who).arg(game.screenName(toPlayer)).arg(game.placeName(toPlace));
        return qsTr("%1 did nothing").arg(who);
    }

    // One player's state out of the broadcast map. A player the map has not caught up with
    // reads as offline rather than leaving the card blank.
    function agentStateOf(playerName) {
        const state = agentStates[playerName];
        return state === undefined ? 0 : state;
    }

    function appendMatchLog(text) {
        if (!text)
            return;
        // Re-assign rather than mutate in place: a plain JS array carries no
        // change signal, so a Repeater bound to it would never see the line.
        const items = matchLog.slice();
        items.push(text);
        if (items.length > 200)
            items.splice(0, items.length - 200);
        matchLog = items;
    }

    // How the punish HP is rounded, spelled out -- the wire carries only the
    // enum's integer value.
    function punishStrategyName(strategy) {
        if (strategy === 0)
            return qsTr("rounded down");
        if (strategy === 1)
            return qsTr("rounded to nearest");
        if (strategy === 2)
            return qsTr("rounded up");
        if (strategy === 3)
            return qsTr("rounded down, plus one");
        return "?";
    }

    function rpsName(rps) {
        if (rps === 0)
            return qsTr("Rock");
        if (rps === 1)
            return qsTr("Scissors");
        if (rps === 2)
            return qsTr("Paper");
        return "?";
    }

    // The request names the players in the struggle by internal name -- all of
    // them, the local player included; the overlay shows them by screen name.
    function rpsPlayerNames() {
        const names = [];
        for (let i = 0; i < rpsRivals.length; ++i)
            names.push(game.screenName(rpsRivals[i]));
        return names.join(", ");
    }

    // What the throw is for, and who is in it. strivedOrder 0 means the throw
    // decides the right to act this round, in which case the core names every
    // player still alive; any other value is an action order, and the core names
    // the players striving for that very order.
    function rpsStakeText() {
        const players = rpsPlayerNames();
        if (rpsOrder === 0)
            return qsTr("This throw is for the right to act this round (players: %1)").arg(players);
        return qsTr("This throw is for action order %1 (contested by %2)").arg(rpsOrder).arg(players);
    }

    // A "<what>: <initial> (up to <maximum>)" rule, for the three damage / HP
    // pairs the configuration carries.
    function ruleRange(label, from, to) {
        return qsTr("%1: %2 (up to %3)").arg(label).arg(from).arg(to);
    }

    // The rules, one line per group, built so that every field the logic
    // configuration carries is readable: the numbers (HP and damage, initial and
    // maximum) and the switches (punish, 0 HP, let-move, where buying is
    // allowed). The values are the wire's own -- nothing is recomputed here.
    function rulesLines() {
        if (!rules || rules.initialMaxHp === undefined)
            return [];
        const hp = ruleRange(qsTr("max HP"), rules.initialMaxHp, rules.maximumMaxHp);
        const knife = ruleRange(qsTr("knife damage"), rules.initialKnifeDamage, rules.maximumKnifeDamage);
        const horse = ruleRange(qsTr("horse damage"), rules.initialHorseDamage, rules.maximumHorseDamage);
        const shares = qsTr("max HP / %1").arg(rules.punishHpModifier);
        const strategy = punishStrategyName(rules.punishHpRoundStrategy);
        const punish = rules.punishHpModifier > 0 ? qsTr("slash self-punish: %1, %2").arg(shares).arg(strategy) : qsTr("slash self-punish: off");
        const death = rules.zeroHpAsDead ? qsTr("0 HP counts as dead") : qsTr("0 HP is still alive");
        const letMove = rules.enableLetMove ? qsTr("let-move allowed") : qsTr("let-move not allowed");
        const buy = rules.canBuyOnlyInInitialCity ? qsTr("buy: starting city only") : qsTr("buy: any city");
        return [hp + " | " + knife + " | " + horse, punish + " | " + death + " | " + letMove + " | " + buy];
    }

    function toggleOrder(v) {
        const i = orderSelected.indexOf(v);
        if (i >= 0) {
            orderSelected.splice(i, 1);
        } else if (orderRemaining > 0) {
            orderSelected.push(v);
        }
        orderRemaining = orderNeed - orderSelected.length;
    }

    function toggleUpgrade(v) {
        const i = upgradeSelected.indexOf(v);
        if (i >= 0) {
            upgradeSelected.splice(i, 1);
        } else if (upgradeRemaining > 0) {
            upgradeSelected.push(v);
        }
        upgradeRemaining = upgradeNeed - upgradeSelected.length;
    }

    function upgradeItemName(item) {
        if (item === 0)
            return qsTr("knife damage");
        if (item === 1)
            return qsTr("horse damage");
        if (item === 2)
            return qsTr("max HP");
        return "?";
    }

    anchors.fill: parent

    // ---- top bar ----------------------------------------------------------
    Rectangle {
        id: topbar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        color: "#cc000000"
        height: 56

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            color: "white"
            font.pixelSize: 28
            text: {
                if (game.gameState === "lobby")
                    return qsTr("Lobby: waiting for other players...");
                if (game.gameState === "playing")
                    return qsTr("Match in progress");
                if (game.gameState === "gameover")
                    return qsTr("Match over");
                return "";
            }
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            height: 44
            source: "../assets/btn.png"
            text: qsTr("Disconnect")
            width: 120

            onClicked: game.disconnectAll()
        }
    }

    // ---- player panels ----------------------------------------------------
    Row {
        id: playersRow

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: topbar.bottom
        anchors.topMargin: 16
        spacing: 16

        Repeater {
            model: game.players

            PlayerCard {
                agentState: scene.agentStateOf(modelData.objectName)
                displayName: game.screenName(modelData.objectName)
                player: modelData
                you: game.isYou(modelData.objectName)
            }
        }
    }

    // ---- log / chat -------------------------------------------------------
    Item {
        id: logArea

        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.left: parent.left
        anchors.leftMargin: 40
        anchors.top: playersRow.bottom
        anchors.topMargin: 12
        width: parent.width / 2 - 50

        Rectangle {
            anchors.fill: parent
            color: "#33000000"
            radius: 8
        }

        // The rules strip: the logic configuration arrives once and the log below
        // scrolls away, so the rules get their own permanent lines at the top.
        Column {
            id: rulesStrip

            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 8
            spacing: 2

            Repeater {
                model: scene.rulesLines()

                Text {
                    color: "#ffd54f"
                    font.pixelSize: 18
                    text: modelData
                    width: rulesStrip.width
                    wrapMode: Text.Wrap
                }
            }
        }

        Flickable {
            id: logFlick

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: 8
            anchors.right: parent.right
            anchors.top: rulesStrip.bottom
            clip: true
            contentHeight: logCol.height

            Column {
                id: logCol

                spacing: 4
                width: parent.width

                Repeater {
                    model: scene.matchLog

                    Text {
                        color: "#9fd0ff"
                        font.pixelSize: 20
                        text: modelData
                        width: logArea.width - 16
                        wrapMode: Text.Wrap
                    }
                }

                Repeater {
                    model: game.chatLog

                    Text {
                        color: game.isYou(modelData.name) ? "#ffe082" : "white"
                        font.pixelSize: 22
                        text: game.screenName(modelData.name) + ": " + modelData.content
                        width: logArea.width - 16
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: 8
            anchors.right: parent.right
            border.color: "#666"
            color: "#000"
            height: 44
            radius: 6

            TextInput {
                id: chatInput

                anchors.fill: parent
                anchors.margins: 6
                color: "white"
                font.pixelSize: 20
                verticalAlignment: TextInput.AlignVCenter

                onAccepted: {
                    game.speak(text);
                    text = "";
                }
            }
        }
    }

    // ---- request overlay --------------------------------------------------
    Item {
        id: requestOverlay

        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 40
        anchors.top: playersRow.bottom
        anchors.topMargin: 12
        visible: activeRequest !== ""
        width: parent.width / 2 - 50

        Rectangle {
            anchors.fill: parent
            color: "#44000000"
            radius: 8
        }

        // rps
        Column {
            anchors.centerIn: parent
            spacing: 12
            visible: activeRequest === "rps"

            Text {
                color: "white"
                font.pixelSize: 30
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Rock-paper-scissors!")
                width: requestOverlay.width
            }

            Text {
                color: "#ccc"
                font.pixelSize: 22
                horizontalAlignment: Text.AlignHCenter
                text: rpsStakeText()
                width: requestOverlay.width
                wrapMode: Text.Wrap
            }

            Row {
                spacing: 12

                Button {
                    height: 80
                    source: "../assets/btn.png"
                    text: qsTr("Rock")
                    width: 120

                    onClicked: {
                        game.replyRps(0);
                        activeRequest = "";
                    }
                }

                Button {
                    height: 80
                    source: "../assets/btn.png"
                    text: qsTr("Scissors")
                    width: 120

                    onClicked: {
                        game.replyRps(1);
                        activeRequest = "";
                    }
                }

                Button {
                    height: 80
                    source: "../assets/btn.png"
                    text: qsTr("Paper")
                    width: 120

                    onClicked: {
                        game.replyRps(2);
                        activeRequest = "";
                    }
                }
            }
        }

        // Action order
        Column {
            anchors.centerIn: parent
            spacing: 10
            visible: activeRequest === "order"

            Text {
                color: "white"
                font.pixelSize: 26
                text: qsTr("Choose action order (%1 more)").arg(orderRemaining)
                width: requestOverlay.width
                wrapMode: Text.Wrap
            }

            Text {
                color: "#ccc"
                font.pixelSize: 22
                text: qsTr("Action orders go from 1 to %1").arg(orderMaximum)
                width: requestOverlay.width
                wrapMode: Text.Wrap
            }

            Flow {
                spacing: 8
                width: requestOverlay.width - 20

                Repeater {
                    model: orderOptions

                    Button {
                        height: 56
                        source: "../assets/btn.png"
                        text: String(modelData)
                        width: 90

                        onClicked: toggleOrder(modelData)
                    }
                }
            }

            Row {
                spacing: 12

                Button {
                    enabled: orderRemaining === 0
                    height: 56
                    source: "../assets/btn.png"
                    text: qsTr("Confirm order")
                    width: 200

                    onClicked: {
                        game.replyActionOrder(orderSelected);
                        activeRequest = "";
                    }
                }

                Button {
                    height: 56
                    source: "../assets/btn.png"
                    text: qsTr("Yield (auto-assign)")
                    width: 200

                    onClicked: {
                        game.yieldActionOrder(orderNeed);
                        activeRequest = "";
                    }
                }
            }
        }

        // Action
        Column {
            anchors.centerIn: parent
            spacing: 10
            visible: activeRequest === "action"

            Text {
                color: "white"
                font.pixelSize: 28
                text: qsTr("Your turn to act")
            }

            Text {
                color: "#ccc"
                font.pixelSize: 22
                text: qsTr("Action order %1").arg(turnOrder)
            }

            Flow {
                spacing: 8
                width: requestOverlay.width - 20

                Repeater {
                    model: actionOptions

                    Button {
                        height: 56
                        source: "../assets/btn.png"
                        text: modelData.label
                        width: Math.min(260, requestOverlay.width - 20)

                        onClicked: {
                            game.replyAction(modelData.action, modelData.target, modelData.place);
                            activeRequest = "";
                        }
                    }
                }
            }
        }

        // Upgrade
        Column {
            anchors.centerIn: parent
            spacing: 10
            visible: activeRequest === "upgrade"

            Text {
                color: "white"
                font.pixelSize: 26
                text: qsTr("Upgrade (%1 more)").arg(upgradeRemaining)
                width: requestOverlay.width
                wrapMode: Text.Wrap
            }

            Flow {
                spacing: 8
                width: requestOverlay.width - 20

                Repeater {
                    model: upgradeOptions

                    Button {
                        height: 56
                        source: "../assets/btn.png"
                        text: modelData.label
                        width: 240

                        onClicked: toggleUpgrade(modelData.item)
                    }
                }
            }

            Row {
                spacing: 12

                Button {
                    enabled: upgradeRemaining === 0
                    height: 56
                    source: "../assets/btn.png"
                    text: qsTr("Confirm upgrade")
                    width: 200

                    onClicked: {
                        game.replyUpgrade(upgradeSelected);
                        activeRequest = "";
                    }
                }

                Button {
                    height: 56
                    source: "../assets/btn.png"
                    text: qsTr("Default (HP)")
                    width: 200

                    onClicked: {
                        for (var i = 0; i < upgradeNeed; ++i)
                            upgradeSelected.push(2);
                        game.replyUpgrade(upgradeSelected);
                        activeRequest = "";
                    }
                }
            }
        }
    }

    // ---- round / game over banners ---------------------------------------
    Item {
        id: banner

        anchors.fill: parent
        visible: false

        Rectangle {
            anchors.fill: parent
            color: "#aa000000"
        }

        Text {
            id: bannerText

            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 64
            text: ""
        }
    }

    Connections {
        function onActionOrderResult(result) {
            // The map is keyed by the order number, "1".."N": key i carries the
            // player taking order i. Object.keys returns those integer-like keys
            // in ascending order.
            const keys = Object.keys(result);
            const names = [];
            for (let i = 0; i < keys.length; ++i)
                names.push(game.screenName(result[keys[i]]));
            appendMatchLog(qsTr("Action order: %1").arg(names.join(" then ")));
        }

        function onActionResult(playerName, action, toPlayer, toPlace) {
            appendMatchLog(actionLine(playerName, action, toPlayer, toPlace));
        }

        function onGameOver(winners) {
            activeRequest = "";
            let names = [];
            for (let i = 0; i < winners.length; ++i)
                names.push(game.screenName(winners[i]));
            bannerText.text = qsTr("Winner(s): ") + names.join(", ");
            banner.visible = true;
        }

        function onGameStart() {
            appendMatchLog(qsTr("Match started"));
        }

        function onRequestAction(currentOrder) {
            actionOptions = game.getActionOptions();
            turnOrder = currentOrder;
            activeRequest = "action";
        }

        function onRequestActionOrder(remainedOrders, maximumOrder, selectionNum) {
            orderOptions = remainedOrders;
            orderMaximum = maximumOrder;
            orderSelected = [];
            orderNeed = selectionNum;
            orderRemaining = selectionNum;
            activeRequest = "order";
        }

        function onRequestRockPaperScissors(playerNames, strivedOrder) {
            rpsRivals = playerNames;
            rpsOrder = strivedOrder;
            activeRequest = "rps";
        }

        function onRequestUpgrade(remainingTimes) {
            upgradeOptions = game.getUpgradeOptions();
            upgradeSelected = [];
            upgradeNeed = remainingTimes;
            upgradeRemaining = remainingTimes;
            activeRequest = "upgrade";
        }

        function onRequestWithdrawn() {
            // Handing this player over answers the request that is on screen as well, so the
            // overlay has to come down with it -- left up, it would keep asking for a decision
            // that is no longer open. The requests after it are given up as they arrive and never
            // reach the scene at all.
            activeRequest = "";
        }

        function onRoundOver() {
            bannerText.text = qsTr("Round over");
            banner.visible = true;
            bannerTimer.start();
        }

        function onRoundStart() {
            appendMatchLog(qsTr("Round started"));
        }

        function onRpsResult(results) {
            const parts = [];
            for (let name in results)
                parts.push(qsTr("%1 (%2)").arg(game.screenName(name)).arg(rpsName(results[name])));
            appendMatchLog(qsTr("Rock-paper-scissors: %1").arg(parts.join(", ")));
        }

        function onUpgradeResult(upgrades) {
            const parts = [];
            for (let name in upgrades) {
                const items = [];
                for (let i = 0; i < upgrades[name].length; ++i)
                    items.push(upgradeItemName(upgrades[name][i]));
                parts.push(qsTr("%1 (%2)").arg(game.screenName(name)).arg(items.join(", ")));
            }
            appendMatchLog(qsTr("Upgrades: %1").arg(parts.join(", ")));
        }

        target: game
    }

    Timer {
        id: bannerTimer

        interval: 1200
        repeat: false

        onTriggered: banner.visible = false
    }
}

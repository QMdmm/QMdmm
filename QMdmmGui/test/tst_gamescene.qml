// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 2.15
import QtTest 1.2

// Smoke test for the match log GameScene builds out of the operation-result
// signals: the rock-paper-scissors picks, the action order, the actions and the
// upgrades of *every* player have to show up there, not only the ones the local
// player makes. That is the gap this guards -- the request handlers only ever
// cover your own turn. The match / round start markers are guarded here too:
// they carry no operation data, so nothing else would put them on screen. The
// request overlays are guarded the same way: a request carries more than the
// choices it asks for (what the throw is for and who is in it, the range the
// orders come from, which turn is being acted on) and none of that is visible
// unless the overlay says it. The rules strip is guarded here for the same
// reason: the logic configuration is broadcast once and nothing else on screen
// would show what the match is played under. The agent-state chain is guarded
// here end to end: the scene is what picks each player's entry out of the
// broadcast map, the card that renders it is guarded in tst_playercard, and the
// case that plays a local game through the scene is what pins the two together
// -- the cards a live room builds have to read their state off that map.
//
// Like tst_scene.qml, the scene is loaded from the source tree (the QMdmm.Gui
// module resource lives in the QMdmm6 executable, which this test does not
// link). It reads the client through the `game` context property, which
// tst_qmdmmgui.cpp installs the same way MainWindow does. Emitting the client's
// result signals is what the networking side does when a notification arrives.
TestCase {
    id: testCase

    // Recursive search for a rendered Text, so a case can assert on what the
    // overlay actually says rather than on the properties behind it.
    function hasText(root, text) {
        for (let i = 0; i < root.children.length; ++i) {
            const c = root.children[i];
            if (c.text === text)
                return true;
            if (hasText(c, text))
                return true;
        }
        return false;
    }

    function makeScene() {
        const comp = Qt.createComponent(Qt.resolvedUrl("../qml/GameScene.qml"));
        tryCompare(comp, "status", Component.Ready);
        verify(comp.status === Component.Ready, "GameScene should load");

        const scene = createTemporaryObject(comp, testCase);
        verify(scene !== null, "GameScene should instantiate");
        return scene;
    }

    function test_aWithdrawnRequestTakesTheOverlayDown() {
        const scene = makeScene();

        // Handing this player over answers the request that is on screen as well, so the overlay
        // has to come down with it -- left up, it keeps asking for a decision that is no longer
        // open. The requests after it never reach the view at all, so nothing else would put this
        // case on trial.
        game.requestAction(3);
        compare(scene.activeRequest, "action");

        game.requestWithdrawn();
        compare(scene.activeRequest, "");
    }

    function test_actionOrderRequestShowsTheRange() {
        const scene = makeScene();

        // The request carries the number of action orders on top of the ones
        // still free; the range is what the buttons alone cannot tell.
        game.requestActionOrder([3, 4], 5, 2);

        compare(scene.orderMaximum, 5);
        verify(hasText(scene, "Action orders go from 1 to 5"));
    }

    function test_actionOrderResultIsLogged() {
        const scene = makeScene();

        // Key "1" is the first order, so it decides who acts first.
        game.actionOrderResult({
                                   "1": "p2",
                                   "2": "p1"
                               });

        compare(scene.matchLog.length, 1);
        compare(scene.matchLog[0], "Action order: p2 then p1");
    }

    function test_actionRequestShowsTheCurrentOrder() {
        const scene = makeScene();

        game.requestAction(3);

        compare(scene.turnOrder, 3);
        verify(hasText(scene, "Action order 3"));
    }

    function test_actionResultIsLogged() {
        const scene = makeScene();

        // One line per action kind: every branch of the action-to-text mapping
        // gets guarded, not just the interesting ones.
        game.actionResult("p1", 0, "", 0); // DoNothing
        game.actionResult("p1", 1, "", -1); // BuyKnife
        game.actionResult("p1", 2, "", -1); // BuyHorse
        game.actionResult("p1", 3, "p2", -1); // Slash
        game.actionResult("p1", 4, "p2", -1); // Kick
        game.actionResult("p1", 5, "", 2); // Move (toPlace carries the target)
        game.actionResult("p1", 6, "p2", 2); // LetMove (both carry information)

        compare(scene.matchLog.length, 7);
        compare(scene.matchLog[0], "p1 did nothing");
        compare(scene.matchLog[1], "p1 bought a knife");
        compare(scene.matchLog[2], "p1 bought a horse");
        compare(scene.matchLog[3], "p1 slashed p2");
        compare(scene.matchLog[4], "p1 kicked p2");
        compare(scene.matchLog[5], "p1 moved to City 2");
        compare(scene.matchLog[6], "p1 moved p2 to City 2");
    }

    function test_agentStateOfAPlayerTheMapDoesNotKnow() {
        const scene = makeScene();

        // The map is filled per player, so a card built the moment its player joins can read
        // before that player's entry is in. That has to come out as offline, not as a blank
        // line behind a blank card. The map is handed to the scene the way tst_gameclient hands
        // the client's real one over, so the lookup itself is on trial here.
        scene.agentStates = {
            "p1": 0x18
        };

        compare(scene.agentStateOf("p1"), 0x18);
        compare(scene.agentStateOf("p2"), 0);
    }

    function test_eachCardTakesItsStateOffTheMap() {
        // The last step of the chain, and the one nothing else in the suite reaches: the scene
        // builds the cards out of the room mirror and hands each one its entry. A card wired to a
        // fixed state instead of the lookup reads as offline -- or as anybody's state -- and no
        // other case would notice, because no other case plays a room through a scene.
        const scene = makeScene();

        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryVerify(function () {
            return hasText(scene, "Online, Bot") && hasText(scene, "Online") && !hasText(scene, "Offline");
        }, 15000);

        verify(!hasText(scene, "Offline"), "no card may read offline while every entry is in the map");

        // The cards have to be gone before the room is: each one holds a player the room owns, and
        // a room that dies under them takes the process down with it. destroy() defers the
        // deletion, so the wait is what actually lands it -- disconnecting before that is the
        // crash. The framework then finds nothing left to destroy, which it handles: it skips
        // objects that are already gone.
        scene.destroy();
        wait(200);
        game.disconnectAll();
    }

    function test_gameStartIsLogged() {
        const scene = makeScene();

        game.gameStart();

        compare(scene.matchLog.length, 1);
        compare(scene.matchLog[0], "Match started");
    }

    function test_matchLogIsBounded() {
        const scene = makeScene();

        // A whole match is long; the log must not grow without end.
        for (let i = 0; i < 250; ++i)
            game.rpsResult({
                               "p1": 0
                           });

        compare(scene.matchLog.length, 200);
    }

    function test_roundStartIsLogged() {
        const scene = makeScene();

        game.roundStart();

        compare(scene.matchLog.length, 1);
        compare(scene.matchLog[0], "Round started");
    }

    function test_rpsRequestShowsTheStake() {
        const scene = makeScene();

        // strivedOrder 0 = the throw decides the right to act this round.
        game.requestRockPaperScissors(["p1", "p2"], 0);

        compare(scene.rpsOrder, 0);
        compare(scene.rpsRivals.length, 2);
        verify(hasText(scene, "This throw is for the right to act this round (players: p1, p2)"));

        // A non-zero strivedOrder = the throw is for that very action order,
        // and the payload names the players striving for it.
        game.requestRockPaperScissors(["p1"], 3);

        compare(scene.rpsOrder, 3);
        verify(hasText(scene, "This throw is for action order 3 (contested by p1)"));
    }

    function test_rpsResultIsLogged() {
        const scene = makeScene();

        // All three throws, so no branch of the throw-to-text mapping is left
        // unguarded.
        game.rpsResult({
                           "p1": 0,
                           "p2": 1,
                           "p3": 2
                       });

        compare(scene.matchLog.length, 1);
        compare(scene.matchLog[0], "Rock-paper-scissors: p1 (Rock), p2 (Scissors), p3 (Paper)");
    }

    function test_rulesShowTheBroadcastConfiguration() {
        const scene = makeScene();

        // The rules the server broadcast are how the players know what they are playing under.
        // The wiring behind them (client -> scene) is guarded in tst_gameclient; here the strip
        // itself is on trial, so the scene is handed a configuration to render -- the default
        // rules of a local game.
        scene.rules = {
            "initialKnifeDamage": 1,
            "maximumKnifeDamage": 10,
            "initialHorseDamage": 2,
            "maximumHorseDamage": 10,
            "initialMaxHp": 10,
            "maximumMaxHp": 20,
            "punishHpModifier": 2,
            "punishHpRoundStrategy": 1,
            "zeroHpAsDead": true,
            "enableLetMove": true,
            "canBuyOnlyInInitialCity": false
        };

        verify(hasText(scene, "max HP: 10 (up to 20) | knife damage: 1 (up to 10) | horse damage: 2 (up to 10)"));
        verify(hasText(scene, "slash self-punish: max HP / 2, rounded to nearest | 0 HP counts as dead | let-move allowed | buy: any city"));
    }

    function test_rulesShowTheOtherSideOfEverySwitch() {
        const scene = makeScene();

        // The other direction of every switch: no punish at all (the modifier is a divisor, so
        // zero turns it off), 0 HP is not death yet, let-move is off, and buying is tied to the
        // starting city. All four are read off the broadcast map, so none of them may be
        // hard-coded to the default.
        scene.rules = {
            "initialKnifeDamage": 1,
            "maximumKnifeDamage": 3,
            "initialHorseDamage": 3,
            "maximumHorseDamage": 5,
            "initialMaxHp": 7,
            "maximumMaxHp": 7,
            "punishHpModifier": 0,
            "punishHpRoundStrategy": 0,
            "zeroHpAsDead": false,
            "enableLetMove": false,
            "canBuyOnlyInInitialCity": true
        };

        verify(hasText(scene, "max HP: 7 (up to 7) | knife damage: 1 (up to 3) | horse damage: 3 (up to 5)"));
        verify(hasText(scene, "slash self-punish: off | 0 HP is still alive | let-move not allowed | buy: starting city only"));
    }

    function test_upgradeResultIsLogged() {
        const scene = makeScene();

        // All three upgrade items, for the same reason.
        game.upgradeResult({
                               "p1": [0, 1, 2]
                           });

        compare(scene.matchLog.length, 1);
        compare(scene.matchLog[0], "Upgrades: p1 (knife damage, horse damage, max HP)");
    }

    name: "GameScene"
}

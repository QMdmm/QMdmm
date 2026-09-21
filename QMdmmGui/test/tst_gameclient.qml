// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 2.15
import QtTest 1.2

import QMdmm.Gui 1.0

// Smoke test for the QMdmmGameClient bridge: drives a local game through the
// exact entry point the QML UI uses (startLocalGame), verifies the room fills
// and the match reaches the playing state, then checks the human's request /
// reply round trip actually advances the match. This is the runtime verification
// the GUI previously lacked.
TestCase {
    id: testCase

    property var game: null

    function cleanup() {
        if (game !== null) {
            game.disconnectAll();
            game.destroy();
        }
        game = null;
    }

    function init() {
        game = gameComponent.createObject(testCase);
        verify(game !== null, "GameClient should instantiate");
    }

    function test_agentStatesFollowTheRoom() {
        // Every player's agent state is broadcast with the player list, and the bridge has to
        // keep the map the player cards read in step with it -- a bot has to read as a bot, not
        // as an anonymous online player. The values are what is waited for, not a change count:
        // startLocalGame() clears the mirror on the way in and announces that too, so a count
        // on its own would be satisfied before a single player is in. The count that is asserted
        // is per arrival, which is what makes the announcement itself on trial.
        var announced = createTemporaryObject(signalSpyComponent, testCase, {
                                                  target: game,
                                                  signalName: "agentStatesChanged"
                                              });

        game.playerCount = 3;
        game.startLocalGame("Tester");

        tryVerify(function () {
            return game.players.length === 3 && Object.keys(game.agentStates).length === 3;
        }, 15000);

        compare(game.agentStates[game.localName], 0x10); // the human signs in as an online agent
        var names = Object.keys(game.agentStates);
        var bots = 0;
        for (var i = 0; i < names.length; ++i) {
            if (game.agentStates[names[i]] === 0x11) // online + bot
                ++bots;
        }
        compare(bots, 2);
        verify(announced.count >= game.players.length, "every arriving player has to be announced");

        // A state that changes later (the managed toggle, a drop, a reconnect) rides on the same
        // map through the client's state-change notification, but this case only watches the
        // states players arrive with -- a drop and a reconnect are the wire's to stage. The
        // notification half has its own case below (the managed flag), so taking that connect out
        // turns the suite red.
    }

    function test_botObjectNameDiffersFromScreenName() {
        // 1 human + 1 auto-replying bot. The bot's internal objectName (the
        // protocol-level player identity) must stay distinct from its display
        // screenName: addBot must not call setObjectName(name) -- doing so would
        // detach the self agent's key from the sign-in playerName (A4 forbids
        // renaming). This guards the Release builds where the Debug-only Q_ASSERT
        // in ClientP::connectSocket is compiled out.
        game.playerCount = 2;
        var added = createTemporaryObject(signalSpyComponent, testCase, {
                                              target: game,
                                              signalName: "playerAdded"
                                          });
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);

        var sawBot = false;
        for (var i = 0; i < added.count; ++i) {
            var args = added.signalArguments[i];
            var playerName = args[0];
            var screenName = args[1];
            if (screenName.indexOf("Bot") === 0) {
                sawBot = true;
                verify(playerName !== screenName, "bot objectName must differ from its display screenName");
            }
        }
        verify(sawBot, "expected to observe a bot player");

        game.disconnectAll();
    }

    function test_localGameFillsRoomAndStarts() {
        // 1 human + 1 auto-replying bot -> room fills and the match starts.
        game.playerCount = 2;
        game.startLocalGame("Tester");

        tryCompare(game, "gameState", "playing", 15000);
        compare(game.players.length, 2);
        verify(game.isYou(game.localName));

        game.disconnectAll();
        compare(game.gameState, "start");
        compare(game.players.length, 0);
    }

    function test_logicConfigurationArrivesWithTheLocalGame() {
        // The rules are broadcast when the player joins the room, before the match starts, and
        // the bridge has to pass them on rather than only keep them in the room mirror: reading
        // the mirror is not the same as being told that they are there. Without the agent
        // notification the rules strip stays empty for the whole match.
        //
        // Wait for the values, not for a change count: startLocalGame() clears the mirror on the
        // way in and announces that too, so counting signals would pass before the rules are in.
        var rules = createTemporaryObject(signalSpyComponent, testCase, {
                                              target: game,
                                              signalName: "logicConfigurationChanged"
                                          });

        game.playerCount = 2;
        game.startLocalGame("Tester");

        tryVerify(function () {
            return game.logicConfiguration.initialMaxHp === 10;
        }, 15000);

        // A local game runs on LogicConfiguration::defaults().
        compare(game.logicConfiguration.maximumMaxHp, 20);
        compare(game.logicConfiguration.enableLetMove, true);
        compare(game.logicConfiguration.canBuyOnlyInInitialCity, false);
        verify(rules.count > 0, "the rules arriving has to be announced");
    }

    function test_replyDrivesTheMatch() {
        var rps = createTemporaryObject(signalSpyComponent, testCase, {
                                            target: game,
                                            signalName: "requestRockPaperScissors"
                                        });
        var rpsResult = createTemporaryObject(signalSpyComponent, testCase, {
                                                  target: game,
                                                  signalName: "rpsResult"
                                              });

        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);

        // The server asks the human for a rock-paper-scissors pick.
        tryCompare(rps, "count", 1, 15000);

        // The auto-replying bot answers its own RPS request, so the human's reply
        // below completes the round and produces an rpsResult broadcast. Require the
        // human's reply to yield *at least one more* result rather than an exact
        // count -- an exact count depends on how the match advances, which is
        // fragile: under the old 80 ms request-timeout default the auto-advancing
        // match kept settling and the count reached 194.
        //
        // Note: this test intentionally does NOT rely on the request-timeout
        // fallback. requestTimeout is now seconds-scale (20 s + 60 s grace, see
        // ServerConfiguration), far longer than the 15 s window below, so a human
        // that stops replying leaves the match waiting; the bot's auto-reply is
        // the only thing driving progress.
        var before = rpsResult.count;
        game.replyRps(0); // rock

        tryVerify(function () {
            return rpsResult.count > before;
        }, 15000);
    }

    function test_theManagedFlagIsDeclaredAndComesBack() {
        // The one piece of an agent's state the client sets rather than only reads. The
        // declaration is not taken as the final word: the server applies the flag and broadcasts
        // the new state back, and the map the player cards read has to follow the broadcast --
        // which is why the wait is for the value and not for a local change. The reply is what
        // makes this the round trip rather than a set and forget.
        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryVerify(function () {
            return game.agentStates[game.localName] === 0x10;
        }, 15000);

        game.setManaged(true);
        tryVerify(function () {
            return game.agentStates[game.localName] === 0x18;
        }, 15000);

        game.setManaged(false);
        tryVerify(function () {
            return game.agentStates[game.localName] === 0x10;
        }, 15000);

        game.disconnectAll();
    }

    name: "GameClient"

    Component {
        id: gameComponent

        GameClient {
        }
    }

    Component {
        id: signalSpyComponent

        SignalSpy {
        }
    }
}

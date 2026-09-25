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
        // A fresh bridge knows nothing of the two programs a local game needs until it is told,
        // exactly as the product tells it at startup (from the paths the command line carries).
        // Here there is no command line, so the empty pair is handed over -- which is what makes
        // both programs be looked up next to this test.
        game.setProgramPaths("", "");
    }

    function test_aBareNameIsHandedOverAsALocalSocketName() {
        // The address reaches the client exactly as it was typed: which transport a string names
        // is the networking layer's call (the scheme whitelist), and a string with no scheme names
        // a local socket. The bridge must not write a scheme in on the way -- the name below read
        // as a TCP host is a hostname that does not exist, and the room would never fill.
        //
        // The name is the one a local game's server listens on (the configuration defaults), and
        // that server is left running in the other bridge for the length of this case, so there is
        // something at the other end of the name rather than only a failure to compare against.
        var host = createTemporaryObject(gameComponent, testCase, {
                                             playerCount: 2
                                         });
        // A bridge made here does not go through init(), so it is told the same way -- it has to
        // find the server program before it can start the game the joiner below connects to.
        host.setProgramPaths("", "");
        host.startLocalGame("Host");

        var joiner = createTemporaryObject(gameComponent, testCase, {});
        joiner.connectOnline("QMdmm", "Joiner");

        tryVerify(function () {
            return joiner.players.length >= 1;
        }, 15000);
    }

    function test_aHandedOverPlayerIsNotAskedForTheRequestsAfterIt() {
        // The other half of the hand-over: with the flag on, a request is given up as it arrives
        // and never reaches the view. The action request is the one to watch -- every living
        // player acts in every round, so there is always one to give up on -- and the marker that
        // it was answered is this player's own action coming back broadcast (the default reply
        // plays DoNothing), which cannot arrive before the server has asked for it.
        var actionAsked = createTemporaryObject(signalSpyComponent, testCase, {
                                                    target: game,
                                                    signalName: "requestAction"
                                                });
        var actions = createTemporaryObject(signalSpyComponent, testCase, {
                                                target: game,
                                                signalName: "actionResult"
                                            });
        var throwAsked = createTemporaryObject(signalSpyComponent, testCase, {
                                                   target: game,
                                                   signalName: "requestRockPaperScissors"
                                               });

        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);

        // Hand over with the throw in flight, so the match is past the point where this side was
        // last asked for anything.
        tryCompare(throwAsked, "count", 1, 15000);
        game.setManaged(true);

        tryVerify(function () {
            for (var i = 0; i < actions.count; ++i) {
                if (actions.signalArguments[i][0] === game.localName)
                    return true;
            }
            return false;
        }, 15000);

        compare(actionAsked.count, 0);
    }

    function test_aLocalGameStartingUpIsNotReportedAsAnError() {
        // A local game points its client at the socket in the same breath as spawning the server,
        // so the first attempt is refused and the client retries on its own (see startLocalGame).
        // That refusal is part of coming up rather than a failure to act on: reported as an error,
        // it would sit on the strip as a red reason through a game that is running perfectly well.
        var errors = createTemporaryObject(signalSpyComponent, testCase, {
                                               target: game,
                                               signalName: "errorOccurred"
                                           });

        game.playerCount = 2;
        game.startLocalGame("Tester");

        tryCompare(game, "gameState", "playing", 15000);
        // The refused attempt lands long before the match starts, so by here it would have been
        // reported -- while the game the user is looking at is fine.
        compare(errors.count, 0);
        compare(game.players.length, 2);
    }

    function test_aLocalGameWithNoServerProgramIsReportedAndNotStarted() {
        // A local game needs the server program, and a bridge that was told about one which is
        // not there has to say so rather than half start a game whose server never appears. The
        // bridge stays where it was, which is what makes this the report of a failure to start
        // rather than of a game that started. Nothing else here looks at the paths, so a bridge
        // that ignored them for this would start a game and be green.
        game.setProgramPaths("/nowhere/QMdmmServer6", "/nowhere/QMdmmBot6");
        game.startLocalGame("Tester");

        compare(game.gameState, "start");
        verify(game.statusMessage.length > 0, "the failure has to be reported");
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

    function test_handingThePlayerOverAnswersTheRequestInFlight() {
        // A managed player answers nothing. Handing the player over is taken to cover the request
        // that is already in flight, not only the ones after it -- this is the half a gate on the
        // arriving requests alone would miss. The throw below is left unanswered on purpose, which
        // is what makes the progress the give-up's doing: requestTimeout is seconds-scale (20 s +
        // 60 s grace, see ServerConfiguration), far beyond the window here.
        var throwAsked = createTemporaryObject(signalSpyComponent, testCase, {
                                                   target: game,
                                                   signalName: "requestRockPaperScissors"
                                               });
        var throwResolved = createTemporaryObject(signalSpyComponent, testCase, {
                                                      target: game,
                                                      signalName: "rpsResult"
                                                  });

        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);

        // The server asks this side for a throw, and gets no reply from it.
        tryCompare(throwAsked, "count", 1, 15000);

        var before = throwResolved.count;
        game.setManaged(true);

        tryVerify(function () {
            return throwResolved.count > before;
        }, 15000);
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

    function test_theLocalGameProgramsAreLookedUpNextToThisOne() {
        // A path given on the command line is taken as it is, whether or not anything is at it:
        // a path that leads nowhere is the business of whoever starts the program, not of the
        // lookup.
        game.setProgramPaths("/nowhere/QMdmmServer6", "/nowhere/QMdmmBot6");
        compare(game.serverProgram, "/nowhere/QMdmmServer6");
        compare(game.botProgram, "/nowhere/QMdmmBot6");

        // With neither path given, both are looked for next to this program -- which is where
        // the build tree keeps them, next to this test.
        game.setProgramPaths("", "");
        verify(game.serverProgram.endsWith("/QMdmmServer6"), game.serverProgram);
        verify(game.botProgram.endsWith("/QMdmmBot6"), game.botProgram);
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

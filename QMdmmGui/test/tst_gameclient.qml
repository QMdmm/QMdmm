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

    // Whether the strip has been told something containing `what`. A status is announced as it is
    // set and the strip keeps the last few, so a case that cares about one of them has to look at
    // what was said rather than at what happens to be showing when it looks -- and the message
    // itself is the bridge's own wording, so the case names the phrase it is about.
    function reported(spy, what) {
        for (var i = 0; i < spy.count; ++i) {
            if (spy.signalArguments[i][0].indexOf(what) >= 0)
                return true;
        }
        return false;
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

    function test_aBotThatIsGoneIsReported() {
        // The other half of the watch: the seats a local game fills are held by bot processes, and
        // a seat whose bot is gone is never going to be taken. The bot program below is a real
        // program that starts and then leaves again -- the server program, whose command line a
        // bot's own "--host / --name" is not, so it exits on it. What is under test is the
        // bridge's watch rather than the program, so a stand-in that does not stay is enough.
        var said = createTemporaryObject(signalSpyComponent, testCase, {
                                             target: game,
                                             signalName: "statusMessageChanged"
                                         });
        game.setProgramPaths("", game.serverProgram);
        game.playerCount = 2;
        game.startLocalGame("Tester");

        // The human's own arrival is what says the server was found and the game came up, so that
        // the report below is about the bot rather than about a game that never started.
        tryVerify(function () {
            return game.players.length === 1;
        }, 15000);
        tryVerify(function () {
            return reported(said, "bot stopped");
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

    function test_aLocalGameWithNoBotProgramLeavesTheSeatEmptyAndReportsIt() {
        // The bot program is wanted when a seat is filled, not when the game is asked for: a
        // bridge that was told about one which is not there still brings the game up, and the
        // seat it would have held stays empty. The two are asserted apart on purpose -- the seat
        // count is what says the bot is the program at all (a bridge that filled the seat on its
        // own would do so whatever the path says), and the report is what says the missing
        // program was noticed rather than silently swallowed.
        game.setProgramPaths("", "/nowhere/QMdmmBot6");
        game.playerCount = 2;
        game.startLocalGame("Tester");

        // The human's own arrival is what says the server program was found and the game came up.
        tryVerify(function () {
            return game.players.length === 1;
        }, 15000);
        // ...and the seat stays empty the whole time a bot would have needed to take it: filling
        // the room is what a local game's bots do within half a second of being started, so the
        // wait is what makes "still empty" an observation rather than a snapshot taken early.
        wait(3000);
        compare(game.players.length, 1);
        verify(game.statusMessage.indexOf("bot") >= 0, game.statusMessage);
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

    function test_aPlayerLeavingARoomThatIsNotFullIsDroppedFromIt() {
        // A player who leaves has to come off the room mirror. Which way the server reads a
        // drop depends on the room: a seat in a full room is kept for a reconnect, and a room
        // with seats to spare drops the player. This is the second kind -- a local game asked
        // for more seats than one bridge fills, with its bot program not there, so the seats it
        // would have filled stay empty -- and the joiner below is a bridge of its own reaching
        // the same local socket. The seat count is what says the player is gone; the
        // announcement and the state map are what say it was taken off the mirror rather than
        // only recomputed.
        var host = createTemporaryObject(gameComponent, testCase, {
                                             playerCount: 3
                                         });
        host.setProgramPaths("", "/nowhere/QMdmmBot6");
        host.startLocalGame("Host");
        tryVerify(function () {
            return host.players.length === 1;
        }, 15000);

        var removed = createTemporaryObject(signalSpyComponent, testCase, {
                                                target: host,
                                                signalName: "playerRemoved"
                                            });

        var joiner = createTemporaryObject(gameComponent, testCase, {});
        joiner.connectOnline("QMdmm", "Joiner");
        tryVerify(function () {
            return host.players.length === 2;
        }, 15000);

        // Read before the joiner is taken down: what it was called is not known to it any more
        // once its own bridge has been reset.
        var joinerName = joiner.localName;
        joiner.disconnectAll();

        tryVerify(function () {
            return host.players.length === 1;
        }, 15000);
        compare(removed.count, 1);
        compare(removed.signalArguments[0][0], joinerName);
        verify(host.agentStates[joinerName] === undefined, "the state of a player who left has to go with it");
    }

    function test_aServerThatIsGoneIsReported() {
        // A local game runs on a server process of its own, and the bridge has to know when that
        // process is gone: a game whose floor has walked out otherwise sits on screen looking like
        // a game waiting for other players (see startLocalGame). Nothing is staged for this -- a
        // local game's transports are fixed (ServerConfiguration::defaults), so the second game's
        // server finds the ports and the local socket already taken and gives up rather than
        // listen anywhere else, which is what a second local game on one machine really meets.
        var host = createTemporaryObject(gameComponent, testCase, {
                                             playerCount: 2
                                         });
        host.setProgramPaths("", "");
        host.startLocalGame("Host");
        tryCompare(host, "gameState", "playing", 15000);

        var second = createTemporaryObject(gameComponent, testCase, {
                                               playerCount: 2
                                           });
        var said = createTemporaryObject(signalSpyComponent, testCase, {
                                             target: second,
                                             signalName: "statusMessageChanged"
                                         });
        second.setProgramPaths("", "");
        second.startLocalGame("Second");

        tryVerify(function () {
            return reported(said, "local server stopped");
        }, 15000);
    }

    function test_aSpokenLineComesBackOnTheChatLog() {
        // Speaking goes to the room and comes back to everybody, this bridge included: the log
        // is built out of what the server says rather than out of what was typed here, which is
        // what makes this the round trip and not an echo of the local call. The line is
        // asserted by its content and by the name it was signed in under, so an entry built
        // from the wrong end of the call is caught as well.
        var said = createTemporaryObject(signalSpyComponent, testCase, {
                                             target: game,
                                             signalName: "chatLogChanged"
                                         });
        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);

        var before = game.chatLog.length;
        game.speak("good luck");
        tryVerify(function () {
            return game.chatLog.length > before;
        }, 15000);

        verify(said.count > 0, "the arriving line has to be announced");
        var line = game.chatLog[game.chatLog.length - 1];
        compare(line.content, "good luck");
        compare(line.name, game.localName);
        compare(line.screen, "Tester");

        // Nothing to say is not a line: the bridge drops an empty one rather than putting it on
        // the room, which is what keeps the log free of blank entries. The wait is what makes
        // this an observation -- a line that was coming would have landed well inside it, one
        // round trip being what the case above has just measured.
        var lines = game.chatLog.length;
        game.speak("");
        wait(1000);
        compare(game.chatLog.length, lines);
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
        //
        // The take-down half is asserted here too: the programs a local game runs are stopped by
        // the bridge itself when the game is left, and a program the bridge is taking down is not
        // one that failed -- a game the user simply leaves must not put a "stopped" report on the
        // strip (see watchChildProcess).
        var said = createTemporaryObject(signalSpyComponent, testCase, {
                                             target: game,
                                             signalName: "statusMessageChanged"
                                         });

        game.playerCount = 2;
        game.startLocalGame("Tester");

        tryCompare(game, "gameState", "playing", 15000);
        compare(game.players.length, 2);
        verify(game.isYou(game.localName));

        game.disconnectAll();
        compare(game.gameState, "start");
        compare(game.players.length, 0);
        compare(reported(said, "stopped"), false);
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

    function test_theActionOrdersOfferedAreAnswered() {
        // The action orders are contested when the throw leaves more than one player with the
        // right to act, and the negotiation then asks each of them which order it wants -- this
        // bridge being one of the askers. Both ways of answering are exercised, because they
        // are told apart by what goes on the wire (a named order, and a zero per selection that
        // gives the whole negotiation up) and each has to leave the negotiation resolved for
        // the round to go on.
        //
        // A room of three is what makes the negotiation contested at all: with two the throw
        // always leaves one winner, who takes every order without being asked. Which rounds
        // leave two winners is up to the throw -- about a third of them -- so the match is
        // played on until this side has been asked twice rather than the first round being
        // taken for granted.
        var orderAsked = createTemporaryObject(signalSpyComponent, testCase, {
                                                   target: game,
                                                   signalName: "requestActionOrder"
                                               });
        var resolved = createTemporaryObject(signalSpyComponent, testCase, {
                                                 target: game,
                                                 signalName: "actionOrderResult"
                                             });
        var actionAsked = createTemporaryObject(signalSpyComponent, testCase, {
                                                    target: game,
                                                    signalName: "requestAction"
                                                });
        var throwAsked = createTemporaryObject(signalSpyComponent, testCase, {
                                                   target: game,
                                                   signalName: "requestRockPaperScissors"
                                               });

        game.playerCount = 3;
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);

        var answeredThrows = 0;
        var answeredActions = 0;
        var seen = 0;
        var answeredUpTo = 0;
        var waiting = 0;
        var resolvedBefore = 0;
        for (var i = 0; i < 500 && answeredUpTo < 2; ++i) {
            if (throwAsked.count > answeredThrows) {
                answeredThrows = throwAsked.count;
                game.replyRps(0);
            }
            if (actionAsked.count > answeredActions) {
                answeredActions = actionAsked.count;
                game.replyAction(0, "", -1);
            }
            if (seen < 2 && orderAsked.count > seen) {
                ++seen;
                var offer = orderAsked.signalArguments[seen - 1];
                verify(offer[0].length >= 1, "there has to be an order to pick from: " + JSON.stringify(offer));
                verify(offer[1] >= 1, "the range the orders come from has to be told: " + JSON.stringify(offer));
                verify(offer[2] >= 1, "at least one selection is asked of this player: " + JSON.stringify(offer));

                resolvedBefore = resolved.count;
                waiting = seen;
                // The first ask is answered by naming an order, the second by giving the whole
                // negotiation up: the two are the replies the view can send.
                if (seen === 1)
                    game.replyActionOrder([offer[0][0]]);
                else
                    game.yieldActionOrder(offer[2]);
            }
            if (waiting > 0 && resolved.count > resolvedBefore) {
                answeredUpTo = waiting;
                waiting = 0;
            }
            wait(100);
        }
        compare(answeredUpTo, 2, "both answers have to leave the orders confirmed");
    }

    function test_theActionsOfferedAreBuiltFromTheLiveRoom() {
        // The action list is not a constant: it is what this room offers this player right now,
        // so an action it may not take is not on it -- the room's own answers are what the list
        // is made of. What it holds therefore depends on where this player stands and what it
        // carries when the match asks it (a room-mate can have moved it), so the case pins the
        // shape of every entry and the reply that carries one back rather than a list written
        // down here. Reading it at the first action of the match is what keeps the reply below
        // the only thing that can have produced the broadcast waited for: a player who says
        // nothing rests, and resting is not what the pick is.
        var asked = createTemporaryObject(signalSpyComponent, testCase, {
                                              target: game,
                                              signalName: "requestAction"
                                          });
        var throwAsked = createTemporaryObject(signalSpyComponent, testCase, {
                                                   target: game,
                                                   signalName: "requestRockPaperScissors"
                                               });
        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);
        // The match waits for this side's throw before anybody is asked to act, and a round of
        // throws that comes out level is thrown again -- so every throw asked of this side is
        // answered, not only the first, until the match asks for the action itself.
        var answered = 0;
        for (var i = 0; i < 150 && asked.count === 0; ++i) {
            if (throwAsked.count > answered) {
                answered = throwAsked.count;
                game.replyRps(0);
            }
            wait(100);
        }
        verify(asked.count >= 1, "the match has to ask this side to act");

        var options = game.getActionOptions();
        verify(options.length >= 2, "the room has to offer more than one thing to do: " + JSON.stringify(options));

        // Every entry describes an action completely: the kind, the label the view shows, and
        // what the reply has to carry back for it. All of them are checked, the list being what
        // the view renders.
        for (var i = 0; i < options.length; ++i) {
            verify(options[i].label.length > 0, JSON.stringify(options[i]));
            verify(typeof options[i].target === "string", JSON.stringify(options[i]));
            verify(typeof options[i].place === "number", JSON.stringify(options[i]));
        }

        // Resting is one of them while this player is alive, and it is the entry whose
        // description is fixed: the kind that takes no target, and the place sentinel that says
        // so. It comes first, which is what leaves the pick below an action of another kind.
        compare(options[0].action, 0);
        compare(options[0].label, "Do nothing / rest");
        compare(options[0].target, "");
        compare(options[0].place, -1);

        var pick = options[options.length - 1];
        verify(pick.action !== 0, "the last entry is an action other than resting: " + JSON.stringify(options));

        var performed = createTemporaryObject(signalSpyComponent, testCase, {
                                                  target: game,
                                                  signalName: "actionResult"
                                              });
        game.replyAction(pick.action, pick.target, pick.place);

        tryVerify(function () {
            for (var i = 0; i < performed.count; ++i) {
                if (performed.signalArguments[i][0] === game.localName && performed.signalArguments[i][1] === pick.action)
                    return true;
            }
            return false;
        }, 15000);
    }

    function test_theBridgesOwnStateEnumIsRegistered() {
        // The bridge's state enum is only reachable by name because gameclient.h registers it
        // (Q_ENUM) -- and that registration is what a reader of the meta object gets, whether the
        // reader is QML naming a state or anything else asking the enum for its members. Nothing
        // else in the tree reads it, so a registration that stopped being there would take no
        // other reading down with it. The names are read on the C++ side (the context property
        // tst_qmdmmgui.cpp sets); spelling them out here would be the same mistake the fixture
        // warns about for the core enums.
        compare(gameStateNames.length, 4, "the bridge's state enum should have four states");
        compare(gameStateNames[0], "Start");
        compare(gameStateNames[1], "Lobby");
        compare(gameStateNames[2], "Playing");
        compare(gameStateNames[3], "GameOver");
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

    function test_theLocalGameRunsOnTheServerDefaultSocketName() {
        // A local game is two ends that have to name the same socket, and neither end is told
        // what the other one picked: the bridge starts a server without a name, so that server
        // listens on its configuration default, and the bridge then names that same name to its
        // own client and to every bot it starts. The two are written down in two places, and a
        // game the two do not agree on does not come apart loudly: the server sits on one socket,
        // the clients reach another, and the screen says it is waiting for players -- which is
        // what it says while a game fills up anyway. So the two are compared here, where the
        // reason can be said.
        //
        // Both readings are the C++ side's own (the context properties tst_qmdmmgui.cpp sets):
        // one is what the bridge hands out, the other what the server's defaults carry, and this
        // comparison is the only place the two meet.
        compare(bridgeSocketName, serverDefaultSocketName, "the socket a local game runs on has to be the one its server listens on");
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

    function test_theNoticeThatTheRetriesRanOutReachesTheBridge() {
        // A connection that cannot be made is not given up on at once: the client retries on a
        // backoff (half a second, then doubling, five attempts) and only then says that it has
        // stopped. That last word is what the user waits for when the first one is not enough
        // to act on, and it travels the bridge's give-up channel rather than the transport's --
        // without it the strip keeps saying "Connecting to server..." for good. The port below
        // has nothing behind it, and the whole chain takes fifteen-odd seconds, which is what
        // the window is for. The reason that arrives first is asserted apart from the notice,
        // the two being different channels: a looser reading would be satisfied by either.
        var errors = createTemporaryObject(signalSpyComponent, testCase, {
                                               target: game,
                                               signalName: "errorOccurred"
                                           });
        game.connectOnline("qmdmm://127.0.0.1:1", "Tester");

        tryVerify(function () {
            return errors.count > 0;
        }, 10000);
        verify(errors.signalArguments[0][0] !== "Reconnect failed", "the transport's own reason comes first");

        tryVerify(function () {
            return game.statusMessage === "Reconnect failed";
        }, 30000);
        compare(errors.signalArguments[errors.count - 1][0], "Reconnect failed");
    }

    function test_theUpgradesOfferedAreBuiltFromTheLiveRoom() {
        // The upgrade list, like the action list, is what this room offers this player right
        // now: the three tracks are offered while there is room left on them, and the room says
        // how much room there is (the damage starts below its maximum and the maximum HP above
        // its own). Nothing has to have been earned for the list to be built -- the point to
        // spend is a separate question, asked when the offer arrives.
        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryCompare(game, "gameState", "playing", 15000);

        var options = game.getUpgradeOptions();
        var labels = options.map(function (o) {
            return o.label;
        });
        compare(options.length, 3);
        compare(labels[0], "Upgrade knife damage");
        compare(labels[1], "Upgrade horse damage");
        compare(labels[2], "Upgrade max HP");
        compare(options[0].item, 0);
        compare(options[1].item, 1);
        compare(options[2].item, 2);
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

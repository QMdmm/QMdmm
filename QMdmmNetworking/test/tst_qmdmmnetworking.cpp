// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QMdmmAgent>
#include <QMdmmClient>
#include <QMdmmData>
#include <QMdmmLogicConfiguration>
#include <QMdmmLogicRunner>
#include <QMdmmPlayer>
#include <QMdmmProtocol>
#include <QMdmmRoom>
#include <QMdmmServer>
#include <QMdmmSocket>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QVariant>
#include <QWebSocket>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmCore;
using namespace QMdmmNetworking;

using namespace Qt::StringLiterals;

class tst_QMdmmNetworking : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmNetworking() = default;

private slots:
    void signIn_disconnectInNotFullRoom_removesPlayer();
    void signIn_reconnectsPlayerInNonCurrentRoom();
    void reconnectDoesNotAutoTrust();
    void addAgent_registersLocalAgent();
    void client_exposesSelfAgent();
    void localAgent_asyncReplyContract();
    void client_giveUpTriggersServerDefaultReply();
    void client_actionOrderYieldAcceptsAssignment();
    void client_routesAgentStateChangeToSelfAgent();
    void client_reportsAgentStatesThroughOneSignal();
    void client_routesLogicConfigurationToSelfAgent();
    void client_declaresManagedStateToServer();
    void client_ignoresABroadcastAboutItsOwnAgent();
    void server_disconnectsOnAbnormalPacket();
    void server_disconnectsOnOutOfRangeReply();
    void server_disconnectsOnOversizedActionOrderReply();
    void server_disconnectsOnOversizedUpgradeReply();
    void client_disconnectsOnAbnormalPacket();
    void client_dropsOnAnUnparseablePacket_data();
    void client_dropsOnAnUnparseablePacket();
    void server_doesNotDropServerBoundNotify();
    void client_disconnectFromHostStopsAutoReconnect();
    void client_sendsHeartbeatPings();
    void client_disconnectsOnProtocolVersionMismatch();
    void client_reportsAVersionNumberMismatchAndSignsIn();
    void server_listenErrorAndClose();
    void server_statusReportsRoomsGamesAndPlayers();
    void client_infeasibleUpgradeReplyDoesNotStall();
    void client_disconnectDuringUpgradeStillAdvances();
    void socket_addressSchemeWhitelist();
    void socket_accessorsReportTheTransportAndAnyError();
    void client_speakReachesTheOtherPlayer();
    void socketError_isRegisteredAsAMetatype();
    void server_requestTimeoutDisconnectsASilentClient();
    void server_zeroRequestTimeoutNeverArmsTheTimer();
    void server_givesUpOnAPeerThatNeverEndsAPacket();
    void server_givesUpOnAnOversizedWebSocketMessage();
    void server_turnsAwayUnusableFramesWithoutDisturbingTheRoom_data();
    void server_turnsAwayUnusableFramesWithoutDisturbingTheRoom();
    void server_signInDeadlineReapsAPeerThatNeverSignsIn();
    void server_survivesTheCorpusInItsOwnProcess();
};

// A room that is not full has not started a game yet: a dropped socket removes the player
// entirely (it can re-join later as a fresh player) rather than preserving the seat for a
// reconnect. Driven end-to-end through the public Server / Client API: p1 and p2 share a
// not-full room, p1 drops, and p2 observes the remove. (p1's own client then auto-reconnects
// and rejoins as a fresh player -- that reconnect path is covered by the reconnect test below
// and the smoke test.)
void tst_QMdmmNetworking::signIn_disconnectInNotFullRoom_removesPlayer()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3); // 3-person room: two players leave it not-full
    serverConf.setTcpPort(16367);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60); // bots stay silent without timing out during the test

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16367"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    // p1 and p2 share a not-full room (3-person room, 2 players).
    QVERIFY(p1->room()->player(p2->objectName()) != nullptr);

    // Drop p1's socket. The room is not full, so the server removes p1's agent and broadcasts
    // notifyPlayerRemove to the remaining player (instead of preserving the seat).
    bool p1Removed = false;
    connect(p2->agent(), &Agent::playerRemoveNotified, [&](const QString &playerName) {
        if (playerName == p1->objectName())
            p1Removed = true;
    });

    QTcpSocket *p1Sock = p1->findChild<QTcpSocket *>();
    QVERIFY(p1Sock != nullptr);
    p1Sock->abort();

    QTRY_VERIFY_WITH_TIMEOUT(p1Removed, 5000);
}

// A reconnecting player may live in ANY room, not just `current`. `current` only tracks
// the room currently recruiting; a full room keeps running in the background and is
// deleted only on gameOver. This is the regression test for the findChildren fix: once a
// later sign-in moves `current` to a fresh room, an offline player in the old full room
// must still be reconnected in whichever room it lives. Driven end-to-end through the
// public Server / Client API over a real TCP connection.
void tst_QMdmmNetworking::signIn_reconnectsPlayerInNonCurrentRoom()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16366);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60); // bots stay silent without timing out during the test

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16366"_s;

    // Sign in one at a time so room assignment is deterministic (p1 -> room 1, p2 fills
    // room 1, p3 -> fresh room 2).
    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p2->objectName()) != nullptr, 5000);

    auto *p3 = new Client(ClientConfiguration(), &server);
    QVERIFY(p3->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p3->room() != nullptr && p3->room()->player(p3->objectName()) != nullptr, 5000);

    // Drop p1 (in the non-current full room 1); it must reconnect into room 1, not room 2.
    QTcpSocket *p1Sock = p1->findChild<QTcpSocket *>();
    QVERIFY(p1Sock != nullptr);

    bool reconnected = false;
    connect(p1, &Client::socketReconnectSucceeded, [&reconnected]() { reconnected = true; });
    p1Sock->abort();

    QTRY_VERIFY_WITH_TIMEOUT(reconnected, 10000);

    // p1 is back in room 1 (it still sees p2), and it was not added as a new player to
    // room 2 (p3's room).
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p2->objectName()) != nullptr, 5000);
    QVERIFY(p3->room() == nullptr || p3->room()->player(p1->objectName()) == nullptr);
}

// D-021 regression: a reconnect restores the player's Online flag but must NOT auto-mark the
// player Trusted. Trust ("managed") is only toggled from the player's own client UI; the server
// must never default a reconnecting player to Trust (the old reconnectAgent set StateMaskTrust on
// reconnect, which is the bug this guards against). p2, still connected in the same full room,
// observes p1's reconnected state via the agentStateChangeNotified broadcast: the drop first
// marks p1 offline (Online cleared), then the reconnect must report it online with Trust clear.
//
// The declaring client is the second half of that guard. The flag is declared while the connection
// is up, so the drop has to withdraw it: p1 goes in managed, drops, and comes back, and it must not
// re-declare the stale Trust it still held locally. A client-side echo of the stale flag would
// re-grant Trust on the server -- the same outcome the server-side regression above produces, which
// is why both halves are latched here.
void tst_QMdmmNetworking::reconnectDoesNotAutoTrust()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16363);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60); // bots stay silent without timing out during the test

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16363"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    // The flag has to really be on the server before the drop, otherwise the reconnect would not
    // have anything stale to withdraw and the guard below would pass for the wrong reason.
    bool p2SawManaged = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p2SawManaged, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && state.testFlag(Data::StateMaskTrust))
            p2SawManaged = true;
    });
    p1->agent()->setManaged(true);
    QTRY_VERIFY_WITH_TIMEOUT(p2SawManaged, 5000);

    // p2 observes p1's state broadcasts. The drop reports p1 offline (Online cleared); only the
    // reconnect broadcasts p1 online again, and it must do so with Trust still clear. If the old
    // auto-Trust behavior regressed, no broadcast would ever carry Online-without-Trust and this
    // flag stays false, failing the assertion below.
    bool p1OnlineNotTrusted = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p1OnlineNotTrusted, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && state.testFlag(Data::StateMaskOnline) && !state.testFlag(Data::StateMaskTrust))
            p1OnlineNotTrusted = true;
    });

    // The stale-Trust latch: once p1 is seen offline, any later broadcast carrying Trust means the
    // flag came back -- either from the server's own reconnect path or from the client declaring
    // the value it still held locally. Both are D-021 violations and both land here.
    bool p1Dropped = false;
    bool p1TrustedAfterDrop = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p1Dropped, &p1TrustedAfterDrop, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName != p1->objectName())
            return;

        if (!state.testFlag(Data::StateMaskOnline))
            p1Dropped = true;
        else if (p1Dropped && state.testFlag(Data::StateMaskTrust))
            p1TrustedAfterDrop = true;
    });

    QTcpSocket *p1Sock = p1->findChild<QTcpSocket *>();
    QVERIFY(p1Sock != nullptr);

    bool reconnected = false;
    connect(p1, &Client::socketReconnectSucceeded, &server, [&reconnected]() { reconnected = true; });
    p1Sock->abort();

    QTRY_VERIFY_WITH_TIMEOUT(reconnected, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(p1OnlineNotTrusted, 5000);

    // The declaring client itself takes the server's word on the way back in: the snapshot the
    // reconnect resends clears the flag it still held, and it is the snapshot -- not the client's
    // own memory -- that settles it. Asserting the local flag after the broadcast above also pins
    // the order: the state the client holds is the one the server reported.
    QTRY_VERIFY_WITH_TIMEOUT(!p1->agent()->managed(), 5000);
    QVERIFY(!p1TrustedAfterDrop);
}

// addAgent with a locally-owned agent (no ServerConnectionP child) registers a socket-less
// "local" agent (operation side = GUI / Bot): it joins the room and is reachable through
// agent(), without creating any wire plumbing. A local agent has no socket, so there is
// nothing to disconnect.
void tst_QMdmmNetworking::addAgent_registersLocalAgent()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    LogicRunner runner(conf, 3);

    Agent *local = new Agent(u"p1"_s, &runner);
    local->setScreenName(u"screen1"_s);
    local->setState(Data::StateOnline);

    QCOMPARE(runner.addAgent(local), local);
    QCOMPARE(runner.agent(u"p1"_s), local);

    // The const overload is the one a caller holding only a const reference reaches, and ServerP
    // looks agents up on a non-const runner -- so nothing exercises it unless it is asked here.
    // Both overloads have to answer the same way, for a name that is there and for one that is not.
    const LogicRunner &constRunner = runner;
    QCOMPARE(constRunner.agent(u"p1"_s), local);
    QVERIFY(constRunner.agent(u"nobody"_s) == nullptr);

    QVERIFY(local->state().testFlag(Data::StateMaskOnline));
    QVERIFY(!runner.full()); // playerNumPerRoom = 3, only one agent added
}

// The client pre-creates its own Agent on construction (symmetric to the server side where
// the operation side creates the agent and hands it to LogicRunner), so the operation side
// always has an Agent to drive even before any network connection. agent() exposes it, keyed
// by the client's own objectName.
void tst_QMdmmNetworking::client_exposesSelfAgent()
{
    Client client(ClientConfiguration {});
    QVERIFY(client.agent() != nullptr);
    QCOMPARE(client.agent()->objectName(), client.objectName());

    // The const overload is the one a caller holding only a const reference reaches. Both callers
    // reach the client through a non-const handle -- Bot::client() has a const overload but the
    // style handlers run on a non-const Bot, and the GUI bridge holds Client * -- so nothing
    // exercises it unless it is asked here. Both overloads read the same stable handle and have to
    // answer with the same pointer.
    const Client &constClient = client;
    QVERIFY(constClient.agent() != nullptr);
    QCOMPARE(constClient.agent(), client.agent());
}

// A local (socket-less) agent plays through the async reply contract: its operation side (here
// the test itself) answers each xxxRequested signal asynchronously via singleShot(0), never
// synchronously. This is the end-to-end check that a local agent -- no ServerConnectionP, no
// socket -- can actually participate (receive a request, reply async, observe the result), not
// merely be registered. The request/reply round-trip only completes because the reply is
// deferred to the event loop, which is the contract's whole point.
void tst_QMdmmNetworking::localAgent_asyncReplyContract()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    LogicRunner runner(conf, 2);

    Agent *p1 = new Agent(u"p1"_s, &runner);
    p1->setState(Data::StateOnline);
    Agent *p2 = new Agent(u"p2"_s, &runner);
    p2->setState(Data::StateOnlineBot);

    int rpsRequests = 0;
    int rpsResults = 0;

    auto wireAsyncRpsReply = [&](Agent *agent) {
        QObject::connect(agent, &Agent::rockPaperScissorsRequested, &runner, [agent, &rpsRequests]() {
            ++rpsRequests;
            QTimer::singleShot(0, agent, [agent]() { agent->rockPaperScissors(Data::Rock); });
        });
    };
    wireAsyncRpsReply(p1);
    wireAsyncRpsReply(p2);

    // The RPS result notification is the observable proof that the async reply round-tripped
    // back through the logic side (Logic -> LogicRunnerP -> Agent::notifyRockPaperScissors).
    QObject::connect(p1, &Agent::rockPaperScissorsNotified, &runner, [&rpsResults](const QHash<QString, Data::RockPaperScissors> &) { ++rpsResults; });

    // Registering both agents fills the room and kicks off the game: gameStart + roundStart,
    // then Logic starts driving the first RPS request on its thread.
    QCOMPARE(runner.addAgent(p1), p1);
    QCOMPARE(runner.addAgent(p2), p2);
    QVERIFY(runner.full());

    // Both agents got asked and their async replies produced at least one RPS result. A
    // synchronous reply would never round-trip here; only the deferred singleShot(0) does.
    QTRY_VERIFY_WITH_TIMEOUT(rpsResults >= 1, 5000);
    QVERIFY(rpsRequests >= 2);
}

// A client whose operation side gives up on a request (Agent::giveUpRequest) sends a null reply
// carrying the *correct* request id, and the server recognizes the null value as the give-up
// marker and applies its default reply -- so the logic keeps advancing instead of stalling or
// erroring out. Driven end-to-end over a real TCP connection: p1 (a bot) answers RPS normally,
// p2 (a "human") gives up, and p1 observing the RPS result is the proof that p2's default reply
// was applied. This is the D-020 regression test (the old code reset currentRequest before
// sending, so the give-up reply carried RequestInvalid and was dropped by the server).
void tst_QMdmmNetworking::client_giveUpTriggersServerDefaultReply()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16365);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60); // the server's own request timer must not fire during the test

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16365"_s;

    // Wire both agents BEFORE connecting: the first RPS request fires as soon as the room fills
    // (p2 joins), and a signal connected after connectToHost would miss it.
    auto *p1 = new Client(ClientConfiguration(), &server);
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [p1]() { QTimer::singleShot(0, p1->agent(), [p1]() { p1->agent()->rockPaperScissors(Data::Rock); }); });

    auto *p2 = new Client(ClientConfiguration(), &server);
    int p2GiveUps = 0;
    connect(p2->agent(), &Agent::rockPaperScissorsRequested, &server, [&p2GiveUps, p2]() {
        ++p2GiveUps;
        p2->agent()->giveUpRequest();
    });

    // The observable proof: p1 receives the RPS result -- which only happens if the server
    // applied p2's default reply (rather than dropping the give-up and stalling the logic).
    int rpsResults = 0;
    connect(p1->agent(), &Agent::rockPaperScissorsNotified, &server, [&rpsResults](const QHash<QString, Data::RockPaperScissors> &) { ++rpsResults; });

    // p1 joins first (room not full yet); p2 fills the room, kicking off the game + first RPS.
    QVERIFY(p1->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    QVERIFY(p2->connectToHost(host, Data::StateOnline));

    QTRY_VERIFY_WITH_TIMEOUT(p2GiveUps >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(rpsResults >= 1, 10000);
}

// A client yields the action-order contest by replying with a 0 sentinel -- the "yield" marker:
// accept whatever order is assigned and stop competing. Yielding is an explicit reply carrying
// semantics, distinct from giveUpRequest()'s null give-up (which makes the server answer with
// its default reply). The 0 sentinel must round-trip the wire (ClientP encodes it into the JSON
// array, ServerConnectionP decodes it back) and reach the core Logic, which counts the yield and
// hands the leftover order to the yielder. In a 3-player room p1/p2 win the RPS (Rock beats p3's
// Scissors) and enter the action-order negotiation; p1 yields while p2 strives for order 1. p2
// reaching the action phase is the proof that p1's yield was applied -- if the 0 sentinel had not
// round-tripped, the logic would stall in the action-order phase waiting for p1's selection.
void tst_QMdmmNetworking::client_actionOrderYieldAcceptsAssignment()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3);
    serverConf.setTcpPort(16361);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60); // the server's request timer must not fire during the test

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16361"_s;

    // Wire the replies before connecting: the first RPS request fires as soon as the room fills
    // (p3 joins), and a signal connected after connectToHost would miss it. p1 and p2 play Rock,
    // p3 plays Scissors, so Rock beats Scissors and the RPS winners are [p1, p2] -- two winners
    // enter the action-order negotiation (a single winner would just take every order without it).
    auto *p1 = new Client(ClientConfiguration(), &server);
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [p1]() { p1->agent()->rockPaperScissors(Data::Rock); });
    auto *p2 = new Client(ClientConfiguration(), &server);
    connect(p2->agent(), &Agent::rockPaperScissorsRequested, &server, [p2]() { p2->agent()->rockPaperScissors(Data::Rock); });
    auto *p3 = new Client(ClientConfiguration(), &server);
    connect(p3->agent(), &Agent::rockPaperScissorsRequested, &server, [p3]() { p3->agent()->rockPaperScissors(Data::Scissors); });

    // p1 yields its action-order selection (0 sentinel); p2 strives for order 1.
    int p1ActionOrderRequests = 0;
    connect(p1->agent(), &Agent::actionOrderRequested, &server, [p1, &p1ActionOrderRequests](const QList<int> &, int, int) {
        ++p1ActionOrderRequests;
        p1->agent()->actionOrder({0});
    });
    connect(p2->agent(), &Agent::actionOrderRequested, &server, [p2](const QList<int> &, int, int) { p2->agent()->actionOrder({1}); });

    // p2 (order 1) acts first, then p1 (assigned the leftover order 2); both reply DoNothing to
    // keep the turn moving. p2 reaching the action phase is the proof p1's yield took effect.
    int p2ActionRequests = 0;
    int p1ActionRequests = 0;
    connect(p2->agent(), &Agent::actionRequested, &server, [p2, &p2ActionRequests](int) {
        ++p2ActionRequests;
        p2->agent()->action(Data::DoNothing, {}, 0);
    });
    connect(p1->agent(), &Agent::actionRequested, &server, [p1, &p1ActionRequests](int) {
        ++p1ActionRequests;
        p1->agent()->action(Data::DoNothing, {}, 0);
    });

    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p2->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p3->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p3->room() != nullptr && p3->room()->player(p1->objectName()) != nullptr, 5000);

    // p1 entered the action-order negotiation and yielded.
    QTRY_VERIFY_WITH_TIMEOUT(p1ActionOrderRequests >= 1, 5000);

    // The game advanced past the action-order phase into the action phase for both winners.
    QTRY_VERIFY_WITH_TIMEOUT(p2ActionRequests >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(p1ActionRequests >= 1, 10000);
}

// A player's state change (online -> offline on a drop) is broadcast to every client, and the
// receiving client must route it out through its selfAgent's agentStateChangeNotified signal --
// not just silently setState the mirror agent. This is the regression test for the D-019
// clarification ("agent state must be routed over"): without the selfAgent->notifyAgentStateChange
// call, the change updates the mirror agent's data but never reaches the operation side (GUI).
void tst_QMdmmNetworking::client_routesAgentStateChangeToSelfAgent()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3); // not full: p2's drop marks it offline then removes it
    serverConf.setTcpPort(16364);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16364"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    // p2's mirror agent exists in p1's view.
    QVERIFY(p1->room()->player(p2->objectName()) != nullptr);

    // Wire the observation BEFORE dropping p2: its state change (online -> offline) is broadcast
    // to p1, whose selfAgent must route it out via agentStateChangeNotified.
    bool stateRouted = false;
    connect(p1->agent(), &Agent::agentStateChangeNotified, &server, [&stateRouted, p2](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p2->objectName() && !state.testFlag(Data::StateMaskOnline))
            stateRouted = true;
    });

    QTcpSocket *p2Sock = p2->findChild<QTcpSocket *>();
    QVERIFY(p2Sock != nullptr);
    p2Sock->abort();

    QTRY_VERIFY_WITH_TIMEOUT(stateRouted, 5000);
}

// The one channel the operation side reads agent states on: Client::agentStateChanged reports every
// agent this client knows about, and both sources of a change reach it -- the agent state broadcast
// the server owns and sends (another player's declaration included), and a change made on this side,
// which is reported as it is made rather than at the next broadcast. A mirror is followed only from
// the moment it is registered: the state it is registered with is playerAddNotified's to carry, and
// the registration itself is not a change of state.
void tst_QMdmmNetworking::client_reportsAgentStatesThroughOneSignal()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3); // not full: nothing but the declarations below moves a state
    serverConf.setTcpPort(16370);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60); // bots stay silent without timing out during the test

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16370"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);

    // Counted per player, and armed before the connection: the sign-in confirm for ourselves is one
    // of the reports this signal has to carry, and it can land before connectToHost() returns. The
    // untrusted half is kept apart because it is what an announcement of a mirror's initial
    // (untrusted) state would land in.
    QHash<QString, int> reported;
    QHash<QString, int> reportedUntrusted;
    connect(p1, &Client::agentStateChanged, &server, [&reported, &reportedUntrusted](const QString &playerName, const Data::AgentState &agentState) {
        ++reported[playerName];
        if (!agentState.testFlag(Data::StateMaskTrust))
            ++reportedUntrusted[playerName];
    });

    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    // The state we signed in with comes back to us, and it arrives here -- not only in the room's
    // own view of ourselves.
    QTRY_VERIFY_WITH_TIMEOUT(reported.value(p1->objectName()) >= 1, 5000);
    QCOMPARE(reportedUntrusted.value(p1->objectName()), 1);

    auto *p2 = new Client(ClientConfiguration(), &server);

    // The end of this client's own processing of p2 joining, rather than the mirror appearing in the
    // room: the room's player is added before the mirror is registered, and everything this client
    // reports out has been reported by the time the notify comes through.
    bool p2Added = false;
    connect(p1->agent(), &Agent::playerAddNotified, &server, [&p2Added, p2](const QString &playerName) {
        if (playerName == p2->objectName())
            p2Added = true;
    });

    // p2's own sign-in confirm has to have landed before it declares anything, and that is a
    // separate wait from the one above: what a client declares is the state it holds for itself,
    // which is the one it was born with until the server's word arrives. A confirm that lands
    // afterwards puts the state it carries back -- the client applies it and declares the result --
    // so a declaration made before it is undone instead of announced. Waiting here is what puts the
    // two declarations in the order the operation side has them in a real game, where the flag is
    // flipped from a room that is already up.
    int p2sOwnReports = 0;
    connect(p2, &Client::agentStateChanged, &server, [&p2sOwnReports, p2](const QString &playerName, const Data::AgentState &) {
        if (playerName == p2->objectName())
            ++p2sOwnReports;
    });

    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2Added, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(p2sOwnReports >= 1, 5000);

    // Joining is not a state change: the mirror's initial state travels with the notify above, and
    // arming the report only after the registration is what keeps it from being announced twice.
    QCOMPARE(reported.value(p2->objectName()), 0);

    // Declaring is not waiting: our own flag is reported as it is made, so the event loop has not
    // run between the flip and the reading below, and what is read cannot be the server's echo.
    const int beforeFlip = reported.value(p1->objectName());
    p1->agent()->setManaged(true);
    QCOMPARE(reported.value(p1->objectName()), beforeFlip + 1);
    QCOMPARE(reportedUntrusted.value(p1->objectName()), 1);

    // The other half is the server's own word: p2 declares, the server applies the flag and
    // broadcasts it, and the receiving client reports it as a change of p2's state.
    p2->agent()->setManaged(true);
    QTRY_COMPARE_WITH_TIMEOUT(reported.value(p2->objectName()) - reportedUntrusted.value(p2->objectName()), 1, 5000);
    QCOMPARE(reportedUntrusted.value(p2->objectName()), 0);
}

// The logic configuration is broadcast when a player joins a room, and the receiving client must
// route it out through its selfAgent's logicConfigurationNotified signal -- filling the mirror
// room's configuration alone only reaches whatever reads the room model directly. The operation
// side (GUI, Bot) learns that the rules are in place from that signal, exactly like it learns
// about a player joining or leaving.
void tst_QMdmmNetworking::client_routesLogicConfigurationToSelfAgent()
{
    LogicConfiguration conf = LogicConfiguration::defaults();
    conf.setInitialMaxHp(7);
    conf.setMaximumMaxHp(9);
    conf.setEnableLetMove(false);

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2); // not full: nothing else is broadcast
    serverConf.setTcpPort(16379);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    auto *p1 = new Client(ClientConfiguration(), &server);

    // The broadcast can land before connectToHost() returns, so the observation is armed first.
    int configRouted = 0;
    connect(p1->agent(), &Agent::logicConfigurationNotified, &server, [&configRouted]() { ++configRouted; });

    QVERIFY(p1->connectToHost(u"qmdmm://localhost:16379"_s, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(configRouted >= 1, 5000);

    // What the signal announces has to be readable: it carries no payload, so the rules the
    // server sent are the ones the mirror room now holds, field for field.
    QCOMPARE(p1->room()->logicConfiguration().initialMaxHp(), conf.initialMaxHp());
    QCOMPARE(p1->room()->logicConfiguration().maximumMaxHp(), conf.maximumMaxHp());
    QCOMPARE(p1->room()->logicConfiguration().enableLetMove(), conf.enableLetMove());
}

// The runtime managed toggle (D-021): the operation side flips the client's own agent with
// Agent::setManaged, the client declares the state it holds on the wire, and the server -- which
// owns the agent state -- applies the whole of it and reports the change to the room's other
// players. A second player in the same room observes the declaration, which is what makes this a
// synchronized flag rather than a client-local one. Withdrawing it is part of the contract too: the
// managed state is a toggle, not a one-way grant. The declaring client is deliberately not among
// those told: what it declared is a value it already holds, and handing it back would invite it to
// declare that value again.
void tst_QMdmmNetworking::client_declaresManagedStateToServer()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16378);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60); // bots stay silent without timing out during the test

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16378"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    // The sign-in carried Online without Trust, so nothing is managed before the toggle.
    QVERIFY(p1->agent()->state().testFlag(Data::StateMaskOnline));
    QVERIFY2(!p1->agent()->managed(), qPrintable(u"p1's agent state is 0x%1 before anything declared it managed"_s.arg(static_cast<int>(p1->agent()->state()), 0, 16)));

    bool p2SawManaged = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p2SawManaged, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && state.testFlag(Data::StateMaskTrust))
            p2SawManaged = true;
    });

    // What the server does with the declaration: it reports the change to the room's other players
    // and not back to the player that made it (see ClientP::sendAgentStateChanged), so a declaration
    // cannot come back to its author as news and be declared again. The reading is taken where that
    // reporting happens -- the server's own agent for p1 -- because a broadcast about itself would
    // be disregarded on the client side anyway (covered separately by
    // client_ignoresABroadcastAboutItsOwnAgent), and would therefore be invisible there. The parent
    // test is what keeps this the server's agent rather than the client's own, which is a child of
    // the Client above it.
    Agent *p1OnTheServer = nullptr;
    for (Agent *agent : server.findChildren<Agent *>()) {
        if (agent->objectName() == p1->objectName() && qobject_cast<LogicRunner *>(agent->parent()) != nullptr)
            p1OnTheServer = agent;
    }
    QVERIFY(p1OnTheServer != nullptr);
    QVERIFY(p1OnTheServer != p1->agent());

    int p1ToldAboutItself = 0;
    connect(p1OnTheServer, &Agent::agentStateChangeNotified, &server, [&p1ToldAboutItself, p1](const QString &playerName, const Data::AgentState &) {
        if (playerName == p1->objectName())
            ++p1ToldAboutItself;
    });

    // Declaring is not waiting: setManaged writes the flag into the agent's own state, so the client
    // already holds what it declares -- the event loop has not run between the flip and the reading
    // below, and this is not a value an echo has carried back.
    p1->agent()->setManaged(true);
    QVERIFY(p1->agent()->managed());

    QTRY_VERIFY_WITH_TIMEOUT(p2SawManaged, 5000);

    // The flag is the only thing that moved: the player is still online, and p2's own state is not
    // touched -- a declaration is about the declaring player only.
    QVERIFY(p1->agent()->managed());
    QVERIFY(p1->agent()->state().testFlag(Data::StateMaskOnline));
    QVERIFY2(!p2->agent()->managed(),
             qPrintable(u"p2's agent state is 0x%1; a declaration is about the declaring player only"_s.arg(static_cast<int>(p2->agent()->state()), 0, 16)));

    bool p2SawUnmanaged = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p2SawUnmanaged, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && !state.testFlag(Data::StateMaskTrust))
            p2SawUnmanaged = true;
    });

    // The withdrawal is read just as directly: no broadcast about this client is in flight for the
    // local clear to race, so the value read here is the settled one.
    p1->agent()->setManaged(false);
    QVERIFY(!p1->agent()->managed());

    QTRY_VERIFY_WITH_TIMEOUT(p2SawUnmanaged, 5000);

    // Both declarations have gone through by now. A settled pair leaves the count at nothing: the
    // two broadcasts went to the room, and neither went back to the player that had declared its
    // state.
    QTest::qWait(500);
    QVERIFY2(p1ToldAboutItself == 0,
             qPrintable(u"the server reported %1 state change(s) about p1 back to p1; a declaration does not come back to its author"_s.arg(p1ToldAboutItself)));
    QVERIFY2(!p1->agent()->managed(), qPrintable(u"p1's agent state is 0x%1 after the withdrawal was declared"_s.arg(static_cast<int>(p1->agent()->state()), 0, 16)));
}

// A broadcast about this client's own agent is not applied: the state it holds for itself is the
// one it declared, so taking the server's word for it would turn a broadcast into a declaration of
// its own -- the value would go back on the wire it had just come off, and the two sides would hand
// it back and forth. The server does not report a change to the player that made it (see
// client_declaresManagedStateToServer), so the case is driven over a bare peer that speaks the
// handshake and then sends just such a broadcast: what the client does with it is the reading.
void tst_QMdmmNetworking::client_ignoresABroadcastAboutItsOwnAgent()
{
    QTcpServer rawServer;
    QVERIFY(rawServer.listen(QHostAddress::Any, 16389));

    QByteArray received;
    QTcpSocket *peer = nullptr;
    QObject::connect(&rawServer, &QTcpServer::newConnection, &rawServer, [&rawServer, &received, &peer]() {
        peer = rawServer.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, peer, [&received, peer]() { received.append(peer->readAll()); });
        QJsonObject ob;
        ob.insert(u"versionNumber"_s, QMdmmCore::Global::version().toString());
        ob.insert(u"protocolVersion"_s, QMdmmCore::Protocol::version());
        peer->write(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyVersion, ob).serialize().append('\n'));
        peer->flush();
    });

    auto *p1 = new Client(ClientConfiguration(), &rawServer);

    // Armed before the connection: a broadcast taken as a change would reach the operation side on
    // this signal, so the count is one of the readings below.
    int reported = 0;
    connect(p1, &Client::agentStateChanged, &rawServer, [&reported](const QString &, const Data::AgentState &) { ++reported; });

    QVERIFY(p1->connectToHost(u"qmdmm://localhost:16389"_s, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(received.contains('\n'), 5000);
    QVERIFY(peer != nullptr);

    // How many times this client declared a state of its own on the connection. Nothing here should
    // make it declare at all: the sign-in carries the state it wants, and that is a NotifySignIn.
    const auto declarationsIn = [](const QByteArray &bytes) {
        int declarations = 0;
        for (const QByteArray &line : bytes.split('\n')) {
            if (line.isEmpty())
                continue;
            const Packet packet = Packet::fromJson(line);
            if (!packet.hasError() && packet.type() == Protocol::TypeNotify && packet.notifyId() == Protocol::NotifyStateChange)
                ++declarations;
        }
        return declarations;
    };
    QCOMPARE(declarationsIn(received), 0);

    // A broadcast about ourselves, carrying the flag as set. The peer is the server here and the
    // name is the one this client signed in with, so what arrives is exactly the message the client
    // would have to disregard.
    QJsonObject ob;
    ob.insert(u"playerName"_s, p1->objectName());
    ob.insert(u"agentState"_s, static_cast<int>(Data::StateOnlineTrust));
    peer->write(Packet(Protocol::NotifyAgentStateChanged, ob).serialize().append('\n'));
    peer->flush();

    // Nothing happens on either side of it: the flag is not taken, it is not reported to the
    // operation side, and it is not declared back at the peer.
    QTest::qWait(300);
    QVERIFY2(!p1->agent()->managed(), qPrintable(u"our own agent state is 0x%1 after a broadcast about ourselves"_s.arg(static_cast<int>(p1->agent()->state()), 0, 16)));
    QCOMPARE(reported, 0);
    QCOMPARE(declarationsIn(received), 0);
    QVERIFY(p1->isConnected());
}

// A reply carrying a statically checkable invalid value -- here an out-of-range
// RockPaperScissors enum (99) -- must be rejected at the decode layer (D-030: decode-layer full
// defense): the server drops the connection instead of silently answering with the default reply.
// In a full room the drop marks the misbehaving player offline (its seat is preserved for a
// reconnect), so the other player observes the state change. The reply is written straight onto
// the client's raw TCP socket (the public Client API has no "send arbitrary reply" entry point).
void tst_QMdmmNetworking::server_disconnectsOnOutOfRangeReply()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2); // full with two players: game starts, drop preserves the seat
    serverConf.setTcpPort(16368);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16368"_s;

    // p1 plays as a bot and replies Rock automatically, keeping the RPS phase alive. p2 replies
    // with an out-of-range enum straight onto its raw socket once the RPS request arrives.
    auto *p1 = new Client(ClientConfiguration(), &server);
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [p1]() { p1->agent()->rockPaperScissors(Data::Rock); });

    auto *p2 = new Client(ClientConfiguration(), &server);
    connect(p2->agent(), &Agent::rockPaperScissorsRequested, &server, [p2]() {
        QTcpSocket *sock = p2->findChild<QTcpSocket *>();
        if (sock != nullptr) {
            sock->write(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestRockPaperScissors, QJsonValue(99)).serialize().append('\n'));
            sock->flush();
        }
    });

    // The observable proof: p2's drop marks it offline, broadcast to p1 as a state change.
    bool p2Offline = false;
    connect(p1->agent(), &Agent::agentStateChangeNotified, &server, [&p2Offline, p2](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p2->objectName() && !state.testFlag(Data::StateMaskOnline))
            p2Offline = true;
    });

    QVERIFY(p1->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    QVERIFY(p2->connectToHost(host, Data::StateOnline));

    QTRY_VERIFY_WITH_TIMEOUT(p2Offline, 5000);
}

// The action-order reply is oversized when it carries more entries than the selectionNum the
// server asked for. 12f03d7 added the arr.size() > selectionNum guard in decodeActionOrderReply
// (D-025 / D-030 decode-layer full defense): the server must drop the connection instead of
// handing the oversized order list to the core Logic. In a full 3-player room p1 and p2 win the
// RPS (Rock beats p3's Scissors) and each get one action-order selection; p1 answers with a
// 2-entry array (size 2 > selectionNum 1) straight onto its raw socket, and p3 observes p1 go
// offline (the seat is preserved in a full room, so p1 is marked offline rather than removed).
void tst_QMdmmNetworking::server_disconnectsOnOversizedActionOrderReply()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3);
    serverConf.setTcpPort(16374);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16374"_s;

    // Wire the replies before connecting: the first RPS request fires as soon as the room fills
    // (p3 joins). p1 and p2 play Rock, p3 plays Scissors, so the two Rock winners (p1, p2) enter
    // the action-order negotiation, each with one selection to make.
    auto *p1 = new Client(ClientConfiguration(), &server);
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [p1]() { p1->agent()->rockPaperScissors(Data::Rock); });
    auto *p2 = new Client(ClientConfiguration(), &server);
    connect(p2->agent(), &Agent::rockPaperScissorsRequested, &server, [p2]() { p2->agent()->rockPaperScissors(Data::Rock); });
    auto *p3 = new Client(ClientConfiguration(), &server);
    connect(p3->agent(), &Agent::rockPaperScissorsRequested, &server, [p3]() { p3->agent()->rockPaperScissors(Data::Scissors); });

    // p1 answers its action-order request with an oversized 2-entry array (size 2 > selectionNum 1);
    // p2 answers normally so the negotiation does not stall while p1's misbehavior is observed.
    connect(p1->agent(), &Agent::actionOrderRequested, &server, [p1](const QList<int> &, int, int) {
        QTcpSocket *sock = p1->findChild<QTcpSocket *>();
        if (sock != nullptr) {
            QJsonArray oversized;
            oversized.append(1);
            oversized.append(2);
            sock->write(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestActionOrder, QJsonValue(oversized)).serialize().append('\n'));
            sock->flush();
        }
    });
    connect(p2->agent(), &Agent::actionOrderRequested, &server, [p2](const QList<int> &, int, int) { p2->agent()->actionOrder({1}); });

    // p3 observes p1's drop (broadcast as a state change with the Online flag cleared).
    bool p1Offline = false;
    connect(p3->agent(), &Agent::agentStateChangeNotified, &server, [&p1Offline, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && !state.testFlag(Data::StateMaskOnline))
            p1Offline = true;
    });

    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p2->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p3->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p3->room() != nullptr && p3->room()->player(p1->objectName()) != nullptr, 5000);

    QTRY_VERIFY_WITH_TIMEOUT(p1Offline, 5000);
}

// The upgrade reply is oversized when it carries more items than the remainingTimes the server
// asked for. 12f03d7 added the arr.size() > remainingTimes guard in decodeUpgradeReply (D-025 /
// D-030 decode-layer full defense): the server must drop the connection instead of handing the
// oversized upgrade list to the core Logic. A 2-player room is driven to the upgrade phase by
// having p1 (Rock, always winning the RPS against p2's Scissors) buy a knife, walk to p2's city
// and slash it -- a single 1-damage slash kills the 1-HP p2 (initialMaxHp is set to 1 and punish
// HP is disabled), earning p1 one upgrade point. p1 then answers its upgrade request with a
// 2-item array (size 2 > remainingTimes 1) straight onto its raw socket, and p2 observes p1 go
// offline (the seat is preserved in a full room, so p1 is marked offline rather than removed).
void tst_QMdmmNetworking::server_disconnectsOnOversizedUpgradeReply()
{
    LogicConfiguration conf = LogicConfiguration::defaults();
    conf.setInitialMaxHp(1); // a single 1-damage slash kills the 1-HP victim
    conf.setPunishHpModifier(0); // no city-slash self-punish, so p1 survives to earn the point

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16375);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16375"_s;

    // Wire the replies before connecting. p1 always wins the RPS (Rock beats p2's Scissors), so
    // p1 is the sole actor each cycle and can reach p2's city and kill it without p2 ever acting.
    auto *p1 = new Client(ClientConfiguration(), &server);
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [p1]() { p1->agent()->rockPaperScissors(Data::Rock); });
    auto *p2 = new Client(ClientConfiguration(), &server);
    connect(p2->agent(), &Agent::rockPaperScissorsRequested, &server, [p2]() { p2->agent()->rockPaperScissors(Data::Scissors); });

    // p1's script: buy a knife, walk to p2's city, then slash it. Each action is preceded by a
    // fresh RPS win, so p1 acts once per cycle until p2 dies.
    int step = 0;
    connect(p1->agent(), &Agent::actionRequested, &server, [p1, p2, &step](int) {
        ++step;
        switch (step) {
        case 1:
            p1->agent()->action(Data::BuyKnife, {}, 0);
            break;
        case 2:
            p1->agent()->action(Data::Move, {}, Data::Village);
            break;
        case 3:
            p1->agent()->action(Data::Move, {}, p1->room()->player(p2->objectName())->place());
            break;
        case 4:
            p1->agent()->action(Data::Slash, p2->objectName(), 0);
            break;
        default:
            p1->agent()->action(Data::DoNothing, {}, 0);
            break;
        }
    });

    // Once p1 earns its upgrade point the server asks for p1's upgrade; p1 answers with an
    // oversized 2-item array (size 2 > remainingTimes 1) carrying valid upgrade items, so the
    // size guard -- not the item-range guard -- is the branch under test.
    connect(p1->agent(), &Agent::upgradeRequested, &server, [p1](int) {
        QTcpSocket *sock = p1->findChild<QTcpSocket *>();
        if (sock != nullptr) {
            QJsonArray oversized;
            oversized.append(static_cast<int>(Data::UpgradeKnife));
            oversized.append(static_cast<int>(Data::UpgradeHorse));
            sock->write(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestUpgrade, QJsonValue(oversized)).serialize().append('\n'));
            sock->flush();
        }
    });

    // p2 observes p1's drop (broadcast as a state change with the Online flag cleared).
    bool p1Offline = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p1Offline, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && !state.testFlag(Data::StateMaskOnline))
            p1Offline = true;
    });

    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p2->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p1->room()->player(p2->objectName()) != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(p1Offline, 15000);
}

// A client sending a packet the server does not expect from a client -- here an invalid packet
// type -- is an abnormal case (D-025): the server must drop the connection rather than silently
// ignore the packet. In a not-full room the drop removes the misbehaving player, so the remaining
// player observes notifyPlayerRemove. The invalid packet is written straight onto the client's raw
// TCP socket (the public Client API has no "send arbitrary packet" entry point).
void tst_QMdmmNetworking::server_disconnectsOnAbnormalPacket()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3); // not full with two players: the drop removes, not preserves
    serverConf.setTcpPort(16362);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16362"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    bool p2Removed = false;
    connect(p1->agent(), &Agent::playerRemoveNotified, &server, [&p2Removed, p2](const QString &playerName) {
        if (playerName == p2->objectName())
            p2Removed = true;
    });

    // Write a packet with an out-of-range type (99) straight onto p2's raw socket. fromJson rejects
    // it at the protocol layer (enum range check), so the socket marks itself errored and p2 drops.
    QTcpSocket *p2Sock = p2->findChild<QTcpSocket *>();
    QVERIFY(p2Sock != nullptr);
    p2Sock->write("{\"type\":99,\"requestId\":0,\"notifyId\":0,\"value\":null}\n");
    p2Sock->flush();

    QTRY_VERIFY_WITH_TIMEOUT(p2Removed, 5000);
}

// The client-side mirror of server_disconnectsOnAbnormalPacket: a packet the client should never
// receive from the server -- here a reply, which only ever originates from the client -- is an
// abnormal case (D-025). The client must drop the connection instead of silently ignoring it. The
// reply is written straight onto a raw TCP server's accepted socket; fromJson accepts it (the
// type/requestId/notifyId are all well-formed), so it reaches the client dispatch layer, which has
// no branch for a server-sent reply and marks the socket errored.
void tst_QMdmmNetworking::client_disconnectsOnAbnormalPacket()
{
    QTcpServer rawServer;
    QVERIFY(rawServer.listen(QHostAddress::Any, 16372));

    bool sent = false;
    QObject::connect(&rawServer, &QTcpServer::newConnection, &rawServer, [&rawServer, &sent]() {
        QTcpSocket *sock = rawServer.nextPendingConnection();
        if (!sent) {
            sent = true;
            // A reply is well-formed, but only the client ever sends replies (D-025: wrong direction).
            sock->write(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestRockPaperScissors, QJsonObject()).serialize().append('\n'));
            sock->flush();
        }
    });

    const QString host = u"qmdmm://localhost:16372"_s;

    auto *p1 = new Client(ClientConfiguration(), &rawServer);
    bool connectionLost = false;
    connect(p1, &Client::socketConnectionLost, &rawServer, [&connectionLost](const QString &) { connectionLost = true; });

    QVERIFY(p1->connectToHost(host, Data::StateOnline));

    // The client drops the connection on the wrong-direction reply (D-025).
    QTRY_VERIFY_WITH_TIMEOUT(connectionLost, 5000);
}

// A server-bound notify (ping) is legal client->server traffic: ServerConnectionP must hand it to
// ServerP (which answers with a pong) and must NOT treat it as abnormal. This is the complement of
// server_disconnectsOnAbnormalPacket -- that test proves an invalid packet drops the connection,
// this one proves a legal server-bound notify does not. The ping is written straight onto p2's raw
// socket (the public Client API has no "send arbitrary packet" entry point, and the real heartbeat
// fires only every 30s).
void tst_QMdmmNetworking::server_doesNotDropServerBoundNotify()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3); // not full with two players: a drop removes, not preserves
    serverConf.setTcpPort(16373);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16373"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    bool p2Removed = false;
    connect(p1->agent(), &Agent::playerRemoveNotified, &server, [&p2Removed, p2](const QString &playerName) {
        if (playerName == p2->objectName())
            p2Removed = true;
    });

    // Send a server-bound ping straight onto p2's raw socket. The server must answer with a pong
    // and keep the connection; it must not treat the ping as an abnormal packet and drop p2.
    QTcpSocket *p2Sock = p2->findChild<QTcpSocket *>();
    QVERIFY(p2Sock != nullptr);
    p2Sock->write(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyPingServer, QJsonValue(123)).serialize().append('\n'));
    p2Sock->flush();

    // Let the ping round-trip; p2 must still be connected and present in the room.
    QTest::qWait(500);
    QVERIFY(p2->isConnected());
    QVERIFY(!p2Removed);
}

// A client can actively disconnect via disconnectFromHost(): the reconnect loop is stopped, no
// "connection lost" / "reconnect succeeded" signal fires (those are the passive-drop notices),
// and isConnected() flips to false immediately. The upper layer stays in control and can
// reconnect with a fresh connectToHost. This is the A1 regression test: before the API existed
// the only way to disconnect was destroying the whole Client.
void tst_QMdmmNetworking::client_disconnectFromHostStopsAutoReconnect()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16360);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16360"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(!p1->isConnected());

    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QVERIFY(p1->isConnected());
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    bool connectionLost = false;
    bool reconnectSucceeded = false;
    connect(p1, &Client::socketConnectionLost, &server, [&connectionLost](const QString &) { connectionLost = true; });
    connect(p1, &Client::socketReconnectSucceeded, &server, [&reconnectSucceeded]() { reconnectSucceeded = true; });

    p1->disconnectFromHost();
    QVERIFY(!p1->isConnected());

    // The reconnect loop is stopped: even after the first retry interval (500ms) elapses the
    // client stays disconnected, and neither passive-drop signal has fired.
    QTest::qWait(700);
    QVERIFY(!p1->isConnected());
    QVERIFY(!connectionLost);
    QVERIFY(!reconnectSucceeded);

    // A fresh connectToHost reconnects normally.
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QVERIFY(p1->isConnected());
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);
}

// The heartbeat is what lets a server tell an idle player from one that is gone, and
// NotifyPingServer has exactly one sender in a client: the heartbeat timer. The timer was created,
// given its interval and wired to its slot, but nothing ever started it, so the ping never left
// the client. The pings are read off a raw TCP server's accepted socket -- the connection's own
// wire, not an internal counter. The interval is shortened on the client's timer rather than the
// case waiting out 30s: it is about pings going out repeatedly, not about the wall clock, and the
// client arms the timer itself (the case never starts it).
void tst_QMdmmNetworking::client_sendsHeartbeatPings()
{
    QTcpServer rawServer;
    QVERIFY(rawServer.listen(QHostAddress::LocalHost, 0));

    QTcpSocket *accepted = nullptr;
    int pings = 0;
    QObject::connect(&rawServer, &QTcpServer::newConnection, &rawServer, [&rawServer, &accepted, &pings]() {
        accepted = rawServer.nextPendingConnection();
        QObject::connect(accepted, &QAbstractSocket::readyRead, accepted, [accepted, &pings]() {
            while (accepted->canReadLine()) {
                const QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(accepted->readLine());
                if (packet.type() == QMdmmCore::Protocol::TypeNotify && packet.notifyId() == QMdmmCore::Protocol::NotifyPingServer)
                    ++pings;
            }
        });
    });

    const QString host = u"qmdmm://127.0.0.1:%1"_s.arg(rawServer.serverPort());

    auto *client = new Client(ClientConfiguration(), &rawServer);

    // The client owns two timers and only the heartbeat repeats (the reconnect timer is
    // single-shot), so the repeating one is the heartbeat.
    QTimer *heartbeat = nullptr;
    for (QTimer *timer : client->findChildren<QTimer *>()) {
        if (!timer->isSingleShot())
            heartbeat = timer;
    }
    QVERIFY2(heartbeat != nullptr, "the client has no repeating heartbeat timer");
    QCOMPARE(heartbeat->interval(), 30000);

    QVERIFY(client->connectToHost(host, Data::StateOnline));
    QVERIFY2(heartbeat->isActive(), "connecting did not arm the heartbeat timer");

    heartbeat->setInterval(200); // does not start an idle timer: only the client's own arming counts
    QTRY_VERIFY_WITH_TIMEOUT(pings >= 2, 5000);
    QVERIFY(accepted != nullptr);

    // The heartbeat belongs to the connection it pings: at the moment the client reports the
    // connection lost, the timer must already be disarmed. Read inside the notice, while the
    // reconnect loop has not had a chance to arm anything again. (read off the client, not
    // asserted here: QVERIFY is not available in a lambda)
    bool armedWhenTheConnectionWasLost = true;
    connect(client, &Client::socketConnectionLost, &rawServer,
            [heartbeat, &armedWhenTheConnectionWasLost](const QString &) { armedWhenTheConnectionWasLost = heartbeat->isActive(); });

    accepted->close();
    QTRY_VERIFY_WITH_TIMEOUT(!armedWhenTheConnectionWasLost, 5000);
}

// A protocolVersion mismatch is a hard incompatibility, not a transient failure: the client must
// disconnect cleanly (via disconnectFromHost) instead of silently aborting sign-in or looping in
// auto-reconnect against a server it can never talk to. This is the B6 regression test, driven by
// a raw TCP server that greets every incoming client with a mismatched protocolVersion.
void tst_QMdmmNetworking::client_disconnectsOnProtocolVersionMismatch()
{
    QTcpServer rawServer;
    QVERIFY(rawServer.listen(QHostAddress::Any, 16371));

    QObject::connect(&rawServer, &QTcpServer::newConnection, &rawServer, [&rawServer]() {
        QTcpSocket *sock = rawServer.nextPendingConnection();
        QJsonObject ob;
        ob.insert(u"versionNumber"_s, u"9.9.9"_s);
        ob.insert(u"protocolVersion"_s, QMdmmCore::Protocol::version() + 1); // mismatched
        sock->write(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyVersion, ob).serialize().append('\n'));
        sock->flush();
    });

    const QString host = u"qmdmm://localhost:16371"_s;

    auto *p1 = new Client(ClientConfiguration(), &rawServer);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));

    // The client drops the connection on the mismatch (never reaching a signed-in state).
    QTRY_VERIFY_WITH_TIMEOUT(!p1->isConnected(), 5000);

    // No auto-reconnect: past the first retry interval the client is still disconnected.
    QTest::qWait(700);
    QVERIFY(!p1->isConnected());
}

// A versionNumber mismatch is tolerated -- the wire protocol is still compatible -- but it must not
// be silent: the client reports both version numbers and then goes on with the handshake. The log
// line is asserted here by name, so changing its wording or its log level in ClientP::notifyVersion
// makes this test fail instead of passing quietly.
void tst_QMdmmNetworking::client_reportsAVersionNumberMismatchAndSignsIn()
{
    QTcpServer rawServer;
    QVERIFY(rawServer.listen(QHostAddress::Any, 16383));

    QByteArray received;
    QObject::connect(&rawServer, &QTcpServer::newConnection, &rawServer, [&rawServer, &received]() {
        QTcpSocket *sock = rawServer.nextPendingConnection();
        QObject::connect(sock, &QTcpSocket::readyRead, sock, [sock, &received]() { received.append(sock->readAll()); });
        QJsonObject ob;
        ob.insert(u"versionNumber"_s, u"9.9.9"_s); // differs from this build
        ob.insert(u"protocolVersion"_s, QMdmmCore::Protocol::version()); // still compatible
        sock->write(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyVersion, ob).serialize().append('\n'));
        sock->flush();
    });

    // Registering the expectation before the handshake runs turns "the line disappeared" into a
    // failure of this test; the wording is matched up to this build's own version number, which is
    // read from the build rather than pinned here.
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(u"ClientP::notifyVersion: the server reports version \"9\\.9\\.9\" while this build is \"%1\""_s.arg(
                             QRegularExpression::escape(QMdmmCore::Global::version().toString()))));

    auto *p1 = new Client(ClientConfiguration(), &rawServer);
    QVERIFY(p1->connectToHost(u"qmdmm://localhost:16383"_s, Data::StateOnline));

    // The handshake proceeds over the same connection: the client signs in rather than dropping it.
    QTRY_VERIFY_WITH_TIMEOUT(received.contains('\n'), 5000);
    const Packet signIn = Packet::fromJson(received.left(received.indexOf('\n')));
    QVERIFY(!signIn.hasError());
    QCOMPARE(int(signIn.type()), int(Protocol::TypeNotify));
    QCOMPARE(int(signIn.notifyId()), int(Protocol::NotifySignIn));
    QCOMPARE(signIn.value().toObject().value(u"playerName"_s).toString(), p1->objectName());
    QVERIFY(p1->isConnected());
}

// The server reports per-transport listen failures via listenError (the aggregate return value
// alone can't tell which transport broke), and close() shuts the listening sockets down so a
// later listen() can bind again. This is the A2 regression test.
void tst_QMdmmNetworking::server_listenErrorAndClose()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    // Occupy a TCP port so the server's TCP transport cannot bind it.
    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress::Any, 16361));

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setTcpPort(16361);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);

    Server server(serverConf, conf);

    QString errorTransport;
    QString errorString;
    connect(&server, &Server::listenError, [&](const QString &transport, const QString &err) {
        errorTransport = transport;
        errorString = err;
    });

    // The port is taken, so listen() fails and reports which transport broke.
    QVERIFY(!server.listen());
    QCOMPARE(errorTransport, u"tcp"_s);
    QVERIFY(!errorString.isEmpty());

    // Free the port; the server can now bind it, close() releases it, and a second listen()
    // binds again (proving the listening socket was actually shut down).
    blocker.close();
    QVERIFY(server.listen());
    server.close();
    QVERIFY(server.listen());
}

// A reply that passes the decode layer's static checks (a valid enum, and a size within the
// requested remainingTimes) but is logically infeasible -- here an UpgradeMaxHp when maxHp is
// already maxed out -- must not stall the game. D-036 made Logic::upgradeReply fall back to a
// feasible default (spend every point, knife damage first) instead of rejecting, so the upgrade
// phase still advances. A 2-player room is driven to the upgrade phase by p1 (Rock, always
// beating p2's Scissors) killing the 1-HP p2 with a single slash. p1 then answers its upgrade
// request with an UpgradeMaxHp that is statically valid but infeasible (maximumMaxHp ==
// initialMaxHp, so maxHp has no remaining upgrade), and p1 observing the upgrade result is the
// proof the fallback advanced the game instead of stalling.
void tst_QMdmmNetworking::client_infeasibleUpgradeReplyDoesNotStall()
{
    LogicConfiguration conf = LogicConfiguration::defaults();
    conf.setInitialMaxHp(1); // a single 1-damage slash kills the 1-HP victim
    conf.setMaximumMaxHp(1); // maxHp is already maxed out: no UpgradeMaxHp is feasible
    conf.setPunishHpModifier(0); // no city-slash self-punish, so p1 survives to earn the point

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16376);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16376"_s;

    // Wire the replies before connecting. p1 always wins the RPS (Rock beats p2's Scissors), so
    // p1 is the sole actor each cycle and can reach p2's city and kill it without p2 ever acting.
    auto *p1 = new Client(ClientConfiguration(), &server);
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [p1]() { p1->agent()->rockPaperScissors(Data::Rock); });
    auto *p2 = new Client(ClientConfiguration(), &server);
    connect(p2->agent(), &Agent::rockPaperScissorsRequested, &server, [p2]() { p2->agent()->rockPaperScissors(Data::Scissors); });

    // p1's script: buy a knife, walk to p2's city, then slash it. Each action is preceded by a
    // fresh RPS win, so p1 acts once per cycle until p2 dies.
    int step = 0;
    connect(p1->agent(), &Agent::actionRequested, &server, [p1, p2, &step](int) {
        ++step;
        switch (step) {
        case 1:
            p1->agent()->action(Data::BuyKnife, {}, 0);
            break;
        case 2:
            p1->agent()->action(Data::Move, {}, Data::Village);
            break;
        case 3:
            p1->agent()->action(Data::Move, {}, p1->room()->player(p2->objectName())->place());
            break;
        case 4:
            p1->agent()->action(Data::Slash, p2->objectName(), 0);
            break;
        default:
            p1->agent()->action(Data::DoNothing, {}, 0);
            break;
        }
    });

    // Answer the upgrade request with an infeasible UpgradeMaxHp (maxHp has no remaining upgrade
    // because maximumMaxHp == initialMaxHp). The reply is statically valid (one entry within the
    // remainingTimes of 1, and a legal enum), so it survives the decode layer and reaches Logic,
    // where the fallback must replace it.
    connect(p1->agent(), &Agent::upgradeRequested, &server, [p1](int) { p1->agent()->upgrade({Data::UpgradeMaxHp}); });

    // The observable proof: p1 receives the upgrade result, which only happens if the fallback
    // replaced the infeasible reply with a feasible default and the game advanced past the phase.
    int upgradeResults = 0;
    connect(p1->agent(), &Agent::upgradeNotified, &server, [&upgradeResults](const QHash<QString, QList<Data::UpgradeItem>> &) { ++upgradeResults; });

    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p2->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p1->room()->player(p2->objectName()) != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(upgradeResults >= 1, 15000);
}

// A player whose socket drops during the upgrade phase must still let the game advance: the
// server's default upgrade reply used to fill every point into UpgradeMaxHp, which
// Logic::upgradeReply rejected once maxHp was maxed out, permanently stalling the upgrade phase
// (D-030 / Qwen 09-03 review finding 1b). 1daf4d2 made defaultReplyUpgrade send an empty list (the
// connection cannot know each stat's remaining-upgrade count), and D-036's fallback then
// auto-spends the absent player's point for them, knife damage first. p1 (Rock, always beating
// p2's Scissors) kills the 1-HP p2 and earns one upgrade point, then drops its socket as the
// upgrade request arrives. The server answers with the deliberately infeasible empty reply,
// Logic's fallback auto-spends the point, the logic advances to the round-over abandonment check,
// and p2 -- still online -- observes the game over. maximumMaxHp == initialMaxHp so the old
// UpgradeMaxHp default would have been infeasible (the exact deadlock this guards against).
void tst_QMdmmNetworking::client_disconnectDuringUpgradeStillAdvances()
{
    LogicConfiguration conf = LogicConfiguration::defaults();
    conf.setInitialMaxHp(1); // a single 1-damage slash kills the 1-HP victim
    conf.setMaximumMaxHp(1); // maxHp is already maxed out: the old default reply would be infeasible
    conf.setPunishHpModifier(0); // no city-slash self-punish, so p1 survives to earn the point

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16377);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16377"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [p1]() { p1->agent()->rockPaperScissors(Data::Rock); });
    auto *p2 = new Client(ClientConfiguration(), &server);
    connect(p2->agent(), &Agent::rockPaperScissorsRequested, &server, [p2]() { p2->agent()->rockPaperScissors(Data::Scissors); });

    int step = 0;
    connect(p1->agent(), &Agent::actionRequested, &server, [p1, p2, &step](int) {
        ++step;
        switch (step) {
        case 1:
            p1->agent()->action(Data::BuyKnife, {}, 0);
            break;
        case 2:
            p1->agent()->action(Data::Move, {}, Data::Village);
            break;
        case 3:
            p1->agent()->action(Data::Move, {}, p1->room()->player(p2->objectName())->place());
            break;
        case 4:
            p1->agent()->action(Data::Slash, p2->objectName(), 0);
            break;
        default:
            p1->agent()->action(Data::DoNothing, {}, 0);
            break;
        }
    });

    // Drop p1's socket as soon as the upgrade request arrives. The server auto-replies with the
    // default upgrade reply (empty = deliberately infeasible, triggering the fallback auto-spend),
    // the logic advances past the upgrade phase, and the round-over abandonment check ends the game
    // for the still-online p2.
    connect(p1->agent(), &Agent::upgradeRequested, &server, [p1]() {
        QTcpSocket *sock = p1->findChild<QTcpSocket *>();
        if (sock != nullptr)
            sock->abort();
    });

    // The observable proof: p2 (still online) receives the game over that only happens if the
    // default upgrade reply advanced the game instead of stalling on the infeasible UpgradeMaxHp.
    bool p2GameOver = false;
    connect(p2->agent(), &Agent::gameOverNotified, &server, [&p2GameOver](const QStringList &) { p2GameOver = true; });

    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p2->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);
    QVERIFY(p1->room()->player(p2->objectName()) != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(p2GameOver, 15000);
}

// The transport is chosen by a prefix whitelist on the connect address: qmdmm:// is TCP,
// ws(s):// is WebSocket, a plain string (no "://") is a local socket, and anything else is
// rejected. qmdmms:// is deliberately rejected too: it would silently promise TLS over a
// still-plaintext transport. URI schemes are case-insensitive (RFC 3986), so the prefix is
// matched case-insensitively -- in both directions: a whitelisted scheme stays accepted in
// upper case, and an unknown one stays rejected.
void tst_QMdmmNetworking::socket_addressSchemeWhitelist()
{
    Socket socket;

    QVERIFY(socket.connectToHost(u"qmdmm://localhost:16378"_s));
    QVERIFY(socket.connectToHost(u"ws://localhost:16378"_s));
    QVERIFY(socket.connectToHost(u"wss://localhost:16378"_s));
    QVERIFY(socket.connectToHost(u"QMdmm"_s)); // plain name -> local socket

    // The scheme is case-insensitive (RFC 3986)
    QVERIFY(socket.connectToHost(u"QMDMM://localhost:16378"_s));
    QVERIFY(socket.connectToHost(u"WS://localhost:16378"_s));
    QVERIFY(socket.connectToHost(u"WSS://localhost:16378"_s));

    QVERIFY(!socket.connectToHost(u"qmdmms://localhost:16378"_s));
    QVERIFY(!socket.connectToHost(u"QMDMMS://localhost:16378"_s));
    QVERIFY(!socket.connectToHost(u"http://localhost:16378"_s));
    QVERIFY(!socket.connectToHost(u"ftp://localhost:16378"_s));
}

// The Socket wrapper answers three questions about itself: which transport it drives, whether it
// is in an error state, and what that error is. The accessors sit on top of the transport-specific
// private implementations (one type() per transport), so they are pinned here rather than through a
// live peer: the type follows the address the same way socket_addressSchemeWhitelist pins the
// whitelist, a socket without a transport yet answers TypeUnknown, and setError is a no-op there --
// the guard is the point of the last block, not a detail.
void tst_QMdmmNetworking::socket_accessorsReportTheTransportAndAnyError()
{
    {
        // Client side: the transport is created lazily by connectToHost(), so there is none yet.
        Socket socket;
        QCOMPARE(socket.type(), Socket::TypeUnknown);
        QVERIFY(!socket.error().has_value());
        QVERIFY(!socket.hasError());

        // Nothing to disconnect and nothing to record: a client-side socket that never connected
        // has no transport for the error to live on.
        socket.setError({.code = Socket::ProtocolError, .errorString = u"ignored"_s});
        QVERIFY(!socket.hasError());
    }

    {
        // Server side: the socket wraps a transport that is already there, so the type is known
        // before anything is connected.
        auto *tcp = new QTcpSocket;
        Socket socket(tcp);
        QCOMPARE(socket.type(), Socket::TypeQTcpSocket);
        QVERIFY(!socket.hasError());

        socket.setError({.code = Socket::ProtocolError, .errorString = u"protocol violation"_s});
        QVERIFY(socket.hasError());
        QVERIFY(socket.error().has_value());
        QCOMPARE(socket.error()->code, Socket::ProtocolError);
        QCOMPARE(socket.error()->errorString, u"protocol violation"_s);
    }

    {
        // One client-side socket, three schemes: every transport has to report itself.
        Socket socket;
        QVERIFY(socket.connectToHost(u"qmdmm://localhost:16380"_s));
        QCOMPARE(socket.type(), Socket::TypeQTcpSocket);
        QVERIFY(socket.connectToHost(u"QMdmmNetworkingTest"_s));
        QCOMPARE(socket.type(), Socket::TypeQLocalSocket);
        QVERIFY(socket.connectToHost(u"ws://localhost:16380"_s));
        QCOMPARE(socket.type(), Socket::TypeQWebSocket);
    }
}

// Speaking is a client-bound notify the server does not interpret: the client hands the text to
// its own agent, the connection encodes it for the wire, the server passes it straight to the
// agent, and the room broadcasts it to every agent in it -- which each receiving connection turns
// back into a wire packet. The receiving client then decodes it. Driven end-to-end through the
// public Server / Client API (the two-client shape the managed-state case uses); the room is left
// not full so no game traffic rides along with the speak.
void tst_QMdmmNetworking::client_speakReachesTheOtherPlayer()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3);
    serverConf.setTcpPort(16380);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(60);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16380"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr && p1->room()->player(p1->objectName()) != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    // p2 learns about the speaker before it can hear anything from them (the receiver drops a
    // speak whose speaker it does not know), so the wait above is part of the case, not preamble.
    QString heardFrom;
    QString heardWhat;
    connect(p2->agent(), &Agent::speakNotified, &server, [&heardFrom, &heardWhat](const QString &playerName, const QString &content) {
        heardFrom = playerName;
        heardWhat = content;
    });

    p1->agent()->speak(u"hello there"_s);

    QTRY_COMPARE_WITH_TIMEOUT(heardFrom, p1->objectName(), 5000);
    QCOMPARE(heardWhat, u"hello there"_s);
}

// A socket error travels as a signal argument, and a listener is free to ask for that call to be
// queued rather than delivered in place -- which a queued connection can only do when the type is
// known to the meta type system. The Q_DECLARE_METATYPE at the bottom of qmdmmsocket.h is what
// declares it so, and that declaration is what puts a QMetaTypeId<Socket::Error> there to ask:
// take it away and the line below stops compiling, which is the guard this case really rests on.
// What it adds at run time is the trip a queued connection would put the value through, and the
// id being a real one. Nothing else in the tree ever asks, so the reading is this case's alone.
void tst_QMdmmNetworking::socketError_isRegisteredAsAMetatype()
{
    const int id = QMetaTypeId<Socket::Error>::qt_metatype_id();
    QVERIFY(id != QMetaType::UnknownType);

    // Asking for the id is what registers the type; the value then has to survive the trip a
    // queued connection puts it through.
    const Socket::Error error {.code = Socket::ProtocolError, .errorString = u"keep me whole"_s};
    const QVariant carried = QVariant::fromValue(error);
    QCOMPARE(carried.metaType().id(), id);

    const Socket::Error back = carried.value<Socket::Error>();
    QCOMPARE(back.code, Socket::ProtocolError);
    QCOMPARE(back.errorString, u"keep me whole"_s);
}

// A packet the client knows the kind of but cannot make sense of is a protocol error, and every
// inbound kind says so for itself: each request and each notify has one parser, and a parser that
// walks away from a payload still has to report it -- the guard each of them arms at entry marks
// the socket errored and drops the connection (see ONERRPRINTJSON). The table below has one row
// per parsable kind, each fed a value of the wrong shape, so what answers in every row is that
// guard rather than any of the field checks behind it.
//
// The value is written straight onto a raw TCP server's accepted socket: the public Client API has
// no "send arbitrary packet" entry point, and only a peer that skips the protocol layer can put a
// wrong-shaped value on the wire. Each row gets its own connection, because the drop ends the one
// it happens on. The tables are the source of the kinds: every entry in ClientP's request and
// notify callback tables appears here except the three payload-less notifies (game start / round
// start / round over -- they take no value, so they have no guard to answer with) and the one
// whose operation side is still unimplemented.
void tst_QMdmmNetworking::client_dropsOnAnUnparseablePacket_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<int>("id");
    QTest::addColumn<bool>("reported");
    QTest::addColumn<QString>("what");

    // The four requests the server sends to the client. Each is dropped by its own parser's guard,
    // so the client's "connection lost" notice is what a listener sees.
    QTest::newRow("request-rock-paper-scissors") << int(Protocol::TypeRequest) << int(Protocol::RequestRockPaperScissors) << true << u"rock-paper-scissors request"_s;
    QTest::newRow("request-action-order") << int(Protocol::TypeRequest) << int(Protocol::RequestActionOrder) << true << u"action-order request"_s;
    QTest::newRow("request-action") << int(Protocol::TypeRequest) << int(Protocol::RequestAction) << true << u"action request"_s;
    QTest::newRow("request-upgrade") << int(Protocol::TypeRequest) << int(Protocol::RequestUpgrade) << true << u"upgrade request"_s;

    // The notifies a server or an agent sends to the client.
    QTest::newRow("notify-pong") << int(Protocol::TypeNotify) << int(Protocol::NotifyPongServer) << true << u"pong"_s;
    QTest::newRow("notify-version") << int(Protocol::TypeNotify) << int(Protocol::NotifyVersion) << true << u"version"_s;
    QTest::newRow("notify-logic-configuration") << int(Protocol::TypeNotify) << int(Protocol::NotifyLogicConfiguration) << true << u"logic configuration"_s;
    QTest::newRow("notify-agent-state-changed") << int(Protocol::TypeNotify) << int(Protocol::NotifyAgentStateChanged) << true << u"agent state change"_s;
    QTest::newRow("notify-player-added") << int(Protocol::TypeNotify) << int(Protocol::NotifyPlayerAdded) << true << u"player added"_s;
    QTest::newRow("notify-player-removed") << int(Protocol::TypeNotify) << int(Protocol::NotifyPlayerRemoved) << true << u"player removed"_s;
    QTest::newRow("notify-rock-paper-scissors") << int(Protocol::TypeNotify) << int(Protocol::NotifyRockPaperScissors) << true << u"rock-paper-scissors result"_s;
    QTest::newRow("notify-action-order") << int(Protocol::TypeNotify) << int(Protocol::NotifyActionOrder) << true << u"action-order result"_s;
    QTest::newRow("notify-action") << int(Protocol::TypeNotify) << int(Protocol::NotifyAction) << true << u"action result"_s;
    QTest::newRow("notify-upgrade") << int(Protocol::TypeNotify) << int(Protocol::NotifyUpgrade) << true << u"upgrade result"_s;
    QTest::newRow("notify-spoken") << int(Protocol::TypeNotify) << int(Protocol::NotifySpoken) << true << u"spoken text"_s;
    // The odd one out: notifyGameOver tears the connection down itself before it looks at the
    // payload, and it does not put the client on the reconnect path, so no "connection lost" is
    // announced -- the end state is a client that is simply no longer connected.
    QTest::newRow("notify-game-over") << int(Protocol::TypeNotify) << int(Protocol::NotifyGameOver) << false << u"game over"_s;
}

void tst_QMdmmNetworking::client_dropsOnAnUnparseablePacket()
{
    QFETCH(int, type);
    QFETCH(int, id);
    QFETCH(bool, reported);
    QFETCH(QString, what);

    // A string is the wrong shape whichever parser reads it: the request parsers want a number or
    // an object, the notify parsers want an object or an array.
    const QJsonValue payload(u"not what this kind carries"_s);
    const Packet packet = type == int(Protocol::TypeRequest) ? Packet(static_cast<Protocol::PacketType>(type), static_cast<Protocol::RequestId>(id), payload)
                                                             : Packet(static_cast<Protocol::NotifyId>(id), payload);

    QTcpServer rawServer;
    QVERIFY(rawServer.listen(QHostAddress::LocalHost, 0));

    bool sent = false;
    QObject::connect(&rawServer, &QTcpServer::newConnection, &rawServer, [&rawServer, &sent, &packet]() {
        QTcpSocket *sock = rawServer.nextPendingConnection();
        // One packet per connection: the first one is the end of it.
        if (sent)
            return;
        sent = true;
        sock->write(packet.serialize().append('\n'));
        sock->flush();
    });

    const QString host = u"qmdmm://127.0.0.1:%1"_s.arg(rawServer.serverPort());

    auto *client = new Client(ClientConfiguration(), &rawServer);
    bool connectionLost = false;
    connect(client, &Client::socketConnectionLost, &rawServer, [&connectionLost](const QString &) { connectionLost = true; });

    QVERIFY(client->connectToHost(host, Data::StateOnline));

    if (reported) {
        QVERIFY2(QTest::qWaitFor([&connectionLost]() { return connectionLost; }, 5000), qPrintable(u"the client kept the connection after a %1 it cannot parse"_s.arg(what)));
    } else {
        QVERIFY2(QTest::qWaitFor([client]() { return !client->isConnected(); }, 5000), qPrintable(u"the client kept the connection after a %1 it cannot parse"_s.arg(what)));
        // The odd one out tears its connection down before it even looks at the payload, and it
        // detaches the socket's signals on the way, so its drop is never announced as a connection
        // loss. Had the packet been turned away by the client's unknown-kind fallback instead, the
        // listener above would have heard it -- this is what tells the two apart.
        QVERIFY(!connectionLost);
    }
}

// A client that neither replies nor gives up is treated as gone: when a request goes out, the
// server arms a single-shot timer for (requestTimeout + the grace period); firing it disconnects
// the socket and applies the default reply, so the logic keeps advancing instead of waiting on
// the silent player forever. Driven end-to-end through the public Server / Client API: two
// clients join, the first RPS request goes out, both stay silent, and the connection loss each
// client reports is the proof it happened.
//
// The timer is fired early rather than waited on: with a non-zero requestTimeout the deadline is
// requestTimeout + grace, which is 75 s at the least (grace is 60 s and the smallest non-zero
// requestTimeout is 15), and the grace period is a private static of ServerConnectionP -- a *P
// type this test cannot link against under QMDMM_EXPORT_PRIVATE=NO. The object tree is the way
// in, since the live connection objects hang off the Server. Only the wait is shortened;
// timer -> requestTimeout -> the socket error the client sees is the real path.
//
// The sibling case server_zeroRequestTimeoutNeverArmsTheTimer covers the other half: at 0 the
// timer is never armed, so no amount of silence ends the wait.
void tst_QMdmmNetworking::server_requestTimeoutDisconnectsASilentClient()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16369);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(15); // a non-zero timeout: the request timer is armed

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16369"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    auto *p2 = new Client(ClientConfiguration(), &server);

    // Neither client answers: no reply slot is wired, so the server's request timer is what ends
    // the wait. p1 hearing the RPS request is how the test knows the timer has been armed.
    int requestsSeen = 0;
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [&requestsSeen]() { ++requestsSeen; });

    int p1Lost = 0;
    connect(p1, &Client::socketConnectionLost, &server, [&p1Lost](const QString &) { ++p1Lost; });
    int p2Lost = 0;
    connect(p2, &Client::socketConnectionLost, &server, [&p2Lost](const QString &) { ++p2Lost; });

    QVERIFY(p1->connectToHost(host, Data::StateOnlineBot));
    QVERIFY(p2->connectToHost(host, Data::StateOnline));

    // p2 fills the room, the game starts, and the first RPS request goes out to both.
    QTRY_VERIFY_WITH_TIMEOUT(requestsSeen >= 1, 5000);

    // Fire every live connection's timer. The class name is spelled out because the type itself
    // is private to the library; findChildren still hands over the live objects, and each of them
    // owns exactly the one request timer.
    int fired = 0;
    const QList<QObject *> children = server.findChildren<QObject *>();
    for (QObject *child : children) {
        if (!QString::fromLatin1(child->metaObject()->className()).endsWith(u"ServerConnectionP"_s))
            continue;
        const QList<QTimer *> timers = child->findChildren<QTimer *>();
        QCOMPARE(timers.size(), 1);
        ++fired;
        timers.first()->setInterval(0);
        timers.first()->start();
    }
    QCOMPARE(fired, 2);

    QTRY_VERIFY_WITH_TIMEOUT(p1Lost >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(p2Lost >= 1, 5000);
}

// A requestTimeout of 0 means "no timeout": the server must not give up on a player that stays
// silent, however long the silence lasts (feedback 86). The observable is the request timer
// itself -- at 0 it is left unarmed, so no silent client can ever be dropped by it. Driven the
// same way as the sibling case above, through the public Server / Client API with both clients
// staying silent; the request going out proves the connection walked the normal request path.
void tst_QMdmmNetworking::server_zeroRequestTimeoutNeverArmsTheTimer()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16381);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(0); // no timeout at all: the request timer stays unarmed

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16381"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    auto *p2 = new Client(ClientConfiguration(), &server);

    // Neither client answers; p1 hearing the RPS request is how the test knows the request went
    // over the wire.
    int requestsSeen = 0;
    connect(p1->agent(), &Agent::rockPaperScissorsRequested, &server, [&requestsSeen]() { ++requestsSeen; });

    int p1Lost = 0;
    connect(p1, &Client::socketConnectionLost, &server, [&p1Lost](const QString &) { ++p1Lost; });
    int p2Lost = 0;
    connect(p2, &Client::socketConnectionLost, &server, [&p2Lost](const QString &) { ++p2Lost; });

    QVERIFY(p1->connectToHost(host, Data::StateOnlineBot));
    QVERIFY(p2->connectToHost(host, Data::StateOnline));

    // p2 fills the room, the game starts, and the first RPS request goes out to both.
    QTRY_VERIFY_WITH_TIMEOUT(requestsSeen >= 1, 5000);

    // Every live connection owns exactly one request timer; with the timeout off none of them may
    // be running. The class name is spelled out because the type itself is private to the library.
    int connections = 0;
    const QList<QObject *> children = server.findChildren<QObject *>();
    for (QObject *child : children) {
        if (!QString::fromLatin1(child->metaObject()->className()).endsWith(u"ServerConnectionP"_s))
            continue;
        const QList<QTimer *> timers = child->findChildren<QTimer *>();
        QCOMPARE(timers.size(), 1);
        ++connections;
        QVERIFY(!timers.first()->isActive());
    }
    QCOMPARE(connections, 2);

    // Nothing else ends the wait either: the clients stay silent and the server keeps their
    // connections open.
    QTest::qWait(500);
    QCOMPARE(p1Lost, 0);
    QCOMPARE(p2Lost, 0);
}

// One packet is one line, so a peer that has sent a packet's worth of bytes without ever ending a
// line is not speaking the protocol. The server must not hold an unbounded amount of such data:
// every socket carries a bound on one packet (ServerConfiguration::maximumPacketSize, which starts
// at Socket::DefaultMaximumPacketSize for everyone), the bound is handed to the transport's own
// read buffer so a peer cannot grow the process past it, and reaching it ends the connection with
// a ProtocolError -- the error any other unusable packet gets -- instead of waiting for a line that
// may never come.
//
// Driven over a raw QTcpSocket: a Client would frame the packets itself, and the framing is exactly
// what is under test. A normal packet goes out over that same connection and configuration just
// before the flood and comes back answered, which is what says the bound is what turns the flood
// away rather than something about this server being unable to talk at all.
//
// The process' own memory is deliberately not measured here: that would take a platform-specific
// high-water read (getrusage) whose baseline is polluted by every other case in this binary, and it
// could only ever confirm what the bound already guarantees structurally. Pinned instead are the
// bound itself -- a small constant by default, so no single socket can hold more than that -- and
// the observable consequence of reaching it.
void tst_QMdmmNetworking::server_givesUpOnAPeerThatNeverEndsAPacket()
{
    // The framing layer's own starting bound: it is what every socket uses unless a server lowers
    // it, so this is the number that caps a socket's memory. Small on purpose, not a placeholder.
    {
        Socket socket;
        QCOMPARE(socket.maximumPacketSize(), Socket::DefaultMaximumPacketSize);
        QCOMPARE(Socket::DefaultMaximumPacketSize, 1024 * 1024);
    }

    LogicConfiguration conf = LogicConfiguration::defaults();

    const int packetSizeBound = 64 * 1024;

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setTcpPort(16384);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(0); // nothing but the framing bound may end this connection
    serverConf.setMaximumPacketSize(packetSizeBound);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    // A raw peer: it speaks the wire format by hand and never signs in, so from the server's point
    // of view it stays a bare connection -- no room and no game traffic are involved.
    auto *peer = new QTcpSocket(&server);
    peer->connectToHost(QHostAddress::LocalHost, 16384);
    QVERIFY(peer->waitForConnected(5000));

    // The control: a normal packet, over this very connection. A server-bound ping comes back as a
    // pong, so the answer proves the server framed the line and dispatched it -- the same path the
    // flood below is expected to be turned away from.
    peer->write(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyPingServer, QJsonValue(qint64(1))).serialize().append('\n'));
    peer->flush();

    bool pongSeen = false;
    QVERIFY2(QTest::qWaitFor(
                 [peer, &pongSeen]() {
                     while (peer->canReadLine()) {
                         const QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(peer->readLine());
                         if (packet.type() == QMdmmCore::Protocol::TypeNotify && packet.notifyId() == QMdmmCore::Protocol::NotifyPongServer)
                             pongSeen = true;
                     }
                     return pongSeen;
                 },
                 5000),
             "the server never answered a normal packet, so this connection was not usable to begin with");

    // The flood: far more than the bound, and not a single line ending anywhere in it.
    bool dropped = false;
    QObject::connect(peer, &QAbstractSocket::disconnected, peer, [&dropped]() { dropped = true; });

    peer->write(QByteArray(packetSizeBound * 8, 'x'));
    peer->flush();

    QVERIFY2(QTest::qWaitFor([&dropped]() { return dropped; }, 10000), "the server kept a peer that never ended a packet connected, so it was buffering without a bound");

    // The server side records why. The peer never signed in, so the connection object is still
    // there to be asked; it never went through any of the game paths that would report this.
    const QList<Socket *> serverSockets = server.findChildren<Socket *>();
    QCOMPARE(serverSockets.size(), 1);

    const auto serverError = serverSockets.first()->error();
    QVERIFY2(serverError.has_value(), "the server dropped the connection without recording a reason");
    QCOMPARE(serverError->code, Socket::ProtocolError);
}

// The bound above is about the transport holding no more than one packet's worth of data, and a
// WebSocket does not frame packets with newlines: it delivers one message per packet. So the bound
// has nowhere to go unless it is handed to the WebSocket layer itself -- left alone, a peer can make
// the server assemble a message of any size, and the packet layer only gets a say once the whole
// message is already in memory.
//
// The control is a normal packet over the same connection, answered: that says the connection speaks
// the protocol, so what turns the flood away below is the bound and not a server unable to talk. The
// flood is a packet the packet layer would otherwise accept -- a well-formed ping whose size comes
// from its padding -- so a run that lost the bound fails on the answer it should never have given,
// not only on a connection that stays open.
void tst_QMdmmNetworking::server_givesUpOnAnOversizedWebSocketMessage()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    const int packetSizeBound = 64 * 1024;

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setTcpEnabled(false);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(true);
    serverConf.setWebsocketPort(16393);
    serverConf.setRequestTimeout(0); // nothing but the framing bound may end this connection
    serverConf.setSignInTimeout(60); // the peer stays a bare connection on purpose
    serverConf.setMaximumPacketSize(packetSizeBound);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    QWebSocket peer;

    int pongs = 0;
    QObject::connect(&peer, &QWebSocket::binaryMessageReceived, &peer, [&pongs](const QByteArray &arr) {
        const QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(arr);
        if (packet.type() == Protocol::TypeNotify && packet.notifyId() == Protocol::NotifyPongServer)
            ++pongs;
    });

    bool connected = false;
    QObject::connect(&peer, &QWebSocket::connected, &peer, [&connected]() { connected = true; });

    peer.open(QUrl(u"ws://localhost:16393"_s));
    QVERIFY2(QTest::qWaitFor([&connected]() { return connected; }, 5000), "the peer never reached the server");

    peer.sendBinaryMessage(QMdmmCore::Packet(Protocol::NotifyPingServer, QJsonValue(qint64(1))).serialize());
    QVERIFY2(QTest::qWaitFor([&pongs]() { return pongs >= 1; }, 5000), "the server never answered a normal packet, so this connection was not usable to begin with");

    // Well over the bound, and a packet in every other respect: the padding is an unknown key, which
    // decoding preserves and otherwise ignores.
    QJsonObject padding;
    padding.insert(u"padding"_s, QString(packetSizeBound, QLatin1Char('x')));
    const QByteArray flood = QMdmmCore::Packet(Protocol::NotifyPingServer, padding).serialize();
    QVERIFY2(flood.size() > packetSizeBound, "the flood did not outgrow the bound, so this case would not test it");

    bool dropped = false;
    QObject::connect(&peer, &QWebSocket::disconnected, &peer, [&dropped]() { dropped = true; });

    peer.sendBinaryMessage(flood);

    QVERIFY2(QTest::qWaitFor([&dropped]() { return dropped; }, 10000),
             "the server kept a peer whose message outgrew the bound connected, so it was assembling a message without a bound");

    // The flood never became a packet: the transport dropped it before the packet layer could answer
    // it, which is the difference between a bound and a peer that simply went away.
    QCOMPARE(pongs, 1);
}

// The read-only status snapshot reports how many rooms exist, how many of them are running a game,
// and how many players are online. Driven end-to-end through the public Server / Client API: a
// three-person room with two players signed in is still recruiting, so "games" stays 0, and the
// third sign-in fills the room, which is when the game starts. The snapshot is also read before
// anyone connects, where all three counts have to be zero.
void tst_QMdmmNetworking::server_statusReportsRoomsGamesAndPlayers()
{
    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(3); // 3-person room: two players leave it still recruiting
    serverConf.setTcpPort(16382);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(0); // the bots stay silent; a 0 timeout leaves them connected

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const auto statusCount = [&server](const QString &key) { return server.status().value(key).toInt(); };

    // Nothing has connected yet.
    QCOMPARE(statusCount(u"rooms"_s), 0);
    QCOMPARE(statusCount(u"games"_s), 0);
    QCOMPARE(statusCount(u"players"_s), 0);

    const QString host = u"qmdmm://localhost:16382"_s;

    auto *p1 = new Client(ClientConfiguration(), &server);
    QVERIFY(p1->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(p1->room() != nullptr, 5000);

    auto *p2 = new Client(ClientConfiguration(), &server);
    QVERIFY(p2->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p2->room() != nullptr && p2->room()->player(p1->objectName()) != nullptr, 5000);

    // One room, two players online, and no game yet -- the room is not full.
    QTRY_COMPARE(statusCount(u"rooms"_s), 1);
    QTRY_COMPARE(statusCount(u"players"_s), 2);
    QCOMPARE(statusCount(u"games"_s), 0);

    auto *p3 = new Client(ClientConfiguration(), &server);
    QVERIFY(p3->connectToHost(host, Data::StateOnlineBot));
    QTRY_VERIFY_WITH_TIMEOUT(p3->room() != nullptr, 5000);

    // The room is full now, which is when the game starts. Still a single room, still three players.
    QTRY_COMPARE(statusCount(u"games"_s), 1);
    QCOMPARE(statusCount(u"rooms"_s), 1);
    QTRY_COMPARE(statusCount(u"players"_s), 3);

    // A player who drops while the game is running keeps their seat, so the room stays full and the
    // game stays counted; they simply stop being online, which is what the player count reports.
    p3->disconnectFromHost();
    QTRY_COMPARE(statusCount(u"players"_s), 2);
    QCOMPARE(statusCount(u"games"_s), 1);
    QCOMPARE(statusCount(u"rooms"_s), 1);
}

// The inbound path is the only place a peer's bytes are trusted, so a frame the server cannot use
// has to be turned away without taking anything else with it. This is the corpus behind that claim.
// Three families go in over a raw socket, one row at a time:
//
//   - the parser refuses the frame, and the recorded reason is fromJson's own message naming the
//     offending field (unparseable, truncated and non-object JSON, a fractional or out-of-range
//     enum field, a request carrying a notify id and the other way round, a missing value, bytes
//     that are not UTF-8, a nesting depth the parser refuses);
//   - the parser accepts the frame, but the packet is not one this side may send, and the dispatch
//     layer drops it: control traffic requiring a signed-in player, before signing in (observe,
//     managed-changed) or with a payload that is not the object sign-in needs;
//   - the frame is legal client-to-server traffic (one ping; two pings in a single write), which
//     must come back answered with the connection kept -- that is what says the rows above are
//     turned away for what they are, rather than by a server that drops whatever it is handed.
//
// One more row pins the other half of "not acted on": a request, which only the logic ever sends
// (D-025), arriving from a peer that has not signed in is neither acted on nor punished -- the
// connection stays up and a ping after it is still answered. That is recorded here rather than
// filed anywhere: it costs the server nothing (the packet-size bound caps what such a peer can
// make it hold) and it is what the code deliberately does, not a gap in the corpus above.
//
// The collateral half is the part a unit test usually cannot reach: a legitimate Client is signed
// in and seated the whole time, and punishing the other connection must leave it connected with its
// player still in the room. The offending peer never signs in, so it is not *in* that room -- what
// is checked here is the weaker, checkable claim that one connection's punishment does not reach
// another.
//
// Driven over a raw QTcpSocket: a Client would frame the packets itself, and the framing is exactly
// what the corpus is feeding. Where a parser message is expected, only its "Json error: " prefix is
// pinned: the rest of the text is Qt's, and this tree is built against more than one Qt version.
// The reason is read back from the server-side Socket (the same observable the packet-size bound
// case uses) rather than from a log line, because the drop path records the parser's own message
// there and logs nothing at all.
//
// The same rows are also fed to the server *executable*, by the case right after this one -- read
// that one for what a server in its own process adds and what it cannot observe.
namespace {
// The corpus as data rather than a row list written straight into the data function, because two
// cases drive it: the in-process one below, which reads each row's recorded reason off the
// server-side Socket, and the process-level one after it, which feeds the same frames to the server
// executable. One list is what makes the second case the same claim rather than a second one.
struct UnusableFrame
{
    const char *name;
    QByteArray frame;
    bool expectDrop;
    QString expectReason; // empty where the drop comes with no message of the parser's own
    int expectPongs;
};

QList<UnusableFrame> unusableFrameCorpus()
{
    const auto asFrame = [](const QByteArray &json) {
        QByteArray ret = json;
        ret.append('\n');
        return ret;
    };

    const QByteArray ping = asFrame(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyPingServer, QJsonValue(qint64(1))).serialize());

    return {
        // The parser refuses these, and the recorded reason is its own message.
        {"json-is-unparseable", "hello, client\n"_ba, true, u"Json error: "_s, 0},
        {"json-is-truncated", asFrame(R"json({"type":1,"requestId":2,"notifyId":0)json"), true, u"Json error: "_s, 0},
        {"json-is-not-an-object", asFrame("[1,2,3]"), true, u"Document is not object"_s, 0},
        {"enum-field-is-fractional", asFrame(R"json({"type":1.5,"requestId":2,"notifyId":0,"value":null})json"), true, u"'type' is not an integer"_s, 0},
        {"type-is-out-of-range", asFrame(R"json({"type":99,"requestId":0,"notifyId":0,"value":null})json"), true, u"'type' is out of range"_s, 0},
        {"request-id-is-out-of-range", asFrame(R"json({"type":1,"requestId":99,"notifyId":0,"value":null})json"), true, u"'requestId' is out of range"_s, 0},
        {"notify-id-is-out-of-range", asFrame(R"json({"type":3,"requestId":0,"notifyId":999,"value":null})json"), true, u"'notifyId' is out of range"_s, 0},
        {"request-carries-a-notify-id", asFrame(R"json({"type":1,"requestId":1,"notifyId":4097,"value":null})json"), true,
         u"'notifyId' should be invalid for a request/reply packet"_s, 0},
        {"notify-carries-a-request-id", asFrame(R"json({"type":3,"requestId":1,"notifyId":4097,"value":null})json"), true, u"'requestId' should be invalid for a notify packet"_s,
         0},
        {"value-is-missing", asFrame(R"json({"type":1,"requestId":2,"notifyId":0})json"), true, u"'value' is non-existent"_s, 0},
        {"bytes-are-not-utf8", asFrame(QByteArray(R"json({"type":1,"requestId":2,"notifyId":0,"value":")json") + QByteArray::fromHex("fffe") + QByteArray("\"}")), true,
         u"Json error: "_s, 0},
        {"value-is-nested-too-deeply",
         asFrame(QByteArray(R"json({"type":1,"requestId":2,"notifyId":0,"value":)json") + QByteArray(4096, '[') + QByteArray(4096, ']') + QByteArray("}")), true, u"Json error: "_s,
         0},

        // The parser accepts these; the dispatch layer is what turns them away. They carry no parser
        // message, so the drop itself is the whole observation.
        {"sign-in-payload-is-not-an-object", asFrame(R"json({"type":3,"requestId":0,"notifyId":16386,"value":"a string, not the object sign-in needs"})json"), true, QString(), 0},
        {"observe-before-sign-in", asFrame(R"json({"type":3,"requestId":0,"notifyId":16387,"value":null})json"), true, QString(), 0},
        {"managed-changed-before-sign-in", asFrame(R"json({"type":3,"requestId":0,"notifyId":16388,"value":null})json"), true, QString(), 0},

        // Legal client-to-server traffic.
        {"one-ping", ping, false, QString(), 1},
        {"two-pings-in-one-write", ping + ping, false, QString(), 2},

        // Not acted on, and not punished either: the connection is still there afterwards, which the
        // ping that follows the request proves.
        {"a-request-before-sign-in-is-ignored", asFrame(R"json({"type":1,"requestId":1,"notifyId":0,"value":null})json") + ping, false, QString(), 1},
    };
}
} // namespace

void tst_QMdmmNetworking::server_turnsAwayUnusableFramesWithoutDisturbingTheRoom_data()
{
    QTest::addColumn<QByteArray>("frame");
    QTest::addColumn<bool>("expectDrop");
    QTest::addColumn<QString>("expectReason");
    QTest::addColumn<int>("expectPongs");

    for (const UnusableFrame &row : unusableFrameCorpus())
        QTest::newRow(row.name) << row.frame << row.expectDrop << row.expectReason << row.expectPongs;
}

void tst_QMdmmNetworking::server_turnsAwayUnusableFramesWithoutDisturbingTheRoom()
{
    QFETCH(QByteArray, frame);
    QFETCH(bool, expectDrop);
    QFETCH(QString, expectReason);
    QFETCH(int, expectPongs);

    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16385);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(0); // the bystander stays silent and must stay connected

    // These two are written from lambdas whose context is the socket below, and that socket is a
    // child of the server: destroying the server deletes the socket, which emits one last
    // disconnected, which runs the lambda. Declared before the server, they are destroyed after it,
    // so that last write lands in storage that is still alive; declared where they are used, they
    // would already be gone (AddressSanitizer calls that stack-use-after-scope).
    bool peerDropped = false;
    int pongs = 0;

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16385"_s;

    // The bystander: a legitimate client, signed in and seated before anything happens.
    auto *bystander = new Client(ClientConfiguration(), &server);
    QVERIFY(bystander->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(bystander->room() != nullptr && bystander->room()->player(bystander->objectName()) != nullptr, 5000);

    // The offender: a raw socket that speaks the wire format by hand and never signs in.
    auto *peer = new QTcpSocket(&server);
    peer->connectToHost(QHostAddress::LocalHost, 16385);
    QVERIFY(peer->waitForConnected(5000));

    QObject::connect(peer, &QAbstractSocket::disconnected, peer, [&peerDropped]() { peerDropped = true; });

    QObject::connect(peer, &QAbstractSocket::readyRead, peer, [peer, &pongs]() {
        while (peer->canReadLine()) {
            const QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(peer->readLine());
            if (packet.type() == QMdmmCore::Protocol::TypeNotify && packet.notifyId() == QMdmmCore::Protocol::NotifyPongServer)
                ++pongs;
        }
    });

    peer->write(frame);
    peer->flush();

    if (expectPongs > 0)
        QTRY_COMPARE_WITH_TIMEOUT(pongs, expectPongs, 5000);
    else
        QTest::qWait(200);

    if (expectDrop) {
        QVERIFY2(QTest::qWaitFor([&peerDropped]() { return peerDropped; }, 10000), "the server kept a peer that sent a frame it could not use");

        // The server records why, on the connection object it was talking over.
        QList<Socket *> failed;
        for (Socket *socket : server.findChildren<Socket *>())
            if (socket->error().has_value())
                failed.append(socket);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first()->error()->code, Socket::ProtocolError);
        if (!expectReason.isEmpty())
            QVERIFY2(failed.first()->error()->errorString.contains(expectReason),
                     qPrintable(u"expected the recorded reason to name the offending field, got \"%1\""_s.arg(failed.first()->error()->errorString)));
    } else {
        QVERIFY2(!peerDropped, "the server turned away legal client-to-server traffic");
        QCOMPARE(peer->state(), QAbstractSocket::ConnectedState);
    }

    // The collateral: the legitimate client is untouched and still seated.
    QVERIFY2(bystander->isConnected(), "punishing one connection took the legitimate one with it");
    QVERIFY(bystander->room() != nullptr);
    QVERIFY(bystander->room()->player(bystander->objectName()) != nullptr);
}

// The corpus above goes to a Server object living in this process, where every row's outcome can be
// read off the server-side Socket. One reading stays out of reach that way: whether the *executable*
// survives the same traffic. An abort in the server's own code would end this test process with the
// case's verdict still owed, which is not the same thing as watching a server process end on an
// abort -- so here the server is spawned, the same rows go in over TCP, and the two questions a
// process can be asked are the two that get asked: it is still running once the last row has been
// answered or dropped, and SIGTERM then ends it as an ordinary stop (status 0) rather than as a
// crash.
//
// The reason half is not repeated: there is no Socket object in this process to read it from, and
// the drop itself is the whole of what this case observes. The legitimate client sits in the
// spawned server's room exactly as in the in-process case, so "one connection's punishment does not
// reach another" is checked across a process boundary as well.
void tst_QMdmmNetworking::server_survivesTheCorpusInItsOwnProcess()
{
    // The run needs a free port of its own -- the in-process cases take theirs from the 1636x range,
    // and a server bound to one of those while its case still holds it would be a conflict. Taking a
    // port from the kernel and handing it back is the shape tst_qmdmmserver.cpp uses.
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::Any, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();

    const QString host = u"qmdmm://localhost:%1"_s.arg(port);

    // Only the transport this case talks to is turned on: the executable's defaults would also bind
    // the local socket and the WebSocket port, which other cases and other runs have a claim on. The
    // operation timeout is off for the same reason it is off above -- the legitimate client is
    // silent and has to stay connected.
    QProcess server;
    server.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), {u"-t"_s, u"on"_s, u"-p"_s, QString::number(port), u"-l"_s, u"off"_s, u"-w"_s, u"off"_s, u"-o"_s, u"0"_s});
    QVERIFY2(server.waitForStarted(60000), "the server executable did not start");

    // The executable says nothing when its listener is up -- a release build compiles the debug log
    // out, and it is not this case's subject anyway -- so readiness is read off the port. What says
    // the run is listening is the version packet it sends on every socket it accepts: a run that has
    // not reached its loop sends nothing, and a socket of this case's own has no other reason to
    // hear from a server.
    //
    // The connect is not that reading. Measured on this machine (macOS 27, Qt 6.12): a
    // waitForConnected() to a port with no listener comes back true, in ConnectedState, with a null
    // peer address and a peer port of 0 -- so a wait made of the connect alone ends on its first
    // attempt whatever the run is doing, and the wait it was meant to be never happens. The peer is
    // read instead: an attempt that hears the version packet is one the run took, and an attempt
    // that hears nothing is one it did not. An attempt takes a socket of its own, since a refused
    // connect leaves nothing to retry.
    bool listening = false;
    QElapsedTimer readiness;
    readiness.start();
    while (!listening && readiness.elapsed() < 10000) {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        if (probe.waitForConnected(500) && probe.peerPort() == port && probe.waitForReadyRead(1000)) {
            while (probe.canReadLine()) {
                const QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(probe.readLine());
                if (packet.type() == QMdmmCore::Protocol::TypeNotify && packet.notifyId() == QMdmmCore::Protocol::NotifyVersion)
                    listening = true;
            }
        }
        probe.abort();
        if (!listening)
            QTest::qWait(50);
    }
    QVERIFY2(listening, "the server executable never started listening");

    // The bystander: a legitimate client, signed in and seated before anything happens.
    auto *bystander = new Client(ClientConfiguration(), this);
    QVERIFY(bystander->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(bystander->room() != nullptr && bystander->room()->player(bystander->objectName()) != nullptr, 5000);

    // The offender of each row is a socket of its own, and every one of them captures state that
    // lives as long as the case rather than as long as the iteration: a socket outliving its turn
    // would otherwise talk to storage that is gone.
    bool peerDropped = false;
    int pongs = 0;

    for (const UnusableFrame &row : unusableFrameCorpus()) {
        const QString name = QString::fromLatin1(row.name);

        peerDropped = false;
        pongs = 0;

        auto *peer = new QTcpSocket(this);
        peer->connectToHost(QHostAddress::LocalHost, port);
        // A connect counts only when it carries the peer: the same reading as above, and the reason
        // this assertion can fail at all.
        QVERIFY2(peer->waitForConnected(5000) && peer->peerPort() == port, qPrintable(u"%1: the server executable refused a connection"_s.arg(name)));

        QObject::connect(peer, &QAbstractSocket::disconnected, peer, [&peerDropped]() { peerDropped = true; });
        QObject::connect(peer, &QAbstractSocket::readyRead, peer, [peer, &pongs]() {
            while (peer->canReadLine()) {
                const QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(peer->readLine());
                if (packet.type() == QMdmmCore::Protocol::TypeNotify && packet.notifyId() == QMdmmCore::Protocol::NotifyPongServer)
                    ++pongs;
            }
        });

        peer->write(row.frame);
        peer->flush();

        if (row.expectPongs > 0)
            QVERIFY2(QTest::qWaitFor([&pongs, &row]() { return pongs >= row.expectPongs; }, 5000), qPrintable(u"%1: the frame went unanswered"_s.arg(name)));
        else
            QTest::qWait(200);

        if (row.expectDrop) {
            QVERIFY2(QTest::qWaitFor([&peerDropped]() { return peerDropped; }, 10000), qPrintable(u"%1: the server kept a peer that sent a frame it could not use"_s.arg(name)));
        } else {
            QVERIFY2(!peerDropped, qPrintable(u"%1: the server turned away legal client-to-server traffic"_s.arg(name)));
            QCOMPARE(peer->state(), QAbstractSocket::ConnectedState);
        }

        // The socket is done with its turn, and its connections go with it: closing a socket is
        // itself a disconnect, so a socket left to be destroyed during a later iteration would report
        // that to that iteration's flag.
        QObject::disconnect(peer, nullptr, peer, nullptr);
        peer->deleteLater();
    }

    // Every row above was answered or dropped, and on its own that only says the server got through
    // the corpus one row at a time. Here is the reading this case exists for: a process that aborted
    // is not a process that is running, and an abort on the last row is exactly the one no later row
    // would have noticed.
    QTest::qWait(200);
    QVERIFY2(server.state() == QProcess::Running, "the corpus took the server executable down");

    // The collateral, this time across the process boundary.
    QVERIFY2(bystander->isConnected(), "punishing one connection took the legitimate one with it");
    QVERIFY(bystander->room() != nullptr);
    QVERIFY(bystander->room()->player(bystander->objectName()) != nullptr);

    // And the shape of the ending. The handler for a stop signal is installed on POSIX only, so that
    // half is read there and the run is simply killed elsewhere -- the same split as
    // tst_qmdmmserver.cpp's stop-signal case takes, which is where the contract itself lives.
#ifdef Q_OS_UNIX
    server.terminate();
    QVERIFY2(server.waitForFinished(15000), "the server executable did not stop on SIGTERM");
    QVERIFY(server.exitStatus() == QProcess::NormalExit);
    QCOMPARE(server.exitCode(), 0);
#else
    server.kill();
    QVERIFY(server.waitForFinished(15000));
#endif
}

// A peer that connects and never signs in is not a player yet, so nothing else in the server would
// ever notice it: without a sign-in deadline the socket (and everything behind it) is held for as
// long as the peer cares to keep it open. This case pins the deadline down -- it is armed on every
// accepted socket, with the interval the configuration was given.
//
// The case does not wait the configured time out: the deadline is a single-shot timer the server
// parents to the socket it guards, so the case reads the interval it was armed with (which is what
// shows the value comes from the configuration rather than from a constant) and then fires it, the
// way server_requestTimeoutDisconnectsASilentClient fires the connections' request timers. The
// bystander is the other half of the judgement: a connection that *did* sign in outlives the same
// deadline, since the deadline is about a peer that never became a player, not about silence.
void tst_QMdmmNetworking::server_signInDeadlineReapsAPeerThatNeverSignsIn()
{
    constexpr int signInTimeout = 45;

    LogicConfiguration conf = LogicConfiguration::defaults();

    ServerConfiguration serverConf = ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(2);
    serverConf.setTcpPort(16390);
    serverConf.setLocalEnabled(false);
    serverConf.setWebsocketEnabled(false);
    serverConf.setRequestTimeout(0); // not this case's timer: the bystander must not be dropped by it
    serverConf.setSignInTimeout(signInTimeout);

    Server server(serverConf, conf);
    QVERIFY(server.listen());

    const QString host = u"qmdmm://localhost:16390"_s;

    // A legitimate client: it signs in, so the deadline has to leave it and its seat alone.
    auto *bystander = new Client(ClientConfiguration(), &server);
    QVERIFY(bystander->connectToHost(host, Data::StateOnline));
    QTRY_VERIFY_WITH_TIMEOUT(bystander->room() != nullptr && bystander->room()->player(bystander->objectName()) != nullptr, 5000);

    // The idle peer: it connects and never says anything, not even a sign-in.
    auto *peer = new QTcpSocket(&server);
    peer->connectToHost(QHostAddress::LocalHost, 16390);
    QVERIFY(peer->waitForConnected(5000));

    bool peerDropped = false;
    QObject::connect(peer, &QAbstractSocket::disconnected, peer, [&peerDropped]() { peerDropped = true; });

    // The version packet is what the server sends when it introduces a socket, so waiting for it is
    // how this case knows the server's side of the peer's connection exists -- and with it, whatever
    // deadline that connection was (or was not) given. (The server accepts the connection from its
    // own event loop, so this also gives that loop its turn.)
    QVERIFY2(QTest::qWaitFor([&peer]() { return peer->canReadLine(); }, 5000), "the server never introduced the peer's connection");

    // The deadlines the server armed for its own sockets. A transport creates timers of its own (the
    // connect timeout, say), and those hang off the transport rather than off the wrapper, so the
    // parent is what separates them from the sign-in deadline, which the server parents to the
    // socket it guards.
    QList<QTimer *> deadlines;
    for (Socket *socket : server.findChildren<Socket *>()) {
        for (QTimer *timer : socket->findChildren<QTimer *>()) {
            if (timer->parent() == socket)
                deadlines.append(timer);
        }
    }

    // One per accepted socket: the bystander's and the peer's.
    QCOMPARE(deadlines.size(), 2);
    for (const QTimer *deadline : deadlines) {
        QVERIFY(deadline->isSingleShot());
        QCOMPARE(deadline->interval(), signInTimeout * 1000);
    }

    for (QTimer *deadline : deadlines) {
        deadline->setInterval(0);
        deadline->start();
    }

    QVERIFY2(QTest::qWaitFor([&peerDropped]() { return peerDropped; }, 10000), "the server kept a peer that never signed in");

    // The server records why, on the connection object it was talking over.
    QList<Socket *> failed;
    for (Socket *socket : server.findChildren<Socket *>())
        if (socket->error().has_value())
            failed.append(socket);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first()->error()->code, Socket::ProtocolError);
    QVERIFY2(failed.first()->error()->errorString.contains(u"did not sign in"_s),
             qPrintable(u"expected the recorded reason to say the peer never signed in, got \"%1\""_s.arg(failed.first()->error()->errorString)));

    // The collateral: the connection that signed in outlived the deadline and is still seated.
    QVERIFY2(bystander->isConnected(), "the sign-in deadline took a connection that had signed in");
    QVERIFY(bystander->room() != nullptr);
    QVERIFY(bystander->room()->player(bystander->objectName()) != nullptr);
}

namespace {
RegisterTestObject<tst_QMdmmNetworking> _;
}
#include "tst_qmdmmnetworking.moc"

// NOLINTEND

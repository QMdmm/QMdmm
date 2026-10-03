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

#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

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
    void client_routesLogicConfigurationToSelfAgent();
    void client_declaresManagedStateToServer();
    void server_disconnectsOnAbnormalPacket();
    void server_disconnectsOnOutOfRangeReply();
    void server_disconnectsOnOversizedActionOrderReply();
    void server_disconnectsOnOversizedUpgradeReply();
    void client_disconnectsOnAbnormalPacket();
    void server_doesNotDropServerBoundNotify();
    void client_disconnectFromHostStopsAutoReconnect();
    void client_disconnectsOnProtocolVersionMismatch();
    void server_listenErrorAndClose();
    void client_infeasibleUpgradeReplyDoesNotStall();
    void client_disconnectDuringUpgradeStillAdvances();
    void socket_addressSchemeWhitelist();
    void socket_accessorsReportTheTransportAndAnyError();
    void client_speakReachesTheOtherPlayer();
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

    // p2 observes p1's state broadcasts. The drop reports p1 offline (Online cleared); only the
    // reconnect broadcasts p1 online again, and it must do so with Trust still clear. If the old
    // auto-Trust behavior regressed, no broadcast would ever carry Online-without-Trust and this
    // flag stays false, failing the assertion below.
    bool p1OnlineNotTrusted = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p1OnlineNotTrusted, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && state.testFlag(Data::StateMaskOnline) && !state.testFlag(Data::StateMaskTrust))
            p1OnlineNotTrusted = true;
    });

    QTcpSocket *p1Sock = p1->findChild<QTcpSocket *>();
    QVERIFY(p1Sock != nullptr);

    bool reconnected = false;
    connect(p1, &Client::socketReconnectSucceeded, &server, [&reconnected]() { reconnected = true; });
    p1Sock->abort();

    QTRY_VERIFY_WITH_TIMEOUT(reconnected, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(p1OnlineNotTrusted, 5000);
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
// Agent::setManaged, the client declares the new flag on the wire, and the server -- which owns the
// agent state -- applies it and reports the result back through the ordinary agent state broadcast.
// A second player in the same room observes the declaration, which is what makes this a
// synchronized flag rather than a client-local one. Withdrawing it is part of the contract too: the
// managed state is a toggle, not a one-way grant.
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
    QVERIFY(!p1->agent()->managed());

    bool p2SawManaged = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p2SawManaged, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && state.testFlag(Data::StateMaskTrust))
            p2SawManaged = true;
    });

    p1->agent()->setManaged(true);
    QTRY_VERIFY_WITH_TIMEOUT(p2SawManaged, 5000);

    // The declaring client reflects it as well (applied on the way out, then confirmed by the
    // broadcast), and the flag is the only thing that moved: the player is still online, and p2's
    // own state is not touched -- a declaration is about the declaring player only.
    QVERIFY(p1->agent()->managed());
    QVERIFY(p1->agent()->state().testFlag(Data::StateMaskOnline));
    QVERIFY(!p2->agent()->managed());

    bool p2SawUnmanaged = false;
    connect(p2->agent(), &Agent::agentStateChangeNotified, &server, [&p2SawUnmanaged, p1](const QString &playerName, const Data::AgentState &state) {
        if (playerName == p1->objectName() && !state.testFlag(Data::StateMaskTrust))
            p2SawUnmanaged = true;
    });

    p1->agent()->setManaged(false);
    QTRY_VERIFY_WITH_TIMEOUT(p2SawUnmanaged, 5000);
    QVERIFY(!p1->agent()->managed());
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

namespace {
RegisterTestObject<tst_QMdmmNetworking> _;
}
#include "tst_qmdmmnetworking.moc"

// NOLINTEND

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmserverconnection_p.h"

#include <QMdmmProtocol>

#include <QJsonArray>
#include <QRandomGenerator>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace QMdmmNetworking {
namespace p {

const QHash<QMdmmCore::Protocol::NotifyId, void (ServerConnectionP::*)(const QJsonValue &)> &ServerConnectionP::notifyCallbacks()
{
    static const QHash<QMdmmCore::Protocol::NotifyId, void (ServerConnectionP::*)(const QJsonValue &)> callbacks {
        std::make_pair(QMdmmCore::Protocol::NotifySpeak, &ServerConnectionP::receiveSpeak),
        std::make_pair(QMdmmCore::Protocol::NotifyOperate, &ServerConnectionP::receiveOperate),
    };
    return callbacks;
}

const QHash<QMdmmCore::Protocol::RequestId, void (ServerConnectionP::*)(const QJsonValue &)> &ServerConnectionP::replyCallbacks()
{
    static const QHash<QMdmmCore::Protocol::RequestId, void (ServerConnectionP::*)(const QJsonValue &)> callbacks {
        std::make_pair(QMdmmCore::Protocol::RequestRockPaperScissors, &ServerConnectionP::decodeRockPaperScissorsReply),
        std::make_pair(QMdmmCore::Protocol::RequestActionOrder, &ServerConnectionP::decodeActionOrderReply),
        std::make_pair(QMdmmCore::Protocol::RequestAction, &ServerConnectionP::decodeActionReply),
        std::make_pair(QMdmmCore::Protocol::RequestUpgrade, &ServerConnectionP::decodeUpgradeReply),
    };
    return callbacks;
}

const QHash<QMdmmCore::Protocol::RequestId, void (ServerConnectionP::*)()> &ServerConnectionP::defaultReplyCallbacks()
{
    static const QHash<QMdmmCore::Protocol::RequestId, void (ServerConnectionP::*)()> callbacks {
        std::make_pair(QMdmmCore::Protocol::RequestRockPaperScissors, &ServerConnectionP::defaultReplyRockPaperScissors),
        std::make_pair(QMdmmCore::Protocol::RequestActionOrder, &ServerConnectionP::defaultReplyActionOrder),
        std::make_pair(QMdmmCore::Protocol::RequestAction, &ServerConnectionP::defaultReplyAction),
        std::make_pair(QMdmmCore::Protocol::RequestUpgrade, &ServerConnectionP::defaultReplyUpgrade),
    };
    return callbacks;
}

// Extra tolerance in seconds added on top of ServerConfiguration::requestTimeout for the
// request timer. The timer only backstops abnormal cases (D-020): a healthy client replies
// or gives up on its own; if it does neither within requestTimeout + grace, the server
// treats the timeout as a disconnect (see ServerConnectionP::requestTimeout). A
// requestTimeout of 0 means "no timeout" and leaves the timer unarmed entirely, so this
// grace only applies to non-zero values (feedback 86).
int ServerConnectionP::requestTimeoutGracePeriod = 60;

ServerConnectionP::ServerConnectionP(Agent *agent, const QMdmmCore::LogicConfiguration &logicConfiguration, int requestTimeout, QObject *parent)
    : QObject(parent)
    , agent(agent)
    , conf(logicConfiguration)
    , currentRequest(QMdmmCore::Protocol::RequestInvalid)
    , requestTimeoutEnabled(requestTimeout > 0)
    , requestTimer(new QTimer(this))
{
    // The interval only matters when the timer is armed at all: at 0 it is never started, so the
    // value set here goes unused.
    requestTimer->setInterval((requestTimeout + requestTimeoutGracePeriod) * 1000);
    requestTimer->setSingleShot(true);
    connect(requestTimer, &QTimer::timeout, this, &ServerConnectionP::requestTimeout);

    // Wire the Agent's notification signals to this connection's encode-and-send slots. The
    // Agent forwards a notifyXxx() call as the corresponding xxxNotified signal; this connection
    // turns the strong-typed notification back into a wire packet and sends it.
    connect(agent, &Agent::logicConfigurationNotified, this, &ServerConnectionP::sendLogicConfigurationNotified);
    connect(agent, &Agent::agentStateChangeNotified, this, &ServerConnectionP::sendAgentStateChangeNotified);
    connect(agent, &Agent::playerAddNotified, this, &ServerConnectionP::sendPlayerAddNotified);
    connect(agent, &Agent::playerRemoveNotified, this, &ServerConnectionP::sendPlayerRemoveNotified);
    connect(agent, &Agent::gameStartNotified, this, &ServerConnectionP::sendGameStartNotified);
    connect(agent, &Agent::roundStartNotified, this, &ServerConnectionP::sendRoundStartNotified);
    connect(agent, &Agent::rockPaperScissorsNotified, this, &ServerConnectionP::sendRockPaperScissorsNotified);
    connect(agent, &Agent::actionOrderNotified, this, &ServerConnectionP::sendActionOrderNotified);
    connect(agent, &Agent::actionNotified, this, &ServerConnectionP::sendActionNotified);
    connect(agent, &Agent::roundOverNotified, this, &ServerConnectionP::sendRoundOverNotified);
    connect(agent, &Agent::upgradeNotified, this, &ServerConnectionP::sendUpgradeNotified);
    connect(agent, &Agent::gameOverNotified, this, &ServerConnectionP::sendGameOverNotified);
    connect(agent, &Agent::speakNotified, this, &ServerConnectionP::sendSpeakNotified);
    connect(agent, &Agent::operateNotified, this, &ServerConnectionP::sendOperateNotified);

    // Wire the Agent's request signals to this connection's encode-and-send slots. The Agent
    // forwards a requestXxx() call as the corresponding xxxRequested signal; this connection turns
    // the strong-typed request back into a wire packet and sends it.
    connect(agent, &Agent::rockPaperScissorsRequested, this, &ServerConnectionP::sendRockPaperScissorsRequested);
    connect(agent, &Agent::actionOrderRequested, this, &ServerConnectionP::sendActionOrderRequested);
    connect(agent, &Agent::actionRequested, this, &ServerConnectionP::sendActionRequested);
    connect(agent, &Agent::upgradeRequested, this, &ServerConnectionP::sendUpgradeRequested);
}

ServerConnectionP::~ServerConnectionP() = default;

void ServerConnectionP::setSocket(Socket *_socket)
{
    if (socket != nullptr)
        socket->deleteLater();

    socket = _socket;
    if (socket != nullptr) {
        connect(socket, &Socket::packetReceived, this, &ServerConnectionP::packetReceived);
        // The socket drop is handled by onSocketDisconnected, which translates it into an
        // Agent event (see there). The room only ever deals with agents, never sockets.
        connect(socket, &Socket::socketDisconnected, this, &ServerConnectionP::onSocketDisconnected);
        connect(this, &ServerConnectionP::sendPacket, socket, &Socket::sendPacket);
    }
}

void ServerConnectionP::onSocketDisconnected()
{
    // Translate the socket drop into an Agent event (D-018: the socket is digested at the
    // server/wire layer, and the room only ever sees agents). Clean up the socket, mark the
    // agent offline, auto-reply any in-flight request so the logic keeps advancing, then emit
    // the disconnect as an agent event -- the room then decides whether to drop the agent
    // (not full) or preserve its seat for a reconnect (full).
    if (socket != nullptr) {
        socket->deleteLater();
        socket = nullptr;
    }

    QMdmmCore::Data::AgentState state = agent->state();
    state.setFlag(QMdmmCore::Data::StateMaskOnline, false).setFlag(QMdmmCore::Data::StateMaskTrust, false);
    agent->setState(state);

    // If there is an active request, answer it with the default reply so the logic is not left
    // waiting on the gone player. executeDefaultReply handles the no-active-request case as a
    // no-op (the not-full case, where the game has not started yet).
    executeDefaultReply();

    emit agentDisconnected(agent);
}

void ServerConnectionP::addRequest(QMdmmCore::Protocol::RequestId requestId, const QJsonValue &value)
{
    currentRequest = requestId;
    currentRequestValue = value;

    if (socket != nullptr) {
        emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::TypeRequest, requestId, value));
        // With no timeout configured the request stays open until the client answers; the logic
        // keeps waiting rather than treating silence as a disconnect (feedback 86).
        if (requestTimeoutEnabled)
            requestTimer->start();
    } else {
        // No socket: the request cannot go over the wire, so the connection answers it itself
        // with the default reply. This reply must be *asynchronous*, never synchronous -- the
        // request/reply flow is event-driven by design:
        // 1. addRequest is reached from a sendXxxRequested slot triggered by the Agent's
        //    xxxRequested signal (i.e. from inside LogicRunnerP's request handler). Doing the
        //    default reply synchronously there re-enters the request handler and stalls the
        //    event loop with the reply's work.
        // 2. The reply is designed to be delivered asynchronously; a synchronous reply would
        //    reach the logic side before the request handler returns, breaking the ordering.
        QTimer::singleShot(0, Qt::CoarseTimer, this, &ServerConnectionP::executeDefaultReply);
    }
}

void ServerConnectionP::decodeRockPaperScissorsReply(const QJsonValue &value)
{
    // A statically checkable invalid value (wrong JSON type, out-of-range enum, oversized array)
    // is an abnormal case (D-025): drop the connection *and* answer the in-flight request with its
    // default reply so the logic is not left waiting on a misbehaving client (D-030 decode-layer
    // full defense). socket->setError records the protocol error and disconnects the transport,
    // which walks socketDisconnected -> onSocketDisconnected -> agentDisconnected.
    //
    // The default reply must be emitted explicitly here (defaultReplyRockPaperScissors) and not
    // left to the disconnect path: packetReceived clears currentRequest *before* invoking this
    // decode callback, so the executeDefaultReply() reached via setError -> onSocketDisconnected
    // is a no-op. Do not let both fire (only reachable if the currentRequest clearing is
    // reordered), or the request would receive two default replies.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
#define PROTOCOLERROR                                  \
    do {                                               \
        socket->setError({Socket::ProtocolError, {}}); \
        defaultReplyRockPaperScissors();               \
        return;                                        \
    } while (0)

    if (!value.isDouble())
        PROTOCOLERROR;

    QMdmmCore::Data::RockPaperScissors rps = static_cast<QMdmmCore::Data::RockPaperScissors>(value.toInt());
    switch (rps) {
    case QMdmmCore::Data::Rock:
    case QMdmmCore::Data::Paper:
    case QMdmmCore::Data::Scissors:
        break;
    default:
        PROTOCOLERROR;
    }

    agent->rockPaperScissors(rps);

#undef PROTOCOLERROR
    // NOLINTEND(cppcoreguidelines-avoid-do-while)
}

void ServerConnectionP::decodeActionOrderReply(const QJsonValue &value)
{
    // Same implicit contract as decodeRockPaperScissorsReply: the disconnect path triggered by
    // setError runs executeDefaultReply as a no-op (currentRequest is already cleared), so the
    // default reply must be emitted explicitly here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
#define PROTOCOLERROR                                  \
    do {                                               \
        socket->setError({Socket::ProtocolError, {}}); \
        defaultReplyActionOrder();                     \
        return;                                        \
    } while (0)

    if (!value.isArray())
        PROTOCOLERROR;

    QJsonArray arr = value.toArray();

    // The reply carries at most one entry per requested selection (0 = yield that opportunity,
    // 1..maximumOrder = strive for an order). An oversized array is an abnormal case (D-025 /
    // D-030: statically checkable invalid value -> drop the connection).
    const int selectionNum = currentRequestValue.toObject().value(u"selectionNum"_s).toInt();
    if (arr.size() > selectionNum)
        PROTOCOLERROR;

    QList<int> ao;
    for (QJsonArray::const_iterator it = arr.constBegin(); it != arr.constEnd(); ++it) {
        if (!it->isDouble())
            PROTOCOLERROR;
        ao << it->toInt();
    }

    agent->actionOrder(ao);

#undef PROTOCOLERROR
    // NOLINTEND(cppcoreguidelines-avoid-do-while)
}

void ServerConnectionP::decodeActionReply(const QJsonValue &value)
{
    // Same implicit contract as decodeRockPaperScissorsReply: the disconnect path triggered by
    // setError runs executeDefaultReply as a no-op (currentRequest is already cleared), so the
    // default reply must be emitted explicitly here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
#define PROTOCOLERROR                                  \
    do {                                               \
        socket->setError({Socket::ProtocolError, {}}); \
        defaultReplyAction();                          \
        return;                                        \
    } while (0)

    if (!value.isObject())
        PROTOCOLERROR;

    QJsonObject arr = value.toObject();

    if (!arr.contains(u"action"_s))
        PROTOCOLERROR;
    QJsonValue vaction = arr.value(u"action"_s);
    if (!vaction.isDouble())
        PROTOCOLERROR;
    QMdmmCore::Data::Action act = static_cast<QMdmmCore::Data::Action>(vaction.toInt());
    switch (act) {
    case QMdmmCore::Data::DoNothing:
    case QMdmmCore::Data::BuyKnife:
    case QMdmmCore::Data::BuyHorse:
    case QMdmmCore::Data::Slash:
    case QMdmmCore::Data::Kick:
    case QMdmmCore::Data::Move:
    case QMdmmCore::Data::LetMove:
        break;
    default:
        PROTOCOLERROR;
    }

    QString toPlayer;
    switch (act) {
    case QMdmmCore::Data::Slash:
    case QMdmmCore::Data::Kick:
    case QMdmmCore::Data::LetMove: {
        if (!arr.contains(u"toPlayer"_s))
            PROTOCOLERROR;
        QJsonValue vtoPlayer = arr.value(u"toPlayer"_s);
        if (!vtoPlayer.isString())
            PROTOCOLERROR;
        toPlayer = vtoPlayer.toString();
        break;
    }
    default:
        break;
    }

    int toPlace = 0;
    switch (act) {
    case QMdmmCore::Data::Move:
    case QMdmmCore::Data::LetMove: {
        if (!arr.contains(u"toPlace"_s))
            PROTOCOLERROR;
        QJsonValue vtoPlace = arr.value(u"toPlace"_s);
        if (!vtoPlace.isDouble())
            PROTOCOLERROR;
        toPlace = vtoPlace.toInt();
        break;
    }
    default:
        break;
    }

    agent->action(act, toPlayer, toPlace);

#undef PROTOCOLERROR
    // NOLINTEND(cppcoreguidelines-avoid-do-while)
}

void ServerConnectionP::decodeUpgradeReply(const QJsonValue &value)
{
    // Same implicit contract as decodeRockPaperScissorsReply: the disconnect path triggered by
    // setError runs executeDefaultReply as a no-op (currentRequest is already cleared), so the
    // default reply must be emitted explicitly here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
#define PROTOCOLERROR                                  \
    do {                                               \
        socket->setError({Socket::ProtocolError, {}}); \
        defaultReplyUpgrade();                         \
        return;                                        \
    } while (0)

    if (!value.isArray())
        PROTOCOLERROR;

    QJsonArray arr = value.toArray();

    // The reply may upgrade at most once per remaining upgrade opportunity; an oversized array is
    // an abnormal case (D-025 / D-030: statically checkable invalid value -> drop the connection).
    const int remainingTimes = currentRequestValue.toInt(1);
    if (arr.size() > remainingTimes)
        PROTOCOLERROR;

    QList<QMdmmCore::Data::UpgradeItem> ups;
    for (QJsonArray::const_iterator it = arr.constBegin(); it != arr.constEnd(); ++it) {
        if (!it->isDouble())
            PROTOCOLERROR;
        QMdmmCore::Data::UpgradeItem up = static_cast<QMdmmCore::Data::UpgradeItem>(it->toInt());
        switch (up) {
        case QMdmmCore::Data::UpgradeKnife:
        case QMdmmCore::Data::UpgradeHorse:
        case QMdmmCore::Data::UpgradeMaxHp:
            break;
        default:
            PROTOCOLERROR;
        }
        ups << up;
    }

    agent->upgrade(ups);

#undef PROTOCOLERROR
    // NOLINTEND(cppcoreguidelines-avoid-do-while)
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ServerConnectionP::defaultReplyRockPaperScissors()
{
    agent->rockPaperScissors(static_cast<QMdmmCore::Data::RockPaperScissors>(QRandomGenerator::global()->generate() % 3));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ServerConnectionP::defaultReplyActionOrder()
{
    QJsonObject ob = currentRequestValue.toObject();
    QJsonArray arr = ob.value(u"remainedOrders"_s).toArray();
    int num = ob.value(u"selectionNum"_s).toInt();
    QList<int> ao;
    ao.reserve(num);
    while ((num--) != 0)
        ao.append(arr.takeAt(0).toInt());

    agent->actionOrder(ao);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ServerConnectionP::defaultReplyAction()
{
    agent->action(QMdmmCore::Data::DoNothing, {}, 0);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ServerConnectionP::defaultReplyUpgrade()
{
    // The default reply must not leave the upgrade phase stuck: any list that fails
    // Logic::upgradeFeasible is replaced by a feasible fallback (D-036) that still advances
    // the phase. The
    // connection cannot build a feasible list because it has no access to each stat's
    // remaining-upgrade count (that state lives in the Logic thread's Room, not in the wire
    // layer). It therefore deliberately sends an empty list -- an infeasible reply -- and lets
    // Logic's fallback take over: upgradeReply auto-spends the absent (disconnected / timed
    // out) player's upgrade point for them, knife damage first (see Logic::upgradeReply). The
    // old code filled every point into UpgradeMaxHp, which upgradeFeasible rejects once that
    // stat is maxed out (reachable in later rounds), permanently stalling the upgrade phase.
    agent->upgrade({});
}

void ServerConnectionP::packetReceived(const QMdmmCore::Packet &packet)
{
    if (socket == nullptr)
        return;

    if (packet.type() == QMdmmCore::Protocol::TypeNotify) {
        // A notify addressed to the agent (speak / operate) is decoded and handed to the Agent.
        if ((packet.notifyId() & QMdmmCore::Protocol::NotifyToAgentMask) != 0) {
            void (ServerConnectionP::*call)(const QJsonValue &) = notifyCallbacks().value(packet.notifyId(), nullptr);
            if (call != nullptr)
                (this->*call)(packet.value());
            else
                socket->setError({.code = Socket::ProtocolError, .errorString = {}});
            return;
        }

        // A notify addressed to the server (ping / sign-in) is handled by ServerP, which is also
        // connected to this socket's packetReceived; this connection leaves it alone.
        if ((packet.notifyId() & QMdmmCore::Protocol::NotifyToServerMask) != 0)
            return;

        // Anything else is an abnormal notify (a server/agent-bound notify echoed back, or an
        // invalid notify id): drop the connection and answer any in-flight request with its
        // default reply so the logic is not left waiting on a misbehaving client (D-025).
        socket->setError({.code = Socket::ProtocolError, .errorString = {}});
        executeDefaultReply();
        return;
    }

    if (packet.type() == QMdmmCore::Protocol::TypeReply) {
        if (currentRequest == packet.requestId()) {
            requestTimer->stop();
            // A null reply value is the protocol's "give up" marker (every legal reply value is
            // non-null), so it means the client has no answer and the default reply applies. Note
            // executeDefaultReply reads currentRequest before clearing it, so it must be called
            // *before* resetting currentRequest below.
            if (packet.value().isNull()) {
                executeDefaultReply();
            } else {
                currentRequest = QMdmmCore::Protocol::RequestInvalid;
                void (ServerConnectionP::*call)(const QJsonValue &) = replyCallbacks().value(packet.requestId(), nullptr);
                if (call != nullptr)
                    (this->*call)(packet.value());
                else
                    socket->setError({.code = Socket::ProtocolError, .errorString = {}});
            }
            return;
        }

        // A reply that does not match the in-flight request is an ordering mismatch: drop the
        // connection and answer the in-flight request with its default reply (D-025).
        socket->setError({.code = Socket::ProtocolError, .errorString = {}});
        executeDefaultReply();
        return;
    }

    // A request or an invalid/unknown packet type from the client is abnormal: requests only
    // originate from the Logic. Drop the connection and answer any in-flight request (D-025).
    socket->setError({.code = Socket::ProtocolError, .errorString = {}});
    executeDefaultReply();
}

void ServerConnectionP::sendRockPaperScissorsRequested(const QStringList &playerNames, int strivedOrder)
{
    QJsonObject ob;
    ob.insert(u"playerNames"_s, QJsonArray::fromStringList(playerNames));
    ob.insert(u"strivedOrder"_s, strivedOrder);
    addRequest(QMdmmCore::Protocol::RequestRockPaperScissors, ob);
}

void ServerConnectionP::sendActionOrderRequested(const QList<int> &remainedOrders, int maximumOrder, int selectionNum)
{
    QJsonObject ob;
    QJsonArray arr;
    foreach (int remainedOrder, remainedOrders)
        arr.append(remainedOrder);
    ob.insert(u"remainedOrders"_s, arr);
    ob.insert(u"maximumOrder"_s, maximumOrder);
    ob.insert(u"selectionNum"_s, selectionNum);
    addRequest(QMdmmCore::Protocol::RequestActionOrder, ob);
}

void ServerConnectionP::sendActionRequested(int currentOrder)
{
    addRequest(QMdmmCore::Protocol::RequestAction, currentOrder);
}

void ServerConnectionP::sendUpgradeRequested(int remainingTimes)
{
    addRequest(QMdmmCore::Protocol::RequestUpgrade, remainingTimes);
}

void ServerConnectionP::sendLogicConfigurationNotified()
{
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyLogicConfiguration, conf));
}

void ServerConnectionP::sendAgentStateChangeNotified(const QString &playerName, const QMdmmCore::Data::AgentState &agentState)
{
    QJsonObject ob;
    ob.insert(u"playerName"_s, playerName);
    ob.insert(u"agentState"_s, static_cast<int>(agentState.toInt()));
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyAgentStateChanged, ob));
}

void ServerConnectionP::sendPlayerAddNotified(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState)
{
    QJsonObject ob;
    ob.insert(u"playerName"_s, playerName);
    ob.insert(u"screenName"_s, screenName);
    ob.insert(u"agentState"_s, static_cast<int>(agentState.toInt()));
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyPlayerAdded, ob));
}

void ServerConnectionP::sendPlayerRemoveNotified(const QString &playerName)
{
    QJsonObject ob;
    ob.insert(u"playerName"_s, playerName);
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyPlayerRemoved, ob));
}

void ServerConnectionP::sendGameStartNotified()
{
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyGameStart, {}));
}

void ServerConnectionP::sendRoundStartNotified()
{
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyRoundStart, {}));
}

void ServerConnectionP::sendRockPaperScissorsNotified(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies)
{
    QJsonObject ob;
    for (QHash<QString, QMdmmCore::Data::RockPaperScissors>::const_iterator it = replies.constBegin(); it != replies.constEnd(); ++it)
        ob.insert(it.key(), static_cast<int>(it.value()));
    QMdmmCore::Packet packet(QMdmmCore::Protocol::NotifyRockPaperScissors, ob);
    roundEventLog.append(packet);
    emit sendPacket(packet);
}

void ServerConnectionP::sendActionOrderNotified(const QStringList &result)
{
    QJsonArray arr;
    for (const QString &playerName : result)
        arr.append(playerName);
    QMdmmCore::Packet packet(QMdmmCore::Protocol::NotifyActionOrder, arr);
    roundEventLog.append(packet);
    emit sendPacket(packet);
}

void ServerConnectionP::sendActionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace)
{
    QJsonObject ob;
    ob.insert(u"playerName"_s, playerName);
    ob.insert(u"action"_s, static_cast<int>(action));

    switch (action) {
    case QMdmmCore::Data::Slash:
    case QMdmmCore::Data::Kick:
    case QMdmmCore::Data::LetMove:
        ob.insert(u"toPlayer"_s, toPlayer);
        break;
    default:
        break;
    }

    switch (action) {
    case QMdmmCore::Data::Move:
    case QMdmmCore::Data::LetMove:
        ob.insert(u"toPlace"_s, toPlace);
        break;
    default:
        break;
    }

    QMdmmCore::Packet packet(QMdmmCore::Protocol::NotifyAction, ob);
    roundEventLog.append(packet);
    emit sendPacket(packet);
}

void ServerConnectionP::sendRoundOverNotified()
{
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyRoundOver, {}));
}

void ServerConnectionP::sendUpgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades)
{
    QJsonObject ob;
    for (QHash<QString, QList<QMdmmCore::Data::UpgradeItem>>::const_iterator it = upgrades.constBegin(); it != upgrades.constEnd(); ++it) {
        QJsonArray arr;
        foreach (QMdmmCore::Data::UpgradeItem up, it.value())
            arr.append(static_cast<int>(up));
        ob.insert(it.key(), arr);
    }
    QMdmmCore::Packet packet(QMdmmCore::Protocol::NotifyUpgrade, ob);
    roundEventLog.append(packet);
    emit sendPacket(packet);
}

void ServerConnectionP::clearRoundEventLog()
{
    roundEventLog.clear();
}

void ServerConnectionP::replayMissedRoundEvents(int lastRoundEventSeq)
{
    // roundEventLog is ordered by send order, and its index IS the round-event sequence number
    // (see roundEventLog in qmdmmlogicrunner_p.h), which equals the client's received-event
    // counter. Replaying in-place needs no sorting: skip the first `lastRoundEventSeq` events the
    // client already got and re-send the rest, in order.
    for (int i = lastRoundEventSeq; i < roundEventLog.size(); ++i)
        emit sendPacket(roundEventLog.at(i));
}

void ServerConnectionP::reconnect(Socket *socket, int lastRoundEventSeq)
{
    // Rebind the socket. setSocket is safe because onSocketDisconnected already set the old
    // socket to nullptr before this point, so nothing gets double-deleted.
    setSocket(socket);

    // Precise catch-up: replay the round events the client missed. The client reported how many
    // round events it received before the drop (lastRoundEventSeq); every event this connection
    // broadcast while the client was gone is replayed in send order, skipping the first
    // `lastRoundEventSeq` (already received). See replayMissedRoundEvents.
    replayMissedRoundEvents(lastRoundEventSeq);
}

void ServerConnectionP::sendGameOverNotified(const QStringList &playerNames)
{
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyGameOver, QJsonArray::fromStringList(playerNames)));
}

void ServerConnectionP::sendSpeakNotified(const QString &playerName, const QString &content)
{
    QJsonObject ob;
    ob.insert(u"playerName"_s, playerName);
    ob.insert(u"content"_s, content);
    emit sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifySpoken, ob));
}

void ServerConnectionP::sendOperateNotified(const QString &playerName, const QJsonValue &todo)
{
    Q_UNIMPLEMENTED();
    Q_UNUSED(playerName);
    Q_UNUSED(todo);
}

void ServerConnectionP::requestTimeout()
{
    // A silent client is gone: setError() disconnects the socket, which walks the
    // disconnect path (onSocketDisconnected marks the agent offline and lets the room preserve
    // the seat or drop the agent). The default reply is issued right away so the logic is not
    // left waiting on the gone player.
    if (socket != nullptr)
        socket->setError({.code = Socket::ProtocolError, .errorString = {}});
    executeDefaultReply();
}

void ServerConnectionP::executeDefaultReply()
{
    if (currentRequest != QMdmmCore::Protocol::RequestInvalid) {
        void (ServerConnectionP::*call)() = defaultReplyCallbacks().value(currentRequest, nullptr);
        currentRequest = QMdmmCore::Protocol::RequestInvalid;
        if (call != nullptr)
            (this->*call)();
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ServerConnectionP::receiveSpeak(const QJsonValue &value)
{
    // The value is the Base64-encoded content sent by Client::notifySpeak. The server forwards it
    // verbatim (it does not decode); the receiving client decodes it in ClientP::notifySpoken.
    agent->speak(value.toString());
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ServerConnectionP::receiveOperate(const QJsonValue &value)
{
    agent->operate(value);
}
} // namespace p
} // namespace QMdmmNetworking

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmclient_p.h"
#include "qmdmmclient.h"

#include <QMdmmLogicConfiguration>
#include <QMdmmPlayer>

#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QMdmmNetworking {
namespace p {

namespace {
// Automatic reconnect policy: exponential backoff (500ms, 1s, 2s, 4s, 8s) with a
// finite number of attempts. After the last attempt fails the client stays
// disconnected and the upper layer must call connectToHost again.
constexpr int ReconnectBaseIntervalMs = 500;
constexpr int ReconnectMaxIntervalMs = 8000;
constexpr int MaxReconnectAttempts = 5;
} // namespace

const QHash<QMdmmCore::Protocol::RequestId, void (ClientP::*)(const QJsonValue &)> &ClientP::requestCallbacks()
{
    static const QHash<QMdmmCore::Protocol::RequestId, void (ClientP::*)(const QJsonValue &)> callbacks {
        std::make_pair(QMdmmCore::Protocol::RequestRockPaperScissors, &ClientP::requestRockPaperScissors),
        std::make_pair(QMdmmCore::Protocol::RequestActionOrder, &ClientP::requestActionOrder),
        std::make_pair(QMdmmCore::Protocol::RequestAction, &ClientP::requestAction),
        std::make_pair(QMdmmCore::Protocol::RequestUpgrade, &ClientP::requestUpgrade),
    };
    return callbacks;
}

const QHash<QMdmmCore::Protocol::NotifyId, void (ClientP::*)(const QJsonValue &)> &ClientP::notifyCallbacks()
{
    static const QHash<QMdmmCore::Protocol::NotifyId, void (ClientP::*)(const QJsonValue &)> callbacks {
        // from Server
        std::make_pair(QMdmmCore::Protocol::NotifyPongServer, &ClientP::notifyPongServer),
        std::make_pair(QMdmmCore::Protocol::NotifyVersion, &ClientP::notifyVersion),

        // from Agent
        std::make_pair(QMdmmCore::Protocol::NotifyLogicConfiguration, &ClientP::notifyLogicConfiguration),
        std::make_pair(QMdmmCore::Protocol::NotifyAgentStateChanged, &ClientP::notifyAgentStateChanged),
        std::make_pair(QMdmmCore::Protocol::NotifyPlayerAdded, &ClientP::notifyPlayerAdded),
        std::make_pair(QMdmmCore::Protocol::NotifyPlayerRemoved, &ClientP::notifyPlayerRemoved),
        std::make_pair(QMdmmCore::Protocol::NotifyGameStart, &ClientP::notifyGameStart),
        std::make_pair(QMdmmCore::Protocol::NotifyRoundStart, &ClientP::notifyRoundStart),
        std::make_pair(QMdmmCore::Protocol::NotifyRockPaperScissors, &ClientP::notifyRockPaperScissors),
        std::make_pair(QMdmmCore::Protocol::NotifyActionOrder, &ClientP::notifyActionOrder),
        std::make_pair(QMdmmCore::Protocol::NotifyAction, &ClientP::notifyAction),
        std::make_pair(QMdmmCore::Protocol::NotifyRoundOver, &ClientP::notifyRoundOver),
        std::make_pair(QMdmmCore::Protocol::NotifyUpgrade, &ClientP::notifyUpgrade),
        std::make_pair(QMdmmCore::Protocol::NotifyGameOver, &ClientP::notifyGameOver),
        std::make_pair(QMdmmCore::Protocol::NotifySpoken, &ClientP::notifySpoken),
        std::make_pair(QMdmmCore::Protocol::NotifyOperated, &ClientP::notifyOperated),
    };
    return callbacks;
}

ClientP::ClientP(ClientConfiguration clientConfiguration, Client *q)
    : QObject(q)
    , q(q)
    , clientConfiguration(std::move(clientConfiguration))
    , socket(nullptr)
    , room(new QMdmmCore::Room(QMdmmCore::LogicConfiguration(), this))
    , heartbeatTimer(new QTimer(this))
    , reconnectTimer(new QTimer(this))
    , reconnectAttempts(0)
    , reconnectInProgress(false)
    , connected(false)
    , currentRequest(QMdmmCore::Protocol::RequestInvalid)
    , initialState(QMdmmCore::Data::StateOffline)
{
    heartbeatTimer->setInterval(30000);
    heartbeatTimer->setSingleShot(false);
    connect(heartbeatTimer, &QTimer::timeout, this, &ClientP::heartbeatTimeout);

    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, &ClientP::reconnectTimeout);
}

void ClientP::initSelfAgent()
{
    // The client pre-creates its own Agent so the operation side (GUI / Bot) always has an
    // Agent to drive, symmetric to the server side where the operation side creates the agent
    // and hands it to LogicRunner. Its name is the client's objectName, which is also what
    // signIn reports as playerName, so the server's notifyPlayerAdded for ourselves updates this
    // Agent in place.
    Agent *self = new Agent(q->objectName(), this);
    self->setScreenName(clientConfiguration.screenName());
    agents.insert(q->objectName(), self);
    selfAgent = self;

    // Wire the client's own agent's reply / speech / operation signals to the encode-and-send
    // slots below. The operation side drives the Agent's bare-verb methods (rockPaperScissors /
    // actionOrder / action / upgrade) and speak / operate / setManaged, which forward as the
    // replyXxx / spoken / operated / managedChanged signals; this client turns them back into wire
    // packets. Mirrors the server side, where ServerConnectionP wires the Agent's xxxRequested /
    // xxxNotified signals.
    connect(self, &Agent::replyRockPaperScissors, this, &ClientP::sendRockPaperScissorsReply);
    connect(self, &Agent::replyActionOrder, this, &ClientP::sendActionOrderReply);
    connect(self, &Agent::replyAction, this, &ClientP::sendActionReply);
    connect(self, &Agent::replyUpgrade, this, &ClientP::sendUpgradeReply);
    connect(self, &Agent::spoken, this, &ClientP::sendSpeak);
    connect(self, &Agent::operated, this, &ClientP::sendOperate);
    connect(self, &Agent::requestGivenUp, this, &ClientP::sendRequestGivenUp);
    connect(self, &Agent::managedChanged, this, &ClientP::sendManagedChanged);
}

// Qt documentation only mentioned "auto" here
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ONERRPRINTJSON(value)                                                     \
    auto onRet_ [[maybe_unused]] = qScopeGuard([this, value, func = __func__]() { \
        if (socket != nullptr)                                                    \
            socket->setError({Socket::ProtocolError, {}});                        \
        QByteArray json(QJsonDocument({value}).toJson(QJsonDocument::Indented));  \
        qDebug("%s fails with Json value: %s", func, json.constData());           \
    });

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::requestRockPaperScissors(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    if (!ob.contains(u"playerNames"_s))
        return;
    QJsonValue vplayerNames = ob.value(u"playerNames"_s);
    if (!vplayerNames.isArray())
        return;
    QJsonArray vaplayerNames = vplayerNames.toArray();
    QStringList playerNames;
    for (const QJsonValueRef &vplayerName : vaplayerNames) {
        if (!vplayerName.isString())
            return;
        playerNames << vplayerName.toString();
    }

    if (!ob.contains(u"strivedOrder"_s))
        return;
    QJsonValue vstrivedOrder = ob.value(u"strivedOrder"_s);
    if (!vstrivedOrder.isDouble())
        return;
    int strivedOrder = vstrivedOrder.toInt();

    selfAgent->requestRockPaperScissors(playerNames, strivedOrder);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::requestActionOrder(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    if (!ob.contains(u"remainedOrders"_s))
        return;
    QJsonValue vremainedOrders = ob.value(u"remainedOrders"_s);
    if (!vremainedOrders.isArray())
        return;
    QJsonArray varemainedOrders = vremainedOrders.toArray();
    QList<int> remainedOrders;
    for (const QJsonValueRef &vremainedOrder : varemainedOrders) {
        if (!vremainedOrder.isDouble())
            return;
        remainedOrders << vremainedOrder.toInt();
    }

    if (!ob.contains(u"maximumOrder"_s))
        return;
    QJsonValue vmaximumOrder = ob.value(u"maximumOrder"_s);
    if (!vmaximumOrder.isDouble())
        return;
    int maximumOrder = vmaximumOrder.toInt();

    if (!ob.contains(u"selectionNum"_s))
        return;
    QJsonValue vselectionNum = ob.value(u"selectionNum"_s);
    if (!vselectionNum.isDouble())
        return;
    int selectionNum = vselectionNum.toInt();

    selfAgent->requestActionOrder(remainedOrders, maximumOrder, selectionNum);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::requestAction(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isDouble())
        return;
    int currentOrder = value.toInt();

    selfAgent->requestAction(currentOrder);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::requestUpgrade(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isDouble())
        return;
    int remainedTimes = value.toInt();

    selfAgent->requestUpgrade(remainedTimes);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyPongServer(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    bool ok = false;
    int64_t pongTime = value.toVariant().toLongLong(&ok);
    if (!ok) {
        socket->setError({.code = Socket::ProtocolError, .errorString = {}});
        return;
    }

    int64_t currentTime = QDateTime::currentMSecsSinceEpoch();
    int64_t elapsed = currentTime - pongTime;

    // broadcast? or recorded internally?
    Q_UNUSED(elapsed);

    onRet_.dismiss();
}

void ClientP::notifyVersion(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    if (!ob.contains(u"versionNumber"_s))
        return;
    QJsonValue vversionNumber = ob.value(u"versionNumber"_s);
    if (!vversionNumber.isString())
        return;
    QString versionNumber = vversionNumber.toString();

    if (!ob.contains(u"protocolVersion"_s))
        return;
    QJsonValue vprotocolVersion = ob.value(u"protocolVersion"_s);
    if (!vprotocolVersion.isDouble())
        return;
    int protocolVersion = vprotocolVersion.toInt();

    if (protocolVersion != QMdmmCore::Protocol::version()) {
        // The wire protocol is incompatible: this is not a transient connection failure, so
        // disconnect cleanly instead of falling into the auto-reconnect loop (which would hit
        // the same mismatch again forever) or silently aborting the sign-in and leaving the
        // client in a connected-but-never-signed-in state.
        onRet_.dismiss();
        q->disconnectFromHost();
        return;
    }

    if (QVersionNumber::fromString(versionNumber) != QMdmmCore::Global::version()) {
        // The client version differs, but the wire protocol is compatible, so the handshake can
        // proceed; the mismatch is tolerated (no-op) for now.
    }

    // sign in process
    QJsonObject signInOb;
    signInOb.insert(u"playerName"_s, q->objectName());
    signInOb.insert(u"screenName"_s, clientConfiguration.screenName());
    signInOb.insert(u"agentState"_s, static_cast<int>(initialState));
    // Report how many round events this client received before a drop, so the server can replay
    // only the events it missed (precise catch-up). On a fresh sign-in this is 0 and the server's
    // empty round-event log means nothing extra is replayed.
    signInOb.insert(u"lastRoundEventSeq"_s, lastRoundEventSeq);
    emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifySignIn, signInOb));

    // The connection is back and we re-signed in. Stop the retry loop and tell
    // the upper layer the client is back online.
    if (reconnectInProgress) {
        reconnectInProgress = false;
        reconnectTimer->stop();
        emit q->socketReconnectSucceeded(Client::QPrivateSignal());
    }

    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyLogicConfiguration(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    QMdmmCore::LogicConfiguration conf;
    if (!conf.deserialize(value))
        return;

    room->setLogicConfiguration(conf);
    // Route it out to the operation side like every other notification: the mirror room now
    // holds the rules, but reading them off the room is not the same as learning that they are
    // there. The signal carries no payload by design -- the rules are read from the mirror.
    selfAgent->notifyLogicConfiguration();
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyAgentStateChanged(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;

    QJsonObject ob = value.toObject();
    if (!ob.contains(u"playerName"_s))
        return;
    if (!ob.contains(u"agentState"_s))
        return;

    QJsonValue vplayerName = ob.value(u"playerName"_s);
    if (!vplayerName.isString())
        return;
    QString playerName = vplayerName.toString();

    if (!agents.contains(playerName))
        return;
    Agent *agent = agents.value(playerName);

    QJsonValue vagentState = ob.value(u"agentState"_s);
    if (!vagentState.isDouble())
        return;
    QMdmmCore::Data::AgentState agentState = QMdmmCore::Data::AgentState(static_cast<QMdmmCore::Data::AgentState::Int>(vagentState.toInt()));

    // Update the mirror agent's state (the "data" half), then route the change to selfAgent so
    // the operation side (GUI) observes it via agentStateChangeNotified -- symmetric to how
    // notifyPlayerAdd / notifyPlayerRemove / notifyAction / notifySpeak route to selfAgent, and
    // to the server's agentStateChanged broadcast.
    agent->setState(agentState);
    selfAgent->notifyAgentStateChange(playerName, agentState);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyPlayerAdded(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;

    QJsonObject ob = value.toObject();
    if (!ob.contains(u"playerName"_s))
        return;
    if (!ob.contains(u"screenName"_s))
        return;
    if (!ob.contains(u"agentState"_s))
        return;

    QJsonValue vplayerName = ob.value(u"playerName"_s);
    if (!vplayerName.isString())
        return;
    QString playerName = vplayerName.toString();

    QJsonValue vscreenName = ob.value(u"screenName"_s);
    if (!vscreenName.isString())
        return;
    QString screenName = vscreenName.toString();

    QJsonValue vagentState = ob.value(u"agentState"_s);
    if (!vagentState.isDouble())
        return;
    QMdmmCore::Data::AgentState agentState = QMdmmCore::Data::AgentState(static_cast<QMdmmCore::Data::AgentState::Int>(vagentState.toInt()));

    // The client pre-creates its own Agent on construction (keyed by the client's objectName).
    // When the server reports ourselves back (sign-in confirm / state snapshot), update that
    // Agent in place and add ourselves to the room mirror, instead of treating it as a duplicate.
    if (Agent *agent = agents.value(playerName, nullptr); agent != nullptr) {
        if (playerName == q->objectName()) {
            room->addPlayer(playerName);
            agent->setScreenName(screenName);
            agent->setState(agentState);
            selfAgent->notifyPlayerAdd(playerName, screenName, agentState);
        }
        // A duplicate notifyPlayerAdded for another already-tracked player (a reconnect state
        // snapshot) is benign and must not be treated as a protocol error.
        onRet_.dismiss();
        return;
    }

    if (room->addPlayer(playerName) == nullptr)
        return;

    Agent *agent = new Agent(playerName, this);
    agent->setScreenName(screenName);
    agent->setState(agentState);
    agents.insert(playerName, agent);

    selfAgent->notifyPlayerAdd(playerName, screenName, agentState);
    onRet_.dismiss();
}

void ClientP::notifyPlayerRemoved(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    if (!ob.contains(u"playerName"_s))
        return;
    QJsonValue vplayerName = ob.value(u"playerName"_s);
    if (!vplayerName.isString())
        return;
    QString playerName = vplayerName.toString();
    if (!agents.contains(playerName))
        return;

    if (!room->removePlayer(playerName))
        return;

    selfAgent->notifyPlayerRemove(playerName);

    Agent *agent = agents.take(playerName);
    delete agent;

    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyGameStart(const QJsonValue &value [[maybe_unused]])
{
    selfAgent->notifyGameStart();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyRoundStart(const QJsonValue &value [[maybe_unused]])
{
    // Mirror the server: reset each player's per-round state (place, hp, items)
    // so the client's local room model stays consistent with the authority.
    // Without this, players would stay at the default Village place and every
    // place-dependent action (BuyKnife/Move/Slash/...) would fail locally.
    room->prepareForRoundStart();
    // A new round begins: reset the round-event counter.
    lastRoundEventSeq = 0;
    selfAgent->notifyRoundStart();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyRockPaperScissors(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    QHash<QString, QMdmmCore::Data::RockPaperScissors> replies;
    for (QJsonObject::const_iterator it = ob.constBegin(); it != ob.constEnd(); ++it) {
        QString playerName = it.key();
        if (!agents.contains(playerName))
            return;
        QJsonValue vrps = it.value();
        if (!vrps.isDouble())
            return;
        QMdmmCore::Data::RockPaperScissors rps = static_cast<QMdmmCore::Data::RockPaperScissors>(vrps.toInt());
        switch (rps) {
        case QMdmmCore::Data::Rock:
        case QMdmmCore::Data::Paper:
        case QMdmmCore::Data::Scissors:
            break;
        default:
            return;
        }
        replies.insert(playerName, rps);
    }

    ++lastRoundEventSeq;
    selfAgent->notifyRockPaperScissors(replies);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyActionOrder(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isArray())
        return;
    QJsonArray arr = value.toArray();

    QStringList result;
    for (const QJsonValueRef &val : arr) {
        if (!val.isString())
            return;
        QString playerName = val.toString();
        if (!agents.contains(playerName))
            return;
        result << playerName;
    }

    ++lastRoundEventSeq;
    selfAgent->notifyActionOrder(result);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const,readability-function-cognitive-complexity)
void ClientP::notifyAction(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    if (!ob.contains(u"playerName"_s))
        return;
    QJsonValue vplayerName = ob.value(u"playerName"_s);
    if (!vplayerName.isString())
        return;
    QString playerName = vplayerName.toString();
    if (!agents.contains(playerName))
        return;

    if (!ob.contains(u"action"_s))
        return;
    QJsonValue vaction = ob.value(u"action"_s);
    if (!vaction.isDouble())
        return;
    QMdmmCore::Data::Action action = static_cast<QMdmmCore::Data::Action>(vaction.toInt());
    switch (action) {
    case QMdmmCore::Data::DoNothing:
    case QMdmmCore::Data::BuyKnife:
    case QMdmmCore::Data::BuyHorse:
    case QMdmmCore::Data::Slash:
    case QMdmmCore::Data::Kick:
    case QMdmmCore::Data::Move:
    case QMdmmCore::Data::LetMove:
        break;
    default:
        return;
    }

    QString toPlayer;
    int toPlace = -1;

    switch (action) {
    case QMdmmCore::Data::Slash:
    case QMdmmCore::Data::Kick:
    case QMdmmCore::Data::LetMove: {
        if (!ob.contains(u"toPlayer"_s))
            return;
        QJsonValue vtoPlayer = ob.value(u"toPlayer"_s);
        if (!vtoPlayer.isString())
            return;
        toPlayer = vtoPlayer.toString();
        if (!agents.contains(toPlayer))
            return;

        break;
    }
    default:
        break;
    }

    switch (action) {
    case QMdmmCore::Data::Move:
    case QMdmmCore::Data::LetMove: {
        if (!ob.contains(u"toPlace"_s))
            return;
        QJsonValue vtoPlace = ob.value(u"toPlace"_s);
        if (!vtoPlace.isDouble())
            return;
        toPlace = vtoPlace.toInt();
        if ((toPlace < 0) || (toPlace > agents.count()))
            return;

        break;
    }
    default:
        break;
    }

    ++lastRoundEventSeq;
    selfAgent->notifyAction(playerName, action, toPlayer, toPlace);

    // This replyed action should always be success, since it is judged in Server
    // So if it fails, forcefully set the client as error, so it can disconnect the socket
    bool success = applyAction(playerName, action, toPlayer, toPlace);
    if (success)
        onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyRoundOver(const QJsonValue &value [[maybe_unused]])
{
    selfAgent->notifyRoundOver();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifyUpgrade(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> replies;
    for (QJsonObject::const_iterator it = ob.constBegin(); it != ob.constEnd(); ++it) {
        QString playerName = it.key();
        if (!agents.contains(playerName))
            return;
        QJsonValue vupgrades = it.value();
        if (!vupgrades.isArray())
            return;
        QJsonArray vaupgrades = vupgrades.toArray();
        QList<QMdmmCore::Data::UpgradeItem> upgrades;
        for (const QJsonValueRef &vupgrade : vaupgrades) {
            if (!vupgrade.isDouble())
                return;
            QMdmmCore::Data::UpgradeItem upgrade = static_cast<QMdmmCore::Data::UpgradeItem>(vupgrade.toInt());
            switch (upgrade) {
            case QMdmmCore::Data::UpgradeKnife:
            case QMdmmCore::Data::UpgradeHorse:
            case QMdmmCore::Data::UpgradeMaxHp:
                break;
            default:
                return;
            }
            upgrades << upgrade;
        }
        replies.insert(playerName, upgrades);
    }

    ++lastRoundEventSeq;
    selfAgent->notifyUpgrade(replies);

    // This replyed upgrade should always be success, since it is judged in Server
    // So if it fails, forcefully set the client as error, so it can disconnect the socket
    bool success = applyUpgrade(replies);
    if (success)
        onRet_.dismiss();
}

void ClientP::notifyGameOver(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    // Game is over, all other requests / replies are not processed anymore.
    // No longer receives anything from server and disconnects socket here seems reasonable.
    // All other things (agents, room, players inside room) can be cleaned up during Client instance destruction

    if (socket != nullptr) {
        heartbeatTimer->stop();
        connected = false;
        disconnect(socket);
        socket->disconnect(this);

        socket->deleteLater();
    }

    if (!value.isArray())
        return;
    QJsonArray arr = value.toArray();

    QStringList winners;
    for (const QJsonValueRef &vwinner : arr) {
        if (!vwinner.isString())
            return;
        QString winner = vwinner.toString();
        // Only trust winners we actually know about; ignore unknown names.
        if (!agents.contains(winner))
            continue;
        winners << winner;
    }

    selfAgent->notifyGameOver(winners);
    onRet_.dismiss();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::notifySpoken(const QJsonValue &value)
{
    ONERRPRINTJSON(value);

    if (!value.isObject())
        return;
    QJsonObject ob = value.toObject();

    if (!ob.contains(u"playerName"_s))
        return;
    QJsonValue vplayerName = ob.value(u"playerName"_s);
    if (!vplayerName.isString())
        return;
    QString playerName = vplayerName.toString();
    if (!agents.contains(playerName))
        return;

    if (!ob.contains(u"content"_s))
        return;
    QJsonValue vContent = ob.value(u"content"_s);
    if (!vContent.isString())
        return;
    QString content = QString::fromUtf8(QByteArray::fromBase64(vContent.toString().toLatin1()));

    selfAgent->notifySpeak(playerName, content);
    onRet_.dismiss();
}

void ClientP::notifyOperated(const QJsonValue &value)
{
    ONERRPRINTJSON(value);
    onRet_.dismiss();
    Q_UNIMPLEMENTED();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
bool ClientP::applyAction(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace)
{
    QMdmmCore::Player *from = room->player(playerName);
    switch (action) {
    case QMdmmCore::Data::DoNothing: {
        return from->doNothing();
    }
    case QMdmmCore::Data::BuyKnife: {
        return from->buyKnife();
    }
    case QMdmmCore::Data::BuyHorse: {
        return from->buyHorse();
    }
    case QMdmmCore::Data::Slash: {
        QMdmmCore::Player *to = room->player(toPlayer);
        return from->slash(to);
    }
    case QMdmmCore::Data::Kick: {
        QMdmmCore::Player *to = room->player(toPlayer);
        return from->kick(to);
    }
    case QMdmmCore::Data::Move: {
        return from->move(toPlace);
    }
    case QMdmmCore::Data::LetMove: {
        QMdmmCore::Player *to = room->player(toPlayer);
        return from->letMove(to, toPlace);
    }
    default:
        break;
    }

    return false;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
bool ClientP::applyUpgrade(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades)
{
    bool ret = true;

    for (QHash<QString, QList<QMdmmCore::Data::UpgradeItem>>::const_iterator it = upgrades.constBegin(); it != upgrades.constEnd(); ++it) {
        QMdmmCore::Player *up = room->player(it.key());
        const QList<QMdmmCore::Data::UpgradeItem> &items = it.value();
        foreach (QMdmmCore::Data::UpgradeItem item, items) {
            bool success = false;
            switch (item) {
            case QMdmmCore::Data::UpgradeKnife:
                success = up->upgradeKnife();
                break;
            case QMdmmCore::Data::UpgradeHorse:
                success = up->upgradeHorse();
                break;
            case QMdmmCore::Data::UpgradeMaxHp:
                success = up->upgradeMaxHp();
                break;
            default:
                break;
            }
            ret = ret && success;
        }
    }

    return ret;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendRockPaperScissorsReply(QMdmmCore::Data::RockPaperScissors rps)
{
    if (socket != nullptr && currentRequest == QMdmmCore::Protocol::RequestRockPaperScissors) {
        currentRequest = QMdmmCore::Protocol::RequestInvalid;
        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestRockPaperScissors, static_cast<int>(rps)));
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendActionOrderReply(const QList<int> &order)
{
    if (socket != nullptr && currentRequest == QMdmmCore::Protocol::RequestActionOrder) {
        currentRequest = QMdmmCore::Protocol::RequestInvalid;
        QJsonArray arr;
        foreach (int a, order)
            arr.append(a);

        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestActionOrder, arr));
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendActionReply(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace)
{
    if (socket != nullptr && currentRequest == QMdmmCore::Protocol::RequestAction) {
        currentRequest = QMdmmCore::Protocol::RequestInvalid;
        QJsonObject ob;
        ob.insert(u"action"_s, static_cast<int>(act));
        ob.insert(u"toPlayer"_s, toPlayer);
        ob.insert(u"toPlace"_s, toPlace);

        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestAction, ob));
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendUpgradeReply(const QList<QMdmmCore::Data::UpgradeItem> &items)
{
    if (socket != nullptr && currentRequest == QMdmmCore::Protocol::RequestUpgrade) {
        currentRequest = QMdmmCore::Protocol::RequestInvalid;
        QJsonArray arr;
        foreach (QMdmmCore::Data::UpgradeItem it, items)
            arr.append(static_cast<int>(it));

        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, QMdmmCore::Protocol::RequestUpgrade, arr));
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendSpeak(const QString &content)
{
    // Although JSON is native UTF-8 we decided to use Base64 anyway.
    // This can make our request / response all in one line.
    if (socket != nullptr)
        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifySpeak, QString::fromLatin1(content.toUtf8().toBase64())));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendOperate(const QJsonValue &todo)
{
    Q_UNIMPLEMENTED();
    Q_UNUSED(todo);

    if (socket != nullptr)
        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyOperate, {}));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendRequestGivenUp()
{
    // Give up on the current request: send a null-valued reply carrying the *current* request
    // id, then stop tracking the request locally. Null is the protocol's "give up" marker --
    // every legal reply value is non-null (RPS=int / actionOrder=array / action=object /
    // upgrade=array), so null is unambiguous. The request id must be captured before resetting
    // currentRequest: resetting it first made the reply carry RequestInvalid, which the server's
    // `currentRequest == packet.requestId()` check dropped, so the give-up never reached it (D-020).
    if (socket != nullptr && currentRequest != QMdmmCore::Protocol::RequestInvalid) {
        QMdmmCore::Protocol::RequestId requestId = currentRequest;
        currentRequest = QMdmmCore::Protocol::RequestInvalid;
        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::TypeReply, requestId, QJsonValue(QJsonValue::Null)));
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::sendManagedChanged(bool managed)
{
    // Declare the managed flag to the server, which owns the agent state: it applies the flag and
    // reports the result back through the ordinary agent state broadcast, so the client never
    // treats its own declaration as the final word. The player is identified by the socket, hence
    // no name in the payload.
    if (socket != nullptr) {
        QJsonObject ob;
        ob.insert(u"managed"_s, managed);
        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyManagedChanged, ob));
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::socketPacketReceived(const QMdmmCore::Packet &packet)
{
    if (socket == nullptr)
        return;

    if (packet.type() == QMdmmCore::Protocol::TypeRequest) {
        currentRequest = packet.requestId();
        void (ClientP::*call)(const QJsonValue &) = requestCallbacks().value(packet.requestId(), nullptr);
        if (call != nullptr)
            (this->*call)(packet.value());
        else
            socket->setError({.code = Socket::ProtocolError, .errorString = {}});
        return;
    }

    if (packet.type() == QMdmmCore::Protocol::TypeNotify) {
        // A notify from the server (pong / version) or from an agent (broadcast) is decoded and
        // handed to the Agent.
        if (((packet.notifyId() & QMdmmCore::Protocol::NotifyFromServerMask) != 0) || ((packet.notifyId() & QMdmmCore::Protocol::NotifyFromAgentMask) != 0)) {
            void (ClientP::*call)(const QJsonValue &) = notifyCallbacks().value(packet.notifyId(), nullptr);
            if (call != nullptr)
                (this->*call)(packet.value());
            else
                socket->setError({.code = Socket::ProtocolError, .errorString = {}});
            return;
        }

        // A client-bound notify echoed back (or an invalid notify id) is abnormal: drop the
        // connection (D-025). The client is the replying side, so it has no "default reply" of its
        // own -- a drop is all there is to do; the existing reconnect path takes over from here.
        socket->setError({.code = Socket::ProtocolError, .errorString = {}});
        return;
    }

    // A reply or an invalid/unknown packet type from the server is abnormal: replies only
    // originate from the client (D-025).
    socket->setError({.code = Socket::ProtocolError, .errorString = {}});
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::socketErrorOccurred(const Socket::Error &error)
{
    handleSocketGone(error.errorString);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::socketDisconnected()
{
    handleSocketGone(u"Disconnected"_s);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::handleSocketGone(const QString &errorString)
{
    if (socket == nullptr)
        return;

    connected = false;

    // Detach first so a second signal from the same socket (error + disconnected
    // often fire back to back) can't re-enter this handler.
    socket->disconnect(this);
    socket->deleteLater();

    // Only notify the upper layer once per disconnect episode. Retries that fail
    // again keep the client in the "disconnected" state without spamming.
    if (!reconnectInProgress) {
        reconnectInProgress = true;
        reconnectAttempts = 0;
        emit q->socketConnectionLost(errorString, Client::QPrivateSignal());
    }

    scheduleReconnect();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
bool ClientP::connectSocket()
{
    // The client's objectName is its playerName and is registered as the self Agent's key in
    // initSelfAgent(). It must not change afterwards: guard the invariant so a rename during
    // the client's lifetime is caught in debug builds, instead of silently desynchronizing the
    // agents key from the sign-in playerName.
    Q_ASSERT(q->objectName() == selfAgent->objectName());

    socket = new Socket(this);
    connect(socket, &Socket::socketDisconnected, this, &ClientP::socketDisconnected);
    connect(socket, &Socket::socketErrorOccurred, this, &ClientP::socketErrorOccurred);
    connect(socket, &Socket::packetReceived, this, &ClientP::socketPacketReceived);
    if (socket->connectToHost(host)) {
        connected = true;
        return true;
    }

    // The host does not map to a known transport. Drop the socketless wrapper so
    // socket stays null and the caller can report the failure.
    connected = false;
    socket->disconnect(this);
    socket->deleteLater();
    return false;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::scheduleReconnect()
{
    if (reconnectAttempts >= MaxReconnectAttempts) {
        // Out of retries: stay disconnected and let the upper layer decide.
        reconnectInProgress = false;
        reconnectTimer->stop();
        emit q->socketErrorDisconnected(u"Reconnect failed"_s, Client::QPrivateSignal());
        return;
    }

    const int interval = ReconnectBaseIntervalMs * static_cast<int>(1U << std::min(static_cast<unsigned int>(reconnectAttempts), 4U));
    reconnectTimer->start(std::min(interval, ReconnectMaxIntervalMs));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::reconnectTimeout()
{
    ++reconnectAttempts;
    if (connectSocket())
        return;

    // The saved host is not connectable (invalid transport). No point retrying.
    reconnectInProgress = false;
    reconnectTimer->stop();
    emit q->socketErrorDisconnected(u"Reconnect failed"_s, Client::QPrivateSignal());
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void ClientP::heartbeatTimeout()
{
    if (socket != nullptr)
        emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyPingServer, QDateTime::currentMSecsSinceEpoch()));
}

} // namespace p
} // namespace QMdmmNetworking

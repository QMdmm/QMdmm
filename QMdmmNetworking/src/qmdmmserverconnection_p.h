// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMNETWORKING_QMDMMSERVERCONNECTION_P_H
#define QMDMMNETWORKING_QMDMMSERVERCONNECTION_P_H

#include "qmdmmagent.h"
#include "qmdmmsocket.h"

#include <QMdmmLogicConfiguration>

#include <QPointer>
#include <QTimer>

namespace QMdmmNetworking {
namespace p {

// The server-side plumbing for one connected player: the socket, the request timer, the current
// request state, the protocol dispatch tables, and the round-event log. It is a *companion* to an
// Agent (composition, not inheritance): the Agent owns the player identity (name / screen name /
// state), while the ServerConnectionP owns everything tied to the wire. This split lets a
// socket-less local agent exist later without dragging socket machinery into the Agent type.
class QMDMMNETWORKING_PRIVATE_EXPORT ServerConnectionP : public QObject
{
    Q_OBJECT

    // The protocol dispatch tables are returned from accessors rather than declared as static
    // data members: building them allocates, and a failure must reach the caller instead of
    // terminating the process during static initialization (cert-err58-cpp).
    static const QHash<QMdmmCore::Protocol::NotifyId, void (ServerConnectionP::*)(const QJsonValue &)> &notifyCallbacks();
    static const QHash<QMdmmCore::Protocol::RequestId, void (ServerConnectionP::*)(const QJsonValue &)> &replyCallbacks();
    static const QHash<QMdmmCore::Protocol::RequestId, void (ServerConnectionP::*)()> &defaultReplyCallbacks();

    static int requestTimeoutGracePeriod;

public:
    Q_DISABLE_COPY_MOVE(ServerConnectionP)

    ServerConnectionP(Agent *agent, const QMdmmCore::LogicConfiguration &logicConfiguration, int requestTimeout, QObject *parent = nullptr);
    ~ServerConnectionP() override;

    void setSocket(Socket *_socket);

    QPointer<Socket> socket;
    Agent *agent;
    QMdmmCore::LogicConfiguration conf;

    QMdmmCore::Protocol::RequestId currentRequest;
    QJsonValue currentRequestValue;

    // Whether the request timer is armed at all. A configured requestTimeout of 0 means "no
    // timeout": the server never gives up on a client that stays silent, so the timer is left
    // unarmed. Any non-zero value is backstopped at requestTimeout + the grace period.
    bool requestTimeoutEnabled;
    QTimer *requestTimer;

    // Round-event log: every round-event packet this connection has broadcast (rps / action-order
    // / action / upgrade), in send order. The list index IS the round-event sequence number (events
    // are appended in broadcast order, and the client counts one per event it receives), so no
    // explicit sequence is stored and no sorting is needed. Used for the per-agent precise
    // catch-up on reconnect (backlog "precise catch-up"): a reconnecting client is replayed only
    // the events it missed, in send order.
    QList<QMdmmCore::Packet> roundEventLog;

    void clearRoundEventLog();
    void replayMissedRoundEvents(int lastRoundEventSeq);

    // Reconnect on the wire layer (D-018): rebind the socket and replay the round events the
    // reconnecting client missed. The logic-side half of a reconnect (restore online/trust +
    // state snapshot) lives on LogicRunner::reconnectAgent, which only deals with agents.
    void reconnect(Socket *socket, int lastRoundEventSeq);

    void addRequest(QMdmmCore::Protocol::RequestId requestId, const QJsonValue &value);

    // reply decode callbacks: validate the wire value and hand the strong-typed reply to the Agent
    // (which then forwards it as the corresponding replyXxx signal). These keep only the JSON
    // validation / type conversion, which is the wire's job; the controller logic lives on Agent.
    void decodeRockPaperScissorsReply(const QJsonValue &value);
    void decodeActionOrderReply(const QJsonValue &value);
    void decodeActionReply(const QJsonValue &value);
    void decodeUpgradeReply(const QJsonValue &value);

    void defaultReplyRockPaperScissors();
    void defaultReplyActionOrder();
    void defaultReplyAction();
    void defaultReplyUpgrade();

    // player speech / operation: hand the wire value to the Agent (which then forwards it as the
    // spoken / operated signal). The wire only strips the value; the controller logic lives on Agent.
    void receiveSpeak(const QJsonValue &value);
    void receiveOperate(const QJsonValue &value);

signals:
    void sendPacket(QMdmmCore::Packet packet);
    void agentDisconnected(Agent *agent);

public slots: // NOLINT(readability-redundant-access-specifiers)
    void packetReceived(const QMdmmCore::Packet &packet);
    void onSocketDisconnected();

    // requests (encode + send): these slots listen to the Agent's xxxRequested signals and turn
    // each request into a wire packet. The controller methods themselves (requestXxx) now live on
    // the Agent.
    void sendRockPaperScissorsRequested(const QStringList &playerNames, int strivedOrder);
    void sendActionOrderRequested(const QList<int> &remainedOrders, int maximumOrder, int selectionNum);
    void sendActionRequested(int currentOrder);
    void sendUpgradeRequested(int remainingTimes);

    // notifications (encode + send): these slots listen to the Agent's xxxNotified signals and
    // turn each notification into a wire packet. The controller methods themselves (notifyXxx)
    // now live on the Agent.
    void sendLogicConfigurationNotified();
    void sendAgentStateChangeNotified(const QString &playerName, const QMdmmCore::Data::AgentState &agentState);
    void sendPlayerAddNotified(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState);
    void sendPlayerRemoveNotified(const QString &playerName);
    void sendGameStartNotified();
    void sendRoundStartNotified();
    void sendRockPaperScissorsNotified(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies);
    void sendActionOrderNotified(const QStringList &result);
    void sendActionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace);
    void sendRoundOverNotified();
    void sendUpgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades);
    void sendGameOverNotified(const QStringList &playerNames);
    void sendSpeakNotified(const QString &playerName, const QString &content);
    void sendOperateNotified(const QString &playerName, const QJsonValue &todo);

    void requestTimeout();
    void executeDefaultReply();
};
} // namespace p
} // namespace QMdmmNetworking

#endif

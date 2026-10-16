// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMCLIENT_P
#define QMDMMCLIENT_P

#include "qmdmmclient.h"

#include "qmdmmagent.h"
#include "qmdmmsocket.h"

#include <QMdmmProtocol>
#include <QMdmmRoom>

#include <QLocalSocket>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): This is private header

namespace QMdmmNetworking {

namespace p {

class QMDMMNETWORKING_PRIVATE_EXPORT ClientP final : public QObject
{
    Q_OBJECT

public:
    // The protocol dispatch tables are returned from accessors rather than declared as static
    // data members: building them allocates, and a failure must reach the caller instead of
    // terminating the process during static initialization (cert-err58-cpp).
    static const QHash<QMdmmCore::Protocol::RequestId, void (ClientP::*)(const QJsonValue &)> &requestCallbacks();
    static const QHash<QMdmmCore::Protocol::NotifyId, void (ClientP::*)(const QJsonValue &)> &notifyCallbacks();

    ClientP(ClientConfiguration clientConfiguration, Client *q);

    // Create and register this client's own Agent (name = the client's objectName), so the
    // operation side (GUI / Bot) always has an Agent to drive.
    void initSelfAgent();

    Client *q;
    ClientConfiguration clientConfiguration;
    QPointer<Socket> socket;
    QMdmmCore::Room *room;
    // Mirror agents for every player the client knows about (the client's own agent is keyed by
    // the client's objectName, the others by their player name). `selfAgent` is a stable handle
    // to the client's own agent -- stable even if the operation side renames the client (which
    // would otherwise desynchronize the objectName key), and the target every incoming notify is
    // routed to.
    QHash<QString, Agent *> agents;
    Agent *selfAgent = nullptr;

    QTimer *heartbeatTimer;
    QTimer *reconnectTimer;
    QString host;
    int reconnectAttempts;
    bool reconnectInProgress;
    // Whether a connection is currently up (connectSocket succeeded and no disconnect happened
    // yet). Backs Client::isConnected(); tracked explicitly because the QPointer<Socket> stays
    // non-null until deleteLater() is processed, so it cannot be used for a synchronous query.
    bool connected;

    QMdmmCore::Protocol::RequestId currentRequest;
    QMdmmCore::Data::AgentState initialState;

    // Number of round events received this round. The client increments this counter by one for
    // every round-event notify it receives (and resets it on notifyRoundStart), and reports it on
    // reconnect for the precise catch-up (see backlog "precise catch-up"). Its value doubles as
    // the last received round-event sequence number.
    int lastRoundEventSeq = 0;

    void requestRockPaperScissors(const QJsonValue &value);
    void requestActionOrder(const QJsonValue &value);
    void requestAction(const QJsonValue &value);
    void requestUpgrade(const QJsonValue &value);

    void notifyPongServer(const QJsonValue &value);
    void notifyVersion(const QJsonValue &value);
    void notifyLogicConfiguration(const QJsonValue &value);
    void notifyAgentStateChanged(const QJsonValue &value);
    void notifyPlayerAdded(const QJsonValue &value);
    void notifyPlayerRemoved(const QJsonValue &value);
    void notifyGameStart(const QJsonValue &value);
    void notifyRoundStart(const QJsonValue &value);
    void notifyRockPaperScissors(const QJsonValue &value);
    void notifyActionOrder(const QJsonValue &value);
    void notifyAction(const QJsonValue &value);
    void notifyRoundOver(const QJsonValue &value);
    void notifyUpgrade(const QJsonValue &value);
    void notifyGameOver(const QJsonValue &value);
    void notifySpoken(const QJsonValue &value);
    void notifyOperated(const QJsonValue &value);

    bool applyAction(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace);
    bool applyUpgrade(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades);

    bool connectSocket();
    void handleSocketGone(const QString &errorString);
    void scheduleReconnect();

public slots: // NOLINT(readability-redundant-access-specifiers)
    void socketPacketReceived(const QMdmmCore::Packet &packet);
    void socketErrorOccurred(const Socket::Error &error);
    void socketDisconnected();

    void reconnectTimeout();
    void heartbeatTimeout();

    // encode + send the client's own agent's replies / speech / operation to the server. These
    // slots listen to the Agent's replyXxx / spoken / operated signals (the operation side drives
    // the Agent's bare-verb methods / speak / operate, which forward as these signals), and turn
    // them back into wire packets. This mirrors ServerConnectionP's send*Requested / send*Notified
    // slots on the server side.
    void sendRockPaperScissorsReply(QMdmmCore::Data::RockPaperScissors rps);
    void sendActionOrderReply(const QList<int> &order);
    void sendActionReply(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace);
    void sendUpgradeReply(const QList<QMdmmCore::Data::UpgradeItem> &items);
    void sendSpeak(const QString &content);
    void sendOperate(const QJsonValue &todo);
    // The operation side gave up on the current request: stop tracking it and send a "give up"
    // reply that triggers the server's default reply logic. Mirrors Agent::giveUpRequest (formerly
    // requestTimeout, moved from Client in D-019).
    void sendRequestGivenUp();
    // The operation side declared a different state for this client's own agent: report the state
    // it carries to the server, which owns the agent state and reports a change of it to the room.
    // Mirrors Agent::setManaged.
    void sendAgentStateChanged(QMdmmCore::Data::AgentState state);
};
} // namespace p
} // namespace QMdmmNetworking

// NOLINTEND(misc-non-private-member-variables-in-classes): This is private header

#endif

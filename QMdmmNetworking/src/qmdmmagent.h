// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMAGENT_H
#define QMDMMAGENT_H

#include "qmdmmnetworkingglobal.h"

#include <QMdmmData>
#include <QMdmmProtocol>

#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QObject>

QMDMM_EXPORT_NAME(QMdmmAgent)

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace p {
struct AgentP;
}
#endif

#ifndef DOXYGEN
namespace v0 {
#endif

class Socket;

class QMDMMNETWORKING_EXPORT Agent final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString screenName READ screenName WRITE setScreenName NOTIFY screenNameChanged FINAL)
    Q_PROPERTY(QMdmmCore::Data::AgentState state READ state WRITE setState NOTIFY stateChanged FINAL)

public:
    Q_DISABLE_COPY_MOVE(Agent);

    explicit Agent(const QString &name, QObject *parent = nullptr);
    ~Agent() override;

    // properties
    [[nodiscard]] QString screenName() const;
    void setScreenName(const QString &name);

    [[nodiscard]] QMdmmCore::Data::AgentState state() const;
    void setState(const QMdmmCore::Data::AgentState &state);

    // The managed flag (StateMaskTrust) is read out of `state` rather than being a property of its
    // own: it has no NOTIFY of its own to give, and it moves as part of the state on both sides --
    // set here the moment the operation side declares it, and set from the wire again when the
    // server's own state arrives (see setManaged) -- so it cannot be described by a plain change
    // signal.
    //
    // Its readers are spread out and not all through this accessor: the client that gives up on its
    // player's requests reads the flag here, while the card that draws it tests the state mask it
    // was handed instead. What the flag means is settled by all of them together, so a change to it
    // has to be checked against every reader -- the one added later is exactly what reading this
    // declaration will not turn up.
    [[nodiscard]] bool managed() const;
    void setManaged(bool managed);

    void notifyLogicConfiguration();
    void notifyAgentStateChange(const QString &playerName, const QMdmmCore::Data::AgentState &agentState);
    void notifyPlayerAdd(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState);
    void notifyPlayerRemove(const QString &playerName);
    void notifyGameStart();
    void notifyRoundStart();
    void notifyRockPaperScissors(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies);
    void notifyActionOrder(const QStringList &result);
    void notifyAction(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace);
    void notifyRoundOver();
    void notifyUpgrade(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades);
    void notifyGameOver(const QStringList &playerNames);
    void notifySpeak(const QString &playerName, const QString &content);
    void notifyOperate(const QString &playerName, const QJsonValue &todo);

    void requestRockPaperScissors(const QStringList &playerNames, int strivedOrder);
    void requestActionOrder(const QList<int> &remainedOrders, int maximumOrder, int selectionNum);
    void requestAction(int currentOrder);
    void requestUpgrade(int remainingTimes);

    void rockPaperScissors(QMdmmCore::Data::RockPaperScissors rps);
    void actionOrder(const QList<int> &order);
    void action(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace);
    void upgrade(const QList<QMdmmCore::Data::UpgradeItem> &items);

    void giveUpRequest();

    void speak(const QString &content);
    void operate(const QJsonValue &todo);

signals:
    void screenNameChanged(const QString &, QPrivateSignal);
    void stateChanged(QMdmmCore::Data::AgentState, QPrivateSignal);
    // The operation side declared a different managed state (see setManaged). The server's own view
    // of the flag arrives through stateChanged instead, so a server-driven change is never declared
    // back out on the wire.
    void managedChanged(bool managed, QPrivateSignal);

    void logicConfigurationNotified(QPrivateSignal);
    void agentStateChangeNotified(const QString &playerName, const QMdmmCore::Data::AgentState &agentState, QPrivateSignal);
    void playerAddNotified(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState, QPrivateSignal);
    void playerRemoveNotified(const QString &playerName, QPrivateSignal);
    void gameStartNotified(QPrivateSignal);
    void roundStartNotified(QPrivateSignal);
    void rockPaperScissorsNotified(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies, QPrivateSignal);
    void actionOrderNotified(const QStringList &result, QPrivateSignal);
    void actionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace, QPrivateSignal);
    void roundOverNotified(QPrivateSignal);
    void upgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades, QPrivateSignal);
    void gameOverNotified(const QStringList &playerNames, QPrivateSignal);
    void speakNotified(const QString &playerName, const QString &content, QPrivateSignal);
    void operateNotified(const QString &playerName, const QJsonValue &todo, QPrivateSignal);

    void rockPaperScissorsRequested(const QStringList &playerNames, int strivedOrder, QPrivateSignal);
    void actionOrderRequested(const QList<int> &remainedOrders, int maximumOrder, int selectionNum, QPrivateSignal);
    void actionRequested(int currentOrder, QPrivateSignal);
    void upgradeRequested(int remainingTimes, QPrivateSignal);

    void replyRockPaperScissors(QMdmmCore::Data::RockPaperScissors rps, QPrivateSignal);
    void replyActionOrder(const QList<int> &order, QPrivateSignal);
    void replyAction(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace, QPrivateSignal);
    void replyUpgrade(const QList<QMdmmCore::Data::UpgradeItem> &items, QPrivateSignal);

    void requestGivenUp(QPrivateSignal);

    void spoken(const QString &content, QPrivateSignal);
    void operated(const QJsonValue &todo, QPrivateSignal);

#ifndef DOXYGEN
private:
    const std::unique_ptr<p::AgentP> d;
#endif
};

#ifndef DOXYGEN
} // namespace v0
inline namespace v1 {
using v0::Agent;
}
#endif
} // namespace QMdmmNetworking

#endif

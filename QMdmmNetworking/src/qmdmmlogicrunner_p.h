// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMLOGICRUNNER_P
#define QMDMMLOGICRUNNER_P

#include "qmdmmlogicrunner.h"
#include "qmdmmserverconnection_p.h"

#include "qmdmmagent.h"
#include "qmdmmsocket.h"

#include <QMdmmLogic>
#include <QMdmmRoom>

#include <QPointer>
#include <QThread>
#include <QTimer>
#include <utility>

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): This is private header

namespace QMdmmNetworking {
namespace p {

class QMDMMNETWORKING_PRIVATE_EXPORT LogicRunnerP : public QObject
{
    Q_OBJECT

public:
    Q_DISABLE_COPY_MOVE(LogicRunnerP);

    LogicRunnerP(QMdmmCore::LogicConfiguration logicConfiguration, int playerNumPerRoom, LogicRunner *q);
    ~LogicRunnerP() override;

    LogicRunner *q;

    QHash<QString, Agent *> agents;
    QHash<QString, ServerConnectionP *> connections;

    QThread *logicThread;
    QPointer<QMdmmCore::Logic> logic;

    QMdmmCore::LogicConfiguration conf;
    int playerNumPerRoom;

    // No qRegisterMetaType<>() is needed for the queued signals / slots below: their argument
    // types are QMdmmCore::Data enums / flags (auto-registered via Q_ENUM_NS / Q_FLAG_NS) plus
    // Qt's built-in container metatypes, and the connections use the function-pointer syntax.
    // Verified by the in-process smoke test, which drives a full game across the logic thread.

public slots: // NOLINT(readability-redundant-access-specifiers)
    // slots called from agent
    void agentStateChanged(const QMdmmCore::Data::AgentState &state);
    void agentSpoken(const QString &content);
    void agentOperated(const QJsonValue &value);
    void agentRockPaperScissorsReplied(QMdmmCore::Data::RockPaperScissors rps);
    void agentActionOrderReplied(const QList<int> &order);
    void agentActionReplied(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace);
    void agentUpgradeReplied(const QList<QMdmmCore::Data::UpgradeItem> &items);
    void agentDisconnected(Agent *agent);

    // These slots are called from Logic
    void requestRpsForAction(const QStringList &playerNames);
    void rpsResult(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies);
    void requestActionOrder(const QString &playerName, const QList<int> &availableOrders, int maximumOrderNum, int selections);
    void actionOrderResult(const QHash<int, QString> &result);
    void requestRpsForActionOrder(const QStringList &playerNames, int strivedOrder);
    void requestAction(const QString &playerName, int actionOrder);
    void actionResult(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace);
    void requestUpgrade(const QString &playerName, int upgradePoint);
    void upgradeResult(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades);
    void roundOver();
    void gameOver(const QStringList &winners);

signals: // NOLINT(readability-redundant-access-specifiers)
    // These signals are emitted to Logic
    void addPlayer(const QString &playerName);
    void removePlayer(const QString &playerName);
    void roundStart();
    void rpsReply(const QString &playerName, QMdmmCore::Data::RockPaperScissors rps);
    void actionOrderReply(const QString &playerName, const QList<int> &desiredOrder);
    void actionReply(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace);
    void upgradeReply(const QString &playerName, const QList<QMdmmCore::Data::UpgradeItem> &items);
};

} // namespace p
} // namespace QMdmmNetworking

// NOLINTEND(misc-non-private-member-variables-in-classes): This is private header

#endif

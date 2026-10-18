// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmlogicrunner_p.h"
#include "qmdmmnotify_p.h"
#include "qmdmmserverconnection_p.h"

#include <QMdmmGlobal>
#include <QMdmmLogicConfiguration>

#include <QDateTime>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace QMdmmNetworking {
namespace p {

LogicRunnerP::LogicRunnerP(QMdmmCore::LogicConfiguration logicConfiguration, int playerNumPerRoom, LogicRunner *q)
    : QObject(q)
    , q(q)
    , logicThread(new QThread(this))
    , conf(std::move(logicConfiguration))
    , playerNumPerRoom(playerNumPerRoom)
{
    logic = new QMdmmCore::Logic(conf);
    logic->moveToThread(logicThread);
    connect(logicThread, &QThread::finished, logic, &QMdmmCore::Logic::deleteLater);
    logicThread->start();

    // NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define CONNECTRUNNERTOLOGIC(signalName) connect(this, &LogicRunnerP::signalName, logic, &QMdmmCore::Logic::signalName, Qt::QueuedConnection)

    CONNECTRUNNERTOLOGIC(addPlayer);
    CONNECTRUNNERTOLOGIC(removePlayer);
    CONNECTRUNNERTOLOGIC(roundStart);
    CONNECTRUNNERTOLOGIC(rpsReply);
    CONNECTRUNNERTOLOGIC(actionOrderReply);
    CONNECTRUNNERTOLOGIC(actionReply);
    CONNECTRUNNERTOLOGIC(upgradeReply);

#undef CONNECTRUNNERTOLOGIC

#define CONNECTLOGICTORUNNER(signalName) connect(logic, &QMdmmCore::Logic::signalName, this, &LogicRunnerP::signalName, Qt::QueuedConnection)

    CONNECTLOGICTORUNNER(requestRpsForAction);
    CONNECTLOGICTORUNNER(rpsResult);
    CONNECTLOGICTORUNNER(requestActionOrder);
    CONNECTLOGICTORUNNER(actionOrderResult);
    CONNECTLOGICTORUNNER(requestRpsForActionOrder);
    CONNECTLOGICTORUNNER(requestAction);
    CONNECTLOGICTORUNNER(actionResult);
    CONNECTLOGICTORUNNER(requestUpgrade);
    CONNECTLOGICTORUNNER(upgradeResult);
    CONNECTLOGICTORUNNER(roundOver);
    CONNECTLOGICTORUNNER(gameOver);

#undef CONNECTLOGICTORUNNER
    // NOLINTEND(cppcoreguidelines-macro-usage)
}

void LogicRunnerP::configureRecording(const QString &directory, const QJsonObject &serverConfiguration)
{
    if (directory.isEmpty())
        return;

    // The header repeats what the server's -d dumps (the two configuration objects) and adds the
    // two things that dump cannot know: which build wrote the record, and when the game was played.
    QJsonObject header;
    header.insert(u"build"_s, QMdmmCore::Global::version().toString());
    header.insert(u"recordedAt"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    header.insert(u"ServerConfiguration"_s, serverConfiguration);
    header.insert(u"LogicConfiguration"_s, static_cast<QJsonObject>(conf));

    recorder.setTarget(directory, QJsonDocument(header).toJson(QJsonDocument::Compact));
}

void LogicRunnerP::agentStateChanged(const QMdmmCore::Data::AgentState &state)
{
    Agent *changedAgent = qobject_cast<Agent *>(sender());
    if (changedAgent == nullptr)
        return;

    foreach (Agent *agent, agents) {
        if (agent != changedAgent)
            agent->notifyAgentStateChange(changedAgent->objectName(), state);
    }
}

void LogicRunnerP::agentSpoken(const QString &content)
{
    Agent *speakAgent = qobject_cast<Agent *>(sender());
    if (speakAgent == nullptr)
        return;

    if (!content.isEmpty()) {
        foreach (Agent *agent, agents)
            agent->notifySpeak(speakAgent->objectName(), content);
    }
}

void LogicRunnerP::agentOperated(const QJsonValue &value)
{
    Agent *operateAgent = qobject_cast<Agent *>(sender());
    if (operateAgent == nullptr)
        return;

    foreach (Agent *agent, agents)
        agent->notifyOperate(operateAgent->objectName(), value);
}

void LogicRunnerP::agentRockPaperScissorsReplied(QMdmmCore::Data::RockPaperScissors rps)
{
    Agent *repliedAgent = qobject_cast<Agent *>(sender());
    if (repliedAgent == nullptr)
        return;

    emit rpsReply(repliedAgent->objectName(), rps);
}

void LogicRunnerP::agentActionOrderReplied(const QList<int> &order)
{
    Agent *repliedAgent = qobject_cast<Agent *>(sender());
    if (repliedAgent == nullptr)
        return;

    emit actionOrderReply(repliedAgent->objectName(), order);
}

void LogicRunnerP::agentActionReplied(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace)
{
    Agent *repliedAgent = qobject_cast<Agent *>(sender());
    if (repliedAgent == nullptr)
        return;

    emit actionReply(repliedAgent->objectName(), act, toPlayer, toPlace);
}

void LogicRunnerP::agentUpgradeReplied(const QList<QMdmmCore::Data::UpgradeItem> &items)
{
    Agent *repliedAgent = qobject_cast<Agent *>(sender());
    if (repliedAgent == nullptr)
        return;

    emit upgradeReply(repliedAgent->objectName(), items);
}

void LogicRunnerP::agentDisconnected(Agent *disconnectedAgent)
{
    if (disconnectedAgent == nullptr)
        return;

    if (q->full()) {
        // case 1: room is full, so game has started.
        // ServerConnectionP::onSocketDisconnected already marked the agent offline and
        // auto-replied the active request. The seat is preserved so the player can reconnect;
        // if they do not reconnect before the round is over, the game is abandoned:
        // LogicRunnerP::upgradeResult detects the still-offline agent and ends the game (see
        // the round-over check there).

        // if all agents are disconnected, terminate the game.
        bool allDisconnected = true;
        foreach (Agent *agent, agents) {
            if (agent->state().testFlag(QMdmmCore::Data::StateMaskOnline)) {
                allDisconnected = false;
                break;
            }
        }

        if (allDisconnected) {
            // No one is left to play, so this game is over too: end its record the same way a
            // played-out one is ended, rather than leaving the file open until the room is deleted.
            recorder.close();
            emit q->gameOver(LogicRunner::QPrivateSignal());
        }
    } else {
        // case 2: room is not full, so game hasn't started
        // Agent should be deleted.
        const QString playerName = disconnectedAgent->objectName();
        Agent *takenAgent = agents.take(playerName);
        ServerConnectionP *takenConn = connections.take(playerName);
        Q_UNUSED(takenAgent);
        Q_ASSERT(takenAgent == disconnectedAgent);
        Q_ASSERT(takenConn != nullptr);

        foreach (Agent *agent, agents)
            agent->notifyPlayerRemove(playerName);
        emit removePlayer(playerName);

        disconnectedAgent->deleteLater();
        takenConn->deleteLater();
    }
}

LogicRunnerP::~LogicRunnerP()
{
    // The logic lives in logicThread. Tear it down cleanly so the thread is no
    // longer running when this object (and thus logicThread, its child) is
    // destroyed -- otherwise Qt's QThread destructor hits
    // qFatal("QThread: Destroyed while thread ... is still running") and aborts.
    if (logic)
        logic->deleteLater();
    logicThread->quit();
    logicThread->wait();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::requestRpsForAction(const QStringList &playerNames)
{
    foreach (const QString &playerName, playerNames) {
        Agent *agent = agents.value(playerName);
        agent->requestRockPaperScissors(playerNames, 0);
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::rpsResult(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies)
{
    // Each agent records the round event it broadcasts in its connection's roundEventLog (see
    // ServerConnectionP::sendRockPaperScissorsNotified), for the per-agent reconnect catch-up.
    foreach (Agent *agent, agents)
        agent->notifyRockPaperScissors(replies);

    // The game record gets the broadcast once, not once per player: every connection builds the
    // same packet, so the recorder takes the one shape that went down the wires (see qmdmmnotify_p.h).
    if (recorder.isOpen())
        recorder.record(p::rockPaperScissorsNotified(replies));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::requestActionOrder(const QString &playerName, const QList<int> &availableOrders, int maximumOrderNum, int selections)
{
    Agent *agent = agents.value(playerName);
    agent->requestActionOrder(availableOrders, maximumOrderNum, selections);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::actionOrderResult(const QHash<int, QString> &result)
{
    // Core signals the action order as a dense 1..N map (order -> player). Collapse it to a
    // plain QStringList for the notify chain: index i holds the player taking order i + 1.
    QStringList order;
    order.reserve(result.count());
    for (int i = 1; i <= result.count(); ++i)
        order << result.value(i);

    foreach (Agent *agent, agents)
        agent->notifyActionOrder(order);

    if (recorder.isOpen())
        recorder.record(p::actionOrderNotified(order));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::requestRpsForActionOrder(const QStringList &playerNames, int strivedOrder)
{
    foreach (const QString &playerName, playerNames) {
        Agent *agent = agents.value(playerName);
        agent->requestRockPaperScissors(playerNames, strivedOrder);
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::requestAction(const QString &playerName, int actionOrder)
{
    Agent *agent = agents.value(playerName);
    agent->requestAction(actionOrder);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::actionResult(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace)
{
    foreach (Agent *agent, agents)
        agent->notifyAction(playerName, action, toPlayer, toPlace);

    if (recorder.isOpen())
        recorder.record(p::actionNotified(playerName, action, toPlayer, toPlace));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::requestUpgrade(const QString &playerName, int upgradePoint)
{
    Agent *agent = agents.value(playerName);
    agent->requestUpgrade(upgradePoint);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void LogicRunnerP::upgradeResult(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades)
{
    // Round-over abandonment check (see the comment in agentDisconnected): a player whose socket
    // dropped mid-round is auto-replied so the logic keeps advancing, but if they have not
    // reconnected by the time the round is over, the game cannot continue. End it as a game over
    // (winners = the still-online agents) instead of notifying the upgrade result, matching
    // QMdmmCore::Logic's game-over behavior.
    foreach (Agent *agent, agents) {
        if (!agent->state().testFlag(QMdmmCore::Data::StateMaskOnline)) {
            QStringList winners;
            foreach (Agent *onlineAgent, agents) {
                if (onlineAgent->state().testFlag(QMdmmCore::Data::StateMaskOnline))
                    winners << onlineAgent->objectName();
            }
            gameOver(winners); // broadcasts notifyGameOver and emits q->gameOver (D-023)
            return;
        }
    }

    foreach (Agent *agent, agents)
        agent->notifyUpgrade(upgrades);

    if (recorder.isOpen())
        recorder.record(p::upgradeNotified(upgrades));

    // The upgrade phase finished without a game over. Advance to the next round.
    // This mirrors the initial kick-off in addAgent(): announce the new round to
    // every agent (so clients reset their local room via notifyRoundStart) and
    // then start it. Without this, the match stalls after the very first round.
    foreach (Agent *agent, agents)
        agent->notifyRoundStart();

    if (recorder.isOpen())
        recorder.record(p::roundStartNotified());

    // A new round begins: drop the previous round's events so the next round's log restarts empty
    // (the client resets its per-round event counter on notifyRoundStart, mirroring this).
    foreach (ServerConnectionP *conn, connections)
        conn->clearRoundEventLog();

    emit roundStart();
}

void LogicRunnerP::roundOver()
{
    // The round's action phase is over: drop each connection's round-event log. A reconnect from
    // here on is in the upgrade phase or the next round, where the old round's events are no longer
    // needed.
    foreach (ServerConnectionP *conn, connections)
        conn->clearRoundEventLog();

    foreach (Agent *agent, agents)
        agent->notifyRoundOver();

    if (recorder.isOpen())
        recorder.record(p::roundOverNotified());
}

void LogicRunnerP::gameOver(const QStringList &winners)
{
    foreach (Agent *agent, agents)
        agent->notifyGameOver(winners);

    if (recorder.isOpen())
        recorder.record(p::gameOverNotified(winners));

    // The game this record belongs to is over: close the file here, so that a reader can tell a
    // finished record from one whose server was still running.
    recorder.close();

    // A natural game over (a player maxed out all three stats) must tear down the room the same
    // way the round-over abandonment path does: emit LogicRunner::gameOver so the Server resets
    // `current` and deletes this runner (D-023). Without this, a naturally finished room leaks and
    // `current` keeps pointing at it.
    emit q->gameOver(LogicRunner::QPrivateSignal());
}

} // namespace p
} // namespace QMdmmNetworking

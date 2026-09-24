// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmlogicrunner.h"
#include "qmdmmlogicrunner_p.h"

#include <QJsonArray>
#include <QMetaType>
#include <QRandomGenerator>
#include <utility>

/**
 * @file qmdmmlogicrunner.h
 * @brief This is the file where the networking LogicRunner is defined.
 */

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class LogicRunner
 * @brief The server-side object that runs a single complete game.
 *
 * A LogicRunner owns the agents (server-side representations of connected clients) and
 * runs a @c QMdmmCore::Logic on a separate thread. It handles exactly one complete game:
 * when the game is over, the LogicRunner should be destroyed and all agents disconnected,
 * and players who want to continue playing need to rejoin.
 *
 * @note This class is designed for one game only. Lobby / multi-room support is not
 * implemented yet.
 * @note All methods must be called in the Server thread, where the LogicRunner instance
 * lives.
 */

/**
 * @brief ctor.
 * @param logicConfiguration The configuration of the logic
 * @param playerNumPerRoom The player number per room: the maximum number of agents this room
 * accepts before it becomes full. This is the running-time mirror of
 * @c ServerConfiguration::playerNumPerRoom, which the @c Server reads and passes in here (the
 * configuration is the serializable source of truth, this int is the per-game capacity). A
 * LogicRunner takes a plain @c int rather than the whole @c ServerConfiguration on purpose: it
 * only needs the room capacity and must not depend on the network-level settings (ports, socket
 * names, timeouts).
 * @param parent QObject parent.
 */
LogicRunner::LogicRunner(const QMdmmCore::LogicConfiguration &logicConfiguration, int playerNumPerRoom, QObject *parent)
    : QObject(parent)
    , d(new p::LogicRunnerP(logicConfiguration, playerNumPerRoom, this))
{
}

/**
 * @brief dtor.
 */
LogicRunner::~LogicRunner() = default;

/**
 * @brief Add a pre-wired agent to the game
 * @param agent the agent to add, already wired to its operation side (Server / GUI / Bot)
 * @return the added agent, or @c nullptr if the player name already exists or @p agent is @c nullptr
 *
 * This is the unified entry point for a player. The operation side creates the @c Agent and wires
 * it up before calling this -- for the network path, Server binds the socket on it; for a local
 * player, GUI / Bot simply own the agent. This function only registers the agent with the logic
 * side: it inserts the agent into the room, connects the agent's logic-port signals (replies /
 * speech / operation) to the room, and broadcasts the join to every player. The socket / wire
 * lifecycle is not LogicRunner's concern.
 */
Agent *LogicRunner::addAgent(Agent *agent)
{
    if (agent == nullptr)
        return nullptr;

    const QString playerName = agent->objectName();
    if (d->agents.contains(playerName))
        return nullptr;

    d->agents.insert(playerName, agent);

    // Register the agent's wire plumbing, if the operation side created one (network path). The
    // ServerConnectionP is a child of the agent so it travels with it; it reports the socket drop
    // as an Agent event (agentDisconnected) that the room listens to.
    if (p::ServerConnectionP *conn = agent->findChild<p::ServerConnectionP *>(); conn != nullptr) {
        connect(conn, &p::ServerConnectionP::agentDisconnected, d, &p::LogicRunnerP::agentDisconnected);
        d->connections.insert(playerName, conn);
    }

    // Connect the agent's logic-port signals to the room (identity change / speech / operation /
    // replies). The operation port (ServerConnectionP / GUI / Bot) is wired by whoever created the
    // agent, not here.
    connect(agent, &Agent::stateChanged, d, &p::LogicRunnerP::agentStateChanged);
    connect(agent, &Agent::spoken, d, &p::LogicRunnerP::agentSpoken);
    connect(agent, &Agent::operated, d, &p::LogicRunnerP::agentOperated);
    connect(agent, &Agent::replyRockPaperScissors, d, &p::LogicRunnerP::agentRockPaperScissorsReplied);
    connect(agent, &Agent::replyActionOrder, d, &p::LogicRunnerP::agentActionOrderReplied);
    connect(agent, &Agent::replyAction, d, &p::LogicRunnerP::agentActionReplied);
    connect(agent, &Agent::replyUpgrade, d, &p::LogicRunnerP::agentUpgradeReplied);

    // When a new agent is added, first we'd notify the logic configuration to client
    // This is also a signal to client that it should switch state for room data

    agent->notifyLogicConfiguration();

    emit d->addPlayer(playerName);

    foreach (Agent *a, d->agents)
        a->notifyPlayerAdd(playerName, agent->screenName(), agent->state());

    // Tell the newly added agent about every player that joined before it.
    // NOTE: iterate over the *existing* agents and report their identities,
    // not the new player's name again (that would duplicate notifyPlayerAdd
    // for the new agent and make the client treat it as a fatal error).
    foreach (Agent *a, d->agents) {
        if (a != agent)
            agent->notifyPlayerAdd(a->objectName(), a->screenName(), a->state());
    }

    if (full()) {
        foreach (Agent *a, d->agents)
            a->notifyGameStart();
        foreach (Agent *a, d->agents)
            a->notifyRoundStart();
        emit d->roundStart();
    }

    return agent;
}

/**
 * @brief Reconnect a previously disconnected agent by restoring its state and room snapshot
 * @param agent the offline agent to reconnect
 * @return the reconnected agent, or @c nullptr if @p agent is @c nullptr, not in this room, or still online
 *
 * A reconnect only makes sense for a player who is already in the room (the room is full, so the
 * game has started) but whose socket was disconnected or deleted, which also marked the agent
 * offline. This is the logic-side half of a reconnect: it restores the online flag (not Trust --
 * the "managed" flag, see @c StateMaskTrust) and resends the state snapshot so the reconnecting
 * client can rebuild its room view. The room itself only ever deals with agents, never sockets.
 */
Agent *LogicRunner::reconnectAgent(Agent *agent)
{
    if (agent == nullptr)
        return nullptr;

    // Only an agent that is already in this room can be reconnected.
    if (d->agents.value(agent->objectName(), nullptr) != agent)
        return nullptr;

    // A still-online agent is not a reconnect candidate: only an agent that
    // onSocketDisconnected marked offline can be reconnected here.
    if (agent->state().testFlag(QMdmmCore::Data::StateMaskOnline))
        return nullptr;

    // Restore the online flag that onSocketDisconnected cleared. Trust (the "managed" flag) is
    // NOT restored by default: a reconnecting player must not be automatically trusted. The flag
    // only travels -- a client declares that it manages its player and gives up on that player's
    // requests, so the server answers them with the same default a timeout gets while the player
    // stays connected. setState emits stateChanged, which LogicRunnerP::agentStateChanged turns
    // into a notifyAgentStateChange broadcast to every agent -- this is what lets the other,
    // still-connected clients see that the player is back online.
    QMdmmCore::Data::AgentState state = agent->state();
    state.setFlag(QMdmmCore::Data::StateMaskOnline, true);
    agent->setState(state);

    // Resend the state snapshot to the reconnected client so it can rebuild its room: the logic
    // configuration, then every player (identity + current state). The client treats a duplicate
    // notifyPlayerAdd as benign (ClientP::notifyPlayerAdded), and the notifyAgentStateChange
    // broadcast above is dropped by the reconnected client until it has learned the players here.
    agent->notifyLogicConfiguration();

    foreach (Agent *a, d->agents)
        agent->notifyPlayerAdd(a->objectName(), a->screenName(), a->state());

    return agent;
}

/**
 * @brief get the agent of a specific internal name
 * @param playerName the internal name of the searched agent
 * @return the agent of the internal name, or @c nullptr if not found
 */
Agent *LogicRunner::agent(const QString &playerName)
{
    return d->agents.value(playerName, nullptr);
}

/**
 * @brief get the agent of a specific internal name (const version)
 * @param playerName the internal name of the searched agent
 * @return the agent of the internal name, or @c nullptr if not found
 */
const Agent *LogicRunner::agent(const QString &playerName) const
{
    return d->agents.value(playerName, nullptr);
}

/**
 * @brief if the room is full
 * @return @c true if the number of agents reaches @c ServerConfiguration::playerNumPerRoom
 */
bool LogicRunner::full() const
{
    return d->agents.count() >= d->playerNumPerRoom;
}

/**
 * @fn LogicRunner::gameOver(QPrivateSignal)
 * @brief emitted when the game is over
 */

#ifndef DOXYGEN
} // namespace v0
#endif
} // namespace QMdmmNetworking

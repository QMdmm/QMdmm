// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmagent.h"
#include "qmdmmagent_p.h"

/**
 * @file qmdmmagent.h
 * @brief This is the file where the networking Agent is defined.
 */

namespace QMdmmNetworking {
#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class Agent
 * @brief The controller for one player, bridging the logic side and the operation side.
 *
 * An Agent is the unified player abstraction with two ports:
 * - the logic side (@c LogicRunner on the server, @c Client on the client) drives the
 *   agent through the requestXxx / notifyXxx methods and listens to the replyXxx /
 *   spoken / operated signals;
 * - the operation side listens to the xxxRequested / xxxNotified signals and answers by
 *   calling the bare-verb methods (rockPaperScissors / actionOrder / action / upgrade)
 *   and speak / operate.
 *
 * Reply contract: the operation side must answer asynchronously (e.g. via
 * QTimer::singleShot or after an event-loop round-trip), never synchronously from
 * inside the xxxRequested handler. A synchronous reply re-enters the request handler
 * and violates the designed async request / reply flow.
 */

/**
 * @property Agent::screenName
 * @brief the screen name (display name) of the agent.
 */

/**
 * @property Agent::state
 * @brief the connection state of the agent.
 *
 * This is a combination of flags defined in @c QMdmmCore::Data::AgentState (e.g.
 * whether the agent is online, is a bot, or is trusted).
 */

/**
 * @brief ctor.
 * @param name The internal name of the agent
 * @param parent QObject parent.
 */
Agent::Agent(const QString &name, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<p::AgentP>())
{
    setObjectName(name);
}

/**
 * @brief dtor.
 */
Agent::~Agent() = default;

/**
 * @brief getter of property @c screenName
 * @return @c screenName
 */
QString Agent::screenName() const
{
    return d->screenName;
}

/**
 * @brief setter of property @c screenName
 * @param name @c screenName
 */
void Agent::setScreenName(const QString &name)
{
    if (name != screenName()) {
        d->screenName = name;
        emit screenNameChanged(name, QPrivateSignal());
    }
}

/**
 * @brief getter of property @c state
 * @return @c state
 */
QMdmmCore::Data::AgentState Agent::state() const
{
    return d->state;
}

/**
 * @brief setter of property @c state
 * @param state @c state
 */
void Agent::setState(const QMdmmCore::Data::AgentState &state)
{
    if (d->state != state) {
        d->state = state;
        emit stateChanged(state, QPrivateSignal());
    }
}

/**
 * @brief Whether the player is managed
 * @return @c true when the @c StateMaskTrust flag is set
 *
 * The bit is only declared, carried and reported here -- this class changes nothing about what it
 * does because of it. A client that manages its player gives up on that player's requests from its
 * own side (@c giveUpRequest), which leaves the server to reply with the same default a timeout
 * gets. See @c setManaged and @c StateMaskTrust.
 */
bool Agent::managed() const
{
    return d->state.testFlag(QMdmmCore::Data::StateMaskTrust);
}

/**
 * @brief Declare whether the player is managed
 * @param managed @c true to declare the player managed, @c false to withdraw the declaration
 *
 * The runtime counterpart of the @c StateMaskTrust bit a sign-in carries: on the client's own agent
 * this is what the client reports to the server, which owns the state, applies the flag and reports
 * the result back through the ordinary agent state broadcast. Declaring the value the state already
 * has is a no-op, so it puts nothing on the wire.
 */
void Agent::setManaged(bool managed)
{
    QMdmmCore::Data::AgentState state = d->state;
    state.setFlag(QMdmmCore::Data::StateMaskTrust, managed);
    if (state == d->state)
        return;

    setState(state);
    emit managedChanged(managed, QPrivateSignal());
}

// Controller interface -- notifications (logic side -> operation side).

/**
 * @brief Notify the player of the logic configuration.
 */
void Agent::notifyLogicConfiguration()
{
    emit logicConfigurationNotified(QPrivateSignal());
}

/**
 * @brief Notify the player that an agent's state changed.
 * @param playerName the internal name of the changed player
 * @param agentState the new state
 */
void Agent::notifyAgentStateChange(const QString &playerName, const QMdmmCore::Data::AgentState &agentState)
{
    emit agentStateChangeNotified(playerName, agentState, QPrivateSignal());
}

/**
 * @brief Notify the player that another player joined.
 * @param playerName the internal name of the added player
 * @param screenName the screen name of the added player
 * @param agentState the state of the added player
 */
void Agent::notifyPlayerAdd(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState)
{
    emit playerAddNotified(playerName, screenName, agentState, QPrivateSignal());
}

/**
 * @brief Notify the player that another player left.
 * @param playerName the internal name of the removed player
 */
void Agent::notifyPlayerRemove(const QString &playerName)
{
    emit playerRemoveNotified(playerName, QPrivateSignal());
}

/**
 * @brief Notify the player that the game started.
 */
void Agent::notifyGameStart()
{
    emit gameStartNotified(QPrivateSignal());
}

/**
 * @brief Notify the player that a round started.
 */
void Agent::notifyRoundStart()
{
    emit roundStartNotified(QPrivateSignal());
}

/**
 * @brief Notify the player of the RockPaperScissors replies of a round.
 * @param replies the RockPaperScissors choice of each player
 */
void Agent::notifyRockPaperScissors(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies)
{
    emit rockPaperScissorsNotified(replies, QPrivateSignal());
}

/**
 * @brief Notify the player of the action order of a round.
 * @param result the action order as a dense list: index @c i (0-based) holds the player taking
 *        order @c i + 1. Orders are always contiguous 1..N.
 */
void Agent::notifyActionOrder(const QStringList &result)
{
    emit actionOrderNotified(result, QPrivateSignal());
}

/**
 * @brief Notify the player of an action another player took.
 * @param playerName the internal name of the acting player
 * @param action the action taken
 * @param toPlayer the target player (for slash / kick / let-move)
 * @param toPlace the target place (for move / let-move)
 */
void Agent::notifyAction(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace)
{
    emit actionNotified(playerName, action, toPlayer, toPlace, QPrivateSignal());
}

/**
 * @brief Notify the player that the round is over.
 */
void Agent::notifyRoundOver()
{
    emit roundOverNotified(QPrivateSignal());
}

/**
 * @brief Notify the player of the upgrade choices of a round.
 * @param upgrades the upgrade items each player chose
 */
void Agent::notifyUpgrade(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades)
{
    emit upgradeNotified(upgrades, QPrivateSignal());
}

/**
 * @brief Notify the player that the game is over.
 * @param playerNames the winners
 */
void Agent::notifyGameOver(const QStringList &playerNames)
{
    emit gameOverNotified(playerNames, QPrivateSignal());
}

/**
 * @brief Notify the player that another player spoke.
 * @param playerName the internal name of the speaking player
 * @param content the spoken content
 */
void Agent::notifySpeak(const QString &playerName, const QString &content)
{
    emit speakNotified(playerName, content, QPrivateSignal());
}

/**
 * @brief Notify the player that another player operated.
 * @param playerName the internal name of the operating player
 * @param todo the operation (semantics not yet defined)
 */
void Agent::notifyOperate(const QString &playerName, const QJsonValue &todo)
{
    emit operateNotified(playerName, todo, QPrivateSignal());
}

// Controller interface -- requests (logic side -> operation side).

/**
 * @brief Request a Rock-Paper-Scissors choice.
 * @param playerNames the players involved in the Rock-Paper-Scissors
 * @param strivedOrder the action order being strived for (0 if not applicable)
 */
void Agent::requestRockPaperScissors(const QStringList &playerNames, int strivedOrder)
{
    emit rockPaperScissorsRequested(playerNames, strivedOrder, QPrivateSignal());
}

/**
 * @brief Request the desired action order.
 * @param remainedOrders the available (remained) orders
 * @param maximumOrder the total number of action orders
 * @param selectionNum the count of selections to make
 */
void Agent::requestActionOrder(const QList<int> &remainedOrders, int maximumOrder, int selectionNum)
{
    emit actionOrderRequested(remainedOrders, maximumOrder, selectionNum, QPrivateSignal());
}

/**
 * @brief Request an action.
 * @param currentOrder the current action order
 */
void Agent::requestAction(int currentOrder)
{
    emit actionRequested(currentOrder, QPrivateSignal());
}

/**
 * @brief Request an upgrade.
 * @param remainingTimes the remaining upgrade points
 */
void Agent::requestUpgrade(int remainingTimes)
{
    emit upgradeRequested(remainingTimes, QPrivateSignal());
}

// Controller interface -- replies and player actions (operation side -> logic side).

/**
 * @brief Reply with a Rock-Paper-Scissors choice.
 * @param rps the chosen Rock-Paper-Scissors
 */
void Agent::rockPaperScissors(QMdmmCore::Data::RockPaperScissors rps)
{
    emit replyRockPaperScissors(rps, QPrivateSignal());
}

/**
 * @brief Reply with the desired action order.
 * @param order the desired action order, one entry per requested selection: an order number
 * in range @c 1..maximumOrder to strive for it, or @c 0 to yield that selection (the player
 * accepts whatever order is left over and stops competing for it). Yielding is an explicit
 * reply, distinct from giveUpRequest(), which gives up and lets the server answer with its
 * default reply.
 */
void Agent::actionOrder(const QList<int> &order)
{
    emit replyActionOrder(order, QPrivateSignal());
}

/**
 * @brief Reply with an action.
 * @param act the action to make
 * @param toPlayer the target player name
 * @param toPlace the target place
 */
void Agent::action(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace)
{
    emit replyAction(act, toPlayer, toPlace, QPrivateSignal());
}

/**
 * @brief Reply with the upgrade choices.
 * @param items the list of upgrade items
 */
void Agent::upgrade(const QList<QMdmmCore::Data::UpgradeItem> &items)
{
    emit replyUpgrade(items, QPrivateSignal());
}

/**
 * @brief Give up on the current request (trigger the server's default reply).
 *
 * The operation side calls this instead of replying when it cannot answer the current request.
 * It forwards as the @c requestGivenUp signal, which the logic side (the client's connection)
 * turns into a "give up" wire reply.
 */
void Agent::giveUpRequest()
{
    emit requestGivenUp(QPrivateSignal());
}

/**
 * @brief The player speaks a message.
 * @param content the content of the message
 */
void Agent::speak(const QString &content)
{
    emit spoken(content, QPrivateSignal());
}

/**
 * @brief The player performs an operation.
 * @param todo the operation data (semantics not yet defined)
 */
void Agent::operate(const QJsonValue &todo)
{
    emit operated(todo, QPrivateSignal());
}

/**
 * @fn Agent::screenNameChanged(const QString &name, QPrivateSignal)
 * @brief notify signal for property @c screenName
 * @param name the new screen name
 */

/**
 * @fn Agent::stateChanged(QMdmmCore::Data::AgentState state, QPrivateSignal)
 * @brief notify signal for property @c state
 * @param state the new state
 */

/**
 * @fn Agent::managedChanged(bool managed, QPrivateSignal)
 * @brief emitted when the operation side declares a different managed state
 * @param managed the newly declared managed state
 */

/**
 * @fn Agent::logicConfigurationNotified(QPrivateSignal)
 * @brief emitted when the logic configuration is notified
 */

/**
 * @fn Agent::agentStateChangeNotified(const QString &playerName, const QMdmmCore::Data::AgentState &agentState, QPrivateSignal)
 * @brief emitted when an agent's state change is notified
 * @param playerName the changed player
 * @param agentState the new state
 */

/**
 * @fn Agent::playerAddNotified(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState, QPrivateSignal)
 * @brief emitted when a player join is notified
 * @param playerName the added player
 * @param screenName the added player's screen name
 * @param agentState the added player's state
 */

/**
 * @fn Agent::playerRemoveNotified(const QString &playerName, QPrivateSignal)
 * @brief emitted when a player leave is notified
 * @param playerName the removed player
 */

/**
 * @fn Agent::gameStartNotified(QPrivateSignal)
 * @brief emitted when the game start is notified
 */

/**
 * @fn Agent::roundStartNotified(QPrivateSignal)
 * @brief emitted when a round start is notified
 */

/**
 * @fn Agent::rockPaperScissorsNotified(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies, QPrivateSignal)
 * @brief emitted when the RockPaperScissors replies are notified
 * @param replies the choice of each player
 */

/**
 * @fn Agent::actionOrderNotified(const QStringList &result, QPrivateSignal)
 * @brief emitted when the action order is notified
 * @param result the action order as a dense list: index @c i (0-based) holds the player taking
 *        order @c i + 1. Orders are always contiguous 1..N.
 */

/**
 * @fn Agent::actionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace, QPrivateSignal)
 * @brief emitted when an action is notified
 * @param playerName the acting player
 * @param action the action taken
 * @param toPlayer the target player
 * @param toPlace the target place
 */

/**
 * @fn Agent::roundOverNotified(QPrivateSignal)
 * @brief emitted when the round over is notified
 */

/**
 * @fn Agent::upgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades, QPrivateSignal)
 * @brief emitted when the upgrade choices are notified
 * @param upgrades the upgrade items each player chose
 */

/**
 * @fn Agent::gameOverNotified(const QStringList &playerNames, QPrivateSignal)
 * @brief emitted when the game over is notified
 * @param playerNames the winners
 */

/**
 * @fn Agent::speakNotified(const QString &playerName, const QString &content, QPrivateSignal)
 * @brief emitted when another player's speech is notified
 * @param playerName the speaking player
 * @param content the spoken content
 */

/**
 * @fn Agent::operateNotified(const QString &playerName, const QJsonValue &todo, QPrivateSignal)
 * @brief emitted when another player's operation is notified
 * @param playerName the operating player
 * @param todo the operation
 */

/**
 * @fn Agent::rockPaperScissorsRequested(const QStringList &playerNames, int strivedOrder, QPrivateSignal)
 * @brief emitted when a Rock-Paper-Scissors choice is requested
 * @param playerNames the players involved in the Rock-Paper-Scissors
 * @param strivedOrder the action order being strived for
 */

/**
 * @fn Agent::actionOrderRequested(const QList<int> &remainedOrders, int maximumOrder, int selectionNum, QPrivateSignal)
 * @brief emitted when the desired action order is requested
 * @param remainedOrders the available (remained) orders
 * @param maximumOrder the total number of action orders
 * @param selectionNum the count of selections to make
 */

/**
 * @fn Agent::actionRequested(int currentOrder, QPrivateSignal)
 * @brief emitted when an action is requested
 * @param currentOrder the current action order
 */

/**
 * @fn Agent::upgradeRequested(int remainingTimes, QPrivateSignal)
 * @brief emitted when an upgrade is requested
 * @param remainingTimes the remaining upgrade points
 */

/**
 * @fn Agent::replyRockPaperScissors(QMdmmCore::Data::RockPaperScissors rps, QPrivateSignal)
 * @brief emitted when a Rock-Paper-Scissors reply is made
 * @param rps the chosen Rock-Paper-Scissors
 */

/**
 * @fn Agent::replyActionOrder(const QList<int> &order, QPrivateSignal)
 * @brief emitted when an action order reply is made
 * @param order the desired action order, where @c 0 entries yield that selection (accept the
 * assigned order) instead of striving for it
 */

/**
 * @fn Agent::replyAction(QMdmmCore::Data::Action act, const QString &toPlayer, int toPlace, QPrivateSignal)
 * @brief emitted when an action reply is made
 * @param act the action to make
 * @param toPlayer the target player name
 * @param toPlace the target place
 */

/**
 * @fn Agent::replyUpgrade(const QList<QMdmmCore::Data::UpgradeItem> &items, QPrivateSignal)
 * @brief emitted when an upgrade reply is made
 * @param items the list of upgrade items
 */

/**
 * @fn Agent::requestGivenUp(QPrivateSignal)
 * @brief emitted when the operation side gives up on the current request
 */

/**
 * @fn Agent::spoken(const QString &content, QPrivateSignal)
 * @brief emitted when the player speaks
 * @param content the content of the message
 */

/**
 * @fn Agent::operated(const QJsonValue &todo, QPrivateSignal)
 * @brief emitted when the player operates
 * @param todo the operation data
 */

#ifndef DOXYGEN
} // namespace v0
#endif
} // namespace QMdmmNetworking

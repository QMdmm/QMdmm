// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmlogic.h"
#include "qmdmmlogic_p.h"
#include "qmdmmplayer.h"
#include "qmdmmroom.h"

#include <QDebug>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QMultiHash>

#include <algorithm>

/**
 * @file qmdmmlogic.h
 * @brief This is the file where MDMM Game logic is defined.
 */

namespace QMdmmCore {
#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class Logic
 * @ingroup QMdmmCore
 * @brief The MDMM Game logic.
 *
 * This is the place where logic is run.
 * The logic is basically a state machine, where the states are changed based on the game process.
 *
 * A typical MDMM game run is like following: (ref. @c Logic::State)
 *
 * <table>
 * <thead>
 * <tr>
 * <th>State
 * <th>Definition
 * <th>Accepted Slots
 * <th>Notes
 * </tr>
 * </thead>
 * <tbody>
 * <tr>
 * <td>@c Logic::BeforeRoundStart
 * <td>The game / round has not yet started yet. Typically due to waiting for preparation of agents.
 * <td>@c Logic::addPlayer() @c Logic::removePlayer() @c Logic::roundStart()
 * <td>When @c Logic::addPlayer() or @c Logic::removePlayer() is called, all the upgrades are cleared.
 * </tr>
 * <tr>
 * <td>@c Logic::RpsForAction
 * <td>Rock-Paper-Scissors is requested for actions. This is always the first request per action time.
 * <td>@c Logic::rpsReply()
 * <td>@c Logic::requestRpsForAction() is emitted when this state enters, @c Logic::rpsResult() is emitted when this state exits to report the result.<br />
 *     If result is a tie @c Logic::RpsForAction re-enters. The number of retries is
 *     not bounded and no deterministic fallback follows a given number of them: a tie
 *     is inherent to Rock-Paper-Scissors, so capping the retries would not make it any
 *     less likely. Consequently, if all alive players keep answering with the same
 *     throw, this state is never left (known limitation).<br />
 *     If there is only one winner, he / she gets all the action orders and @c Logic::Action enters.<br />
 *     Else @c Logic::ActionOrder enters.
 * </tr>
 * <tr>
 * <td>@c Logic::ActionOrder
 * <td>If multiple winners has determined in the @c Logic::RpsForAction state, then in this action time the winners should determine the action order.
 * <td>@c Logic::actionOrderReply()
 * <td>@c Logic::requestActionOrder() is emitted when this state enters.<br />
 *     If multiple agents chose same action order, @c Logic::RpsForActionOrder enters.<br />
 *     Else the actions are got by the choosers, @c Logic::actionOrderResult() is emitted and @c Logic::Action enters.
 * </tr>
 * <tr>
 * <td>@c Logic::RpsForActionOrder
 * <td>If multiple winners chose same action order, request Rock-Paper-Scissors to determine who can actually get the action order.
 * <td>@c Logic::rpsReply()
 * <td>@c Logic::requestRpsForActionOrder() is emitted when this state enters, @c Logic::rpsResult() is emitted when this state exits to report the result.<br />
 *     If result is a tie @c Logic::RpsForActionOrder re-enters.<br />
 *     If the result is not a tie, but there are multiple winners, @c Logic::RpsForActionOrder re-enters with only winners are requested.<br />
 *     Else the action order is got by the winner. Then if there are still undetermined action orders, @c Logic::ActionOrder enters, else @c Logic::actionOrderResult() is emitted and @c Logic::Action enters.
 * </tr>
 * <tr>
 * <td>@c Logic::Action
 * <td>Request action then apply the action.
 * <td>@c Logic::actionReply()
 * <td>When a player die due to an action before his action order, then his action is skipped.<br />
 *     @c Logic::requestAction() is emitted when this state enters, @c Logic::actionResult() is emitted then action is applied when this state exits.<br />
 *     If there is less than 1 player alive after an action, the remained actions are discarded, @c Logic::roundOver() is emitted and round overs.<br />
 * </tr>
 * <tr>
 * <td>@c Logic::Upgrade
 * <td>Request upgrade then apply the upgrade.
 * <td>@c Logic::upgradeReply()
 * <td>@c Logic::requestUpgrade() is emitted when this state enters, @c Logic::upgradeResult() is emitted when this state exits to report the result.<br />
 *     When all upgradable properties reaches maximum value, @c Logic::gameOver() is emitted and game overs.
 * </tr>
 * </tbody>
 * </table>
 *
 * @note The Logic is a synchronous, reply-driven state machine: each
 * @c requestXxx signal expects a matching @c xxxReply() slot call, and that
 * reply advances the state machine (possibly emitting further requests) within
 * the same call stack. Some requests are emitted in a loop over players -- for
 * example @c requestUpgrade() is emitted once per player with upgrade points --
 * so a directly-connected slot replies re-entrantly while the loop is still
 * running. This is safe by design: each phase only advances once every
 * expected reply has been collected (the reply handlers count replies and act
 * only when the set is complete), and the emit loops iterate over stable
 * containers that the re-entrant advance does not mutate. A direct connection
 * in a single-threaded setup is therefore equivalent to a queued connection;
 * no particular connection type is required. (The reference implementation in
 * QMdmmNetworking runs Logic on its own thread with queued connections purely
 * for thread marshaling, not because direct connection is unsafe.)
 */

/**
 * @enum Logic::State
 * @ingroup QMdmmCore
 * @brief The state of the current game
 *
 * @sa @c QMdmmLogic
 */

/**
 * @var Logic::State Logic::BeforeRoundStart
 * @brief The game / round has not yet started yet. Typically due to waiting for preparation of agents.
 *
 * @var Logic::State Logic::RpsForAction
 * @brief Rock-Paper-Scissors is requested for actions. This is always the first request per action time.
 *
 * @var Logic::State Logic::ActionOrder
 * @brief If multiple winners has determined in the @c Logic::RpsForAction state, then in this action time the winners should determine the action order.
 *
 * @var Logic::State Logic::RpsForActionOrder
 * @brief If multiple winners chose same action order, request Rock-Paper-Scissors to determine who can actually get the action order.
 *
 * @var Logic::State Logic::Action
 * @brief Request action then apply the action.
 *
 * @var Logic::State Logic::Upgrade
 * @brief Request upgrade then apply the upgrade.
 */

/**
 * @brief ctor.
 * @param logicConfiguration The configuration of this logic. Usually initialized by Server.
 * @param parent QObject parent.
 */
Logic::Logic(const LogicConfiguration &logicConfiguration, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<p::LogicP>(logicConfiguration, this))
{
}

/**
 * @brief dtor.
 */
Logic::~Logic() = default;

/**
 * @brief Return current state of the logic.
 * @return the current state.
 */
Logic::State Logic::state() const noexcept
{
    return d->state;
}

/**
 * @brief Add a player to the logic
 * @param playerName the internal name of the player
 * @return true if state matches and operation succeeded (or there are no action), otherwise false
 *
 * Can be only called from @c Logic::BeforeRoundStart state.
 */
bool Logic::addPlayer(const QString &playerName)
{
    if (d->state == BeforeRoundStart) {
        if (d->room->addPlayer(playerName) != nullptr) {
            d->room->resetUpgrades();

            return true;
        }
    }

    return false;
}

/**
 * @brief Remove a player from the logic
 * @param playerName the internal name of the player
 * @return true if state matches and operation succeeded (or there are no action), otherwise false
 *
 * Can be only called from @c Logic::BeforeRoundStart state.
 */
bool Logic::removePlayer(const QString &playerName)
{
    if (d->state == BeforeRoundStart) {
        if (d->room->removePlayer(playerName)) {
            d->room->resetUpgrades();

            return true;
        }
    }

    return false;
}

/**
 * @brief Round start
 * @return true if state matches and operation succeeded (or there are no action), otherwise false
 *
 * Can be only called from @c Logic::BeforeRoundStart state.
 */
bool Logic::roundStart()
{
    if (d->state == BeforeRoundStart) {
        // a game must be started for player number >= 2
        if (d->room->playerNames().length() >= 2) {
            d->room->prepareForRoundStart();
            d->startRpsForAction();

            return true;
        }
    }

    return false;
}

/**
 * @brief Receive Rock-Paper-Scissors result
 * @param playerName the internal name of the player
 * @param rps the Rock-Paper-Scissors of the player
 * @return true if state matches and operation succeeded (or there are no action), otherwise false
 *
 * Can be only called from @c Logic::RpsForAction or @c Logic::RpsForActionOrder state.
 */
bool Logic::rpsReply(const QString &playerName, Data::RockPaperScissors rps)
{
    if (d->room->player(playerName) != nullptr) {
        if (d->state == RpsForAction) {
            if (!d->rpsForActionReplies.contains(playerName)) {
                d->rpsForActionReplies.insert(playerName, rps);
                d->rpsForAction();

                return true;
            }
        }

        if (d->state == RpsForActionOrder) {
            if (!d->rpsForActionOrderReplies.contains(playerName)) {
                d->rpsForActionOrderReplies.insert(playerName, rps);
                d->rpsForActionOrder();

                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Receive the desired action order result
 * @param playerName the internal name of the player
 * @param desiredOrder the desired action order of the player
 * @return true if the reply was accepted and applied as sent; false if it was
 *         rejected (unknown player, wrong state, or duplicate) or replaced by a
 *         full yield because the order list is invalid (the state machine still
 *         advances)
 *
 * Can be only called from @c Logic::ActionOrder state.
 *
 * The reply must contain exactly as many entries as the selections that were
 * requested from the player. Each entry is either an order the player wants to
 * strive for (in range @c 1..maximumOrderNum, not already confirmed, and not
 * duplicated) or @c 0 to yield that action opportunity -- the player accepts
 * whatever order is left over and stops competing for it. An invalid reply is
 * not rejected: it falls back to yielding every selection, so a buggy or
 * malicious agent cannot stall the ActionOrder phase.
 */
bool Logic::actionOrderReply(const QString &playerName, const QList<int> &desiredOrder)
{
    if (d->room->player(playerName) != nullptr) {
        if (d->state == ActionOrder) {
            const int selections = d->actionOrderRemainingSelections.value(playerName, -1);
            if (selections >= 0) {
                const int maximumOrderNum = static_cast<int>(d->rpsForActionWinners.length());
                QList<int> chosenOrders;
                int yields = 0;
                bool accepted = desiredOrder.length() == selections;

                if (accepted) {
                    foreach (int order, desiredOrder) {
                        if (order == 0) {
                            ++yields;
                        } else if (order >= 1 && order <= maximumOrderNum && !d->confirmedActionOrders.contains(order) && !chosenOrders.contains(order)) {
                            chosenOrders << order;
                        } else {
                            accepted = false;
                            break;
                        }
                    }
                }

                if (!accepted) {
                    // QMdmmBot's whole-match case fails the run on an illegal reply by matching this
                    // text, so its wording and its log level are an interface: tst_qmdmmlogic.cpp
                    // asserts the same text, and is where a reword shows up.
                    qWarning() << "Logic::actionOrderReply: player" << playerName << "sent an invalid order list; yielding all" << selections << "selections";
                    chosenOrders.clear();
                    yields = selections;
                }

                foreach (int order, chosenOrders)
                    d->desiredActionOrders.insert(order, playerName);
                if (yields > 0)
                    d->actionOrderYields[playerName] += yields;
                d->actionOrderRemainingSelections.remove(playerName);
                d->actionOrder();

                return accepted;
            }
        }
    }
    return false;
}

/**
 * @brief Receive the action performed by a player
 * @param playerName the internal name of the player
 * @param action the action to do by the player
 * @param toPlayer the internal name of the target player
 * @param toPlace the target place
 * @return true if the reply was accepted and applied as sent; false if it was
 *         rejected (unknown player or wrong state) or replaced by DoNothing
 *         because the action is infeasible (the state machine still advances)
 *
 * Can be only called from @c Logic::Action state.
 */
bool Logic::actionReply(const QString &playerName, Data::Action action, const QString &toPlayer, int toPlace)
{
    if (d->room->player(playerName) != nullptr) {
        if (d->state == Action) {
            const bool accepted = d->actionFeasible(playerName, action, toPlayer, toPlace);
            if (!accepted) {
                // QMdmmBot's whole-match case fails the run on an illegal reply by matching this
                // text, so its wording and its log level are an interface: tst_qmdmmlogic.cpp
                // asserts the same text, and is where a reword shows up.
                qWarning() << "Logic::actionReply: player" << playerName << "sent infeasible action" << static_cast<int>(action) << "; falling back to DoNothing";
                action = Data::DoNothing;
            }

            d->applyAction(playerName, action, toPlayer, toPlace);
            d->startAction();

            return accepted;
        }
    }

    return false;
}

/**
 * @brief Receive the upgrade items of a player
 * @param playerName the internal name of the player
 * @param items the upgrade items
 * @return true if the reply was accepted and applied as sent; false if it was
 *         rejected (unknown player, wrong state, or duplicate) or replaced by a
 *         feasible default because the item list is infeasible (the state
 *         machine still advances)
 *
 * Can be only called from @c Logic::Upgrade state.
 */
bool Logic::upgradeReply(const QString &playerName, const QList<Data::UpgradeItem> &items)
{
    const Player *p = d->room->player(playerName);
    if (p != nullptr) {
        if (d->state == Upgrade) {
            if (!d->upgrades.contains(playerName)) {
                const bool accepted = d->upgradeFeasible(playerName, items);
                if (accepted) {
                    d->upgrades.insert(playerName, items);
                } else {
                    // QMdmmBot's whole-match case fails the run on an illegal reply by matching this
                    // text, so its wording and its log level are an interface: tst_qmdmmlogic.cpp
                    // asserts the same text, and is where a reword shows up.
                    qWarning() << "Logic::upgradeReply: player" << playerName << "sent an infeasible upgrade list; falling back to a feasible default";
                    // The server has no access to a player's remaining upgrade counts, so it
                    // cannot build a feasible list. Build a feasible default here: spend every
                    // point, knife damage first.
                    QList<Data::UpgradeItem> feasibleItems;
                    int pointLeft = p->upgradePoint();
                    const auto take = [&feasibleItems, &pointLeft](Data::UpgradeItem item, int remaining) {
                        const int n = std::min(pointLeft, remaining);
                        for (int i = 0; i < n; ++i)
                            feasibleItems << item;
                        pointLeft -= n;
                    };
                    take(Data::UpgradeKnife, p->upgradeKnifeRemainingTimes());
                    take(Data::UpgradeHorse, p->upgradeHorseRemainingTimes());
                    take(Data::UpgradeMaxHp, p->upgradeMaxHpRemainingTimes());
                    d->upgrades.insert(playerName, feasibleItems);
                }

                d->upgrade();
                return accepted;
            }
        }
    }

    return false;
}

/**
 * @fn Logic::requestRpsForAction(const QStringList &playerNames, QPrivateSignal)
 * @brief emits when Rock-Paper-Scissors is requested for actions
 * @param playerNames the requested player names, without dead players
 *
 * Can be only emitted in @c Logic::RpsForAction state.
 */

/**
 * @fn Logic::rpsResult(const QHash<QString, QMdmmData::RockPaperScissors> &replies, QPrivateSignal)
 * @brief emits when Rock-Paper-Scissors is all replied
 * @param replies the replies of Rock-Paper-Scissors (key = internal name of player, value = Rock-Paper-Scissors)
 *
 * Can be only emitted in @c Logic::RpsForAction and @c Logic::RpsForActionOrder state.
 *
 * Reports the completed selection, not a completed execution: it is emitted once
 * every requested player has replied, before the winners are computed and the
 * round proceeds.
 */

/**
 * @fn Logic::requestActionOrder(const QString &playerName, const QList<int> &availableOrders, int maximumOrderNum, int selections, QPrivateSignal)
 * @brief emits when desired action orders are requested
 * @param playerName one of the requested player names
 * @param availableOrders the available (remained) orders
 * @param maximumOrderNum total number of action orders
 * @param selections remained count of selections
 *
 * Can be only emitted in @c Logic::ActionOrder state.
 */

/**
 * @fn Logic::actionOrderResult(const QHash<int, QString> &result, QPrivateSignal)
 * @brief emits when action order is confirmed
 * @param result the result of action orders. (key = action order, value = internal name of player)
 *
 * Can be only emitted in @c Logic::ActionOrder state.
 *
 * Reports the completed selection, not a completed execution: it is emitted once
 * the action order is finalized, before @c Logic::Action state enters and the
 * actions are performed.
 */

/**
 * @fn Logic::requestRpsForActionOrder(const QStringList &playerNames, int strivedOrder, QPrivateSignal)
 * @brief emits when Rock-Paper-Scissors is requested for striving for a specific action order
 * @param playerNames the requested player names with same desired action order
 * @param strivedOrder the strived action order
 *
 * Can be only emitted in @c Logic::RpsForActionOrder state.
 */

/**
 * @fn Logic::requestAction(const QString &playerName, int actionOrder, QPrivateSignal)
 * @brief emits when a action should be made
 * @param playerName the requested player name
 * @param actionOrder current action order
 *
 * Can be only emitted in @c Logic::Action state.
 */

/**
 * @fn Logic::actionResult(const QString &playerName, QMdmmData::Action action, const QString &toPlayer, int toPlace, QPrivateSignal)
 * @brief emits when action is confirmed for current action order
 * @param playerName the requested player name
 * @param action the action to be made
 * @param toPlayer the target player name
 * @param toPlace the target place
 *
 * Can be only emitted in @c Logic::Action state.
 *
 * Reports the completed selection, not a completed execution: it is emitted
 * before the action is actually applied (the effect, e.g. slash/kick/move, is
 * applied after this signal). Emitting before applying is the intended order, so
 * subscribers observe the choice before its effect takes place.
 */

/**
 * @fn Logic::roundOver(QPrivateSignal)
 * @brief emits when round is over
 *
 * Can be only emitted in @c Logic::Action state.
 */

/**
 * @fn Logic::requestUpgrade(const QString &playerName, int upgradePoint, QPrivateSignal)
 * @brief emits when round overs and there is upgrade point remaining for a player
 * @param playerName the requested player
 * @param upgradePoint the remaining point for upgrading
 *
 * Can be only emitted in @c Logic::Upgrade state.
 */

/**
 * @fn Logic::upgradeResult(const QHash<QString, QList<QMdmmData::UpgradeItem>> &upgrades, QPrivateSignal);
 * @brief emits when upgrade has confirmed
 * @param upgrades the upgrades performed by each player (key: the internal name of player, value: list of upgrade items)
 *
 * Can be only emitted in @c Logic::Upgrade state.
 *
 * Reports the completed selection of upgrades for this round.
 */

/**
 * @fn Logic::gameOver(const QStringList &playerNames, QPrivateSignal)
 * @brief emits when game is over
 * @param playerNames the internal names of the winners
 *
 * Can be only emitted in @c Logic::Upgrade state.
 */

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore

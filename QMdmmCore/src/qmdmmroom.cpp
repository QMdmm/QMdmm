// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmroom.h"
#include "qmdmmroom_p.h"

#include "qmdmmlogic.h"
#include "qmdmmplayer.h"

#include <QString>

#include <cmath>
#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

/**
 * @file qmdmmroom.h
 * @brief Contains definitions of room.
 */

namespace QMdmmCore {

#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class LogicConfiguration
 * @ingroup QMdmmCore
 * @brief Contains configurations of logic
 *
 * A set of game-rule parameters (knife / horse damage, maximum HP, punish HP and a few behavior toggles)
 * stored as a @c QJsonObject, one key per field. It is sent to clients as @c NotifyLogicConfiguration and
 * parsed on the receiving side by @c LogicConfiguration::deserialize(). Two canned presets are provided:
 * @c LogicConfiguration::defaults() matches MDMM Version 2 rules and @c LogicConfiguration::v1() matches the
 * legacy Version 1 rules.
 */

/**
 * @enum LogicConfiguration::PunishHpRoundStrategy
 * @ingroup QMdmmCore
 * @brief The round strategy of punishing HP
 *
 * Punish HP is taking place when a player slashes others in city. By default a half of maximum HP is lost. <br />
 * Since HP is integer value one can not have a ".5" stuff or something, so rounding of the calculated half is needed.
 * The modifier can be configured individually, so "a third" or "a quarter" etc. are also configurable
 *
 * Currently there is 4 different rounding strategy supported. Following table is sort by understandability.
 *
 * | Strategy name | Description | Examples |
 * |-|-|-|
 * | @c LogicConfiguration::RoundDown | Round all number down, i.e. digits after the dot are stripped. | 1.1 -> 1, 1.4 -> 1, 1.5 -> 1, 1.9 -> 1, 2.0 -> 2 |
 * | @c LogicConfiguration::PlusOne | Apply +1 after round all number down. | 1.1 -> 2, 1.4 -> 2, 1.5 -> 2, 1.9 -> 2, 2.0 -> 3 |
 * | @c LogicConfiguration::RoundUp | Round all number up, i.e. plus one if there are digits after the dot, then digits after the dot are stripped. | 1.1 -> 2, 1.4 -> 2, 1.5 -> 2, 1.9 -> 2, 2.0 -> 2 |
 * | @c LogicConfiguration::RoundToNearest45 | Round all number to nearest integer. Round down if the first digit after dot is <= 4, round up else. | 1.1 -> 1, 1.4 -> 1, 1.5 -> 2, 1.9 -> 2, 2.0 -> 2 |
 */

/**
 * @var LogicConfiguration::PunishHpRoundStrategy LogicConfiguration::RoundDown
 * Round all number down, i.e. digits after the dot are stripped. See table above.
 *
 * @var LogicConfiguration::PunishHpRoundStrategy LogicConfiguration::PlusOne
 * Apply +1 after round all number down. See table above.
 *
 * @var LogicConfiguration::PunishHpRoundStrategy LogicConfiguration::RoundUp
 * Round all number up, i.e. plus one if there are digits after the dot, then digits after the dot are stripped. See table above.
 *
 * @var LogicConfiguration::PunishHpRoundStrategy LogicConfiguration::RoundToNearest45
 * Round all number to nearest integer. Round down if the first digit after dot is <= 4, round up else. See table above.
 */

/**
 * @property LogicConfiguration::initialKnifeDamage
 * @brief the "knife damage" when game starts, default 1
 */

/**
 * @fn LogicConfiguration::initialKnifeDamage() const
 * @brief getter of @c LogicConfiguration::initialKnifeDamage
 * @return @c LogicConfiguration::initialKnifeDamage
 */

/**
 * @fn LogicConfiguration::setInitialKnifeDamage(int initialKnifeDamage)
 * @brief setter of @c LogicConfiguration::initialKnifeDamage
 * @param initialKnifeDamage @c LogicConfiguration::initialKnifeDamage
 */

/**
 * @property LogicConfiguration::maximumKnifeDamage
 * @brief the maximum "knife damage" one can upgrade to, default 10
 */

/**
 * @fn LogicConfiguration::maximumKnifeDamage() const
 * @brief getter of @c LogicConfiguration::maximumKnifeDamage
 * @return @c LogicConfiguration::maximumKnifeDamage
 */

/**
 * @fn LogicConfiguration::setMaximumKnifeDamage(int maximumKnifeDamage)
 * @brief setter of @c LogicConfiguration::maximumKnifeDamage
 * @param maximumKnifeDamage @c LogicConfiguration::maximumKnifeDamage
 */

/**
 * @property LogicConfiguration::initialHorseDamage
 * @brief the "horse damage" when game starts, default 2
 */

/**
 * @fn LogicConfiguration::initialHorseDamage() const
 * @brief getter of @c LogicConfiguration::initialHorseDamage
 * @return @c LogicConfiguration::initialHorseDamage
 */

/**
 * @fn LogicConfiguration::setInitialHorseDamage(int initialHorseDamage)
 * @brief setter of @c LogicConfiguration::initialHorseDamage
 * @param initialHorseDamage @c LogicConfiguration::initialHorseDamage
 */

/**
 * @property LogicConfiguration::maximumHorseDamage
 * @brief the maximum "horse damage" one can upgrade to, default 10
 */

/**
 * @fn LogicConfiguration::maximumHorseDamage() const
 * @brief getter of @c LogicConfiguration::maximumHorseDamage
 * @return @c LogicConfiguration::maximumHorseDamage
 */

/**
 * @fn LogicConfiguration::setMaximumHorseDamage(int maximumHorseDamage)
 * @brief setter of @c LogicConfiguration::maximumHorseDamage
 * @param maximumHorseDamage @c LogicConfiguration::maximumHorseDamage
 */

/**
 * @property LogicConfiguration::initialMaxHp
 * @brief the "maximum HP" when game starts, default 10
 */

/**
 * @fn LogicConfiguration::initialMaxHp() const
 * @brief getter of @c LogicConfiguration::initialMaxHp
 * @return @c LogicConfiguration::initialMaxHp
 */

/**
 * @fn LogicConfiguration::setInitialMaxHp(int initialMaxHp)
 * @brief setter of @c LogicConfiguration::initialMaxHp
 * @param initialMaxHp @c LogicConfiguration::initialMaxHp
 */

/**
 * @property LogicConfiguration::maximumMaxHp
 * @brief the maximum "maximum HP" one can upgrade to, default 20
 * @note This seems hard to understand. HP is reset to "maximum HP" when round starts and this "maximum HP" is upgradable. Any upgradable data have a maximum value so there is maximum "maximum HP".
 */

/**
 * @fn LogicConfiguration::maximumMaxHp() const
 * @brief getter of @c LogicConfiguration::maximumMaxHp
 * @return @c LogicConfiguration::maximumMaxHp
 */

/**
 * @fn LogicConfiguration::setMaximumMaxHp(int maximumMaxHp)
 * @brief setter of @c LogicConfiguration::maximumMaxHp
 * @param maximumMaxHp @c LogicConfiguration::maximumMaxHp
 */

/**
 * @property LogicConfiguration::punishHpModifier
 * @brief The modifier of punish HP. Set it to 0 to disable punish HP, default 2
 */

/**
 * @fn LogicConfiguration::punishHpModifier() const
 * @brief getter of @c LogicConfiguration::punishHpModifier
 * @return @c LogicConfiguration::punishHpModifier
 */

/**
 * @fn LogicConfiguration::setPunishHpModifier(int punishHpModifier)
 * @brief setter of @c LogicConfiguration::punishHpModifier
 * @param punishHpModifier @c LogicConfiguration::punishHpModifier
 */

/**
 * @property LogicConfiguration::punishHpRoundStrategy
 * @brief Punish HP round strategy, use with @c LogicConfiguration::punishHpModifier, default @c LogicConfiguration::RoundToNearest45
 * @sa @c LogicConfiguration::PunishHpRoundStrategy
 */

/**
 * @fn LogicConfiguration::punishHpRoundStrategy() const
 * @brief getter of @c LogicConfiguration::punishHpRoundStrategy
 * @return @c LogicConfiguration::punishHpRoundStrategy
 */

/**
 * @fn LogicConfiguration::setPunishHpRoundStrategy(LogicConfiguration::PunishHpRoundStrategy punishHpRoundStrategy)
 * @brief setter of @c LogicConfiguration::punishHpRoundStrategy
 * @param punishHpRoundStrategy @c LogicConfiguration::punishHpRoundStrategy
 */

/**
 * @property LogicConfiguration::zeroHpAsDead
 * @brief Treat one with 0 hp as dead. default true
 */

/**
 * @fn LogicConfiguration::zeroHpAsDead() const
 * @brief getter of @c LogicConfiguration::zeroHpAsDead
 * @return @c LogicConfiguration::zeroHpAsDead
 */

/**
 * @fn LogicConfiguration::setZeroHpAsDead(bool zeroHpAsDead)
 * @brief setter of @c LogicConfiguration::zeroHpAsDead
 * @param zeroHpAsDead @c LogicConfiguration::zeroHpAsDead
 */

/**
 * @property LogicConfiguration::enableLetMove
 * @brief Enables "let move" game mechanism. default true
 */

/**
 * @fn LogicConfiguration::enableLetMove() const
 * @brief getter of @c LogicConfiguration::enableLetMove
 * @return @c LogicConfiguration::enableLetMove
 */

/**
 * @fn LogicConfiguration::setEnableLetMove(bool enableLetMove)
 * @brief setter of @c LogicConfiguration::enableLetMove
 * @param enableLetMove @c LogicConfiguration::enableLetMove
 */

/**
 * @property LogicConfiguration::canBuyOnlyInInitialCity
 * @brief Can buy knife / horse only in initial city. default false
 * @note even if this is false, one cannot buy things in the Village.
 */

/**
 * @fn LogicConfiguration::canBuyOnlyInInitialCity() const
 * @brief getter of @c LogicConfiguration::canBuyOnlyInInitialCity
 * @return @c LogicConfiguration::canBuyOnlyInInitialCity
 */

/**
 * @fn LogicConfiguration::setCanBuyOnlyInInitialCity(bool canBuyOnlyInInitialCity)
 * @brief setter of @c LogicConfiguration::canBuyOnlyInInitialCity
 * @param canBuyOnlyInInitialCity @c LogicConfiguration::canBuyOnlyInInitialCity
 */

/**
 * @brief Get default values of configuration
 * @return default configuration
 *
 * I had experienced 2 versions of MDMM, they are mostly same but with minor differences. <br />
 * In both versions one can only buy k/h in city. <br />
 * In both versions one can slash only if knife is bought and kick only if horse is bought. <br />
 * In both versions one can only slash / kick other one when they are at same place. <br />
 * In both versions one can kick other one in city, and the kicked one will be force moved to the Village. <br />
 * In both versions one can only move to adjacent place at a time. Different cities are not adjacent, while the Village is adjacent to every city. <br />
 *
 * Version 1 (Legacy): initial 7/1/3 mh/kd/hd, maximum 7/3/5 mh/kd/hd (Yeah, no maxHp upgrade). One with zero HP is still alive. No "Let move"s, no punish HP. <br />
 * This is the version I had experienced in primary school. <br />
 * This version knife upgrades are more valuable. Horse is of less value since there is no HP punish.
 *
 * Version 2: initial 10/1/2 mh/kd/hd, maximum 20/10/10 mh/kd/hd. One with zero HP dies. With "Let move"s (pull sb. in / push sb. out of city stuff), with punish HP. <br />
 * This is the version I had experienced in junior high school. <br />
 * This version horse can be effectively used in multi-player. pull - kick loop is fun!
 *
 * Default configurations matches rules of Version 2 and can be tweaked.
 */
const LogicConfiguration &LogicConfiguration::defaults()
{
    // clang-format off
    static const LogicConfiguration defaultInstance {
        qMakePair(u"initialKnifeDamage"_s, 1),
        qMakePair(u"maximumKnifeDamage"_s, 10),
        qMakePair(u"initialHorseDamage"_s, 2),
        qMakePair(u"maximumHorseDamage"_s, 10),
        qMakePair(u"initialMaxHp"_s, 10),
        qMakePair(u"maximumMaxHp"_s, 20),
        qMakePair(u"punishHpModifier"_s, 2),
        qMakePair(u"punishHpRoundStrategy"_s, static_cast<int>(RoundToNearest45)),
        qMakePair(u"zeroHpAsDead"_s, true),
        qMakePair(u"enableLetMove"_s, true),
        qMakePair(u"canBuyOnlyInInitialCity"_s, false),
    };
    // clang-format on

    return defaultInstance;
}

/**
 * @brief Get Version 1 configuration
 * @return Version 1 configuration
 *
 * @sa @c LogicConfiguration::defaults()
 */
const LogicConfiguration &LogicConfiguration::v1()
{
    // clang-format off
    static const LogicConfiguration defaultInstance {
        qMakePair(u"initialKnifeDamage"_s, 1),
        qMakePair(u"maximumKnifeDamage"_s, 3),
        qMakePair(u"initialHorseDamage"_s, 3),
        qMakePair(u"maximumHorseDamage"_s, 5),
        qMakePair(u"initialMaxHp"_s, 7),
        qMakePair(u"maximumMaxHp"_s, 7),
        qMakePair(u"punishHpModifier"_s, 0),
        qMakePair(u"punishHpRoundStrategy"_s, static_cast<int>(RoundToNearest45)),
        qMakePair(u"zeroHpAsDead"_s, false),
        qMakePair(u"enableLetMove"_s, false),
        qMakePair(u"canBuyOnlyInInitialCity"_s, false),
    };
    // clang-format on

    return defaultInstance;
}

// NOLINTBEGIN(bugprone-macro-parentheses,cppcoreguidelines-macro-usage)

#define CONVERTTOTYPEBOOL(v) v.toBool()
#define CONVERTTOTYPEINT(v) v.toInt()
#define CONVERTTOTYPEPUNISHHPROUNDSTRATEGY(v) static_cast<LogicConfiguration::PunishHpRoundStrategy>(v.toInt())
#define IMPLEMENTATION_CONFIGURATION(type, valueName, ValueName, convertToType, convertToJsonValue) \
    type LogicConfiguration::valueName() const                                                      \
    {                                                                                               \
        if (contains(u"" #valueName ""_s))                                                          \
            return convertToType(value(u"" #valueName ""_s));                                       \
        return convertToType(defaults().value(u"" #valueName ""_s));                                \
    }                                                                                               \
    void LogicConfiguration::set##ValueName(type valueName)                                         \
    {                                                                                               \
        insert(u"" #valueName ""_s, convertToJsonValue(valueName));                                 \
    }

// NOLINTEND(bugprone-macro-parentheses,cppcoreguidelines-macro-usage)

IMPLEMENTATION_CONFIGURATION(int, initialKnifeDamage, InitialKnifeDamage, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, maximumKnifeDamage, MaximumKnifeDamage, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, initialHorseDamage, InitialHorseDamage, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, maximumHorseDamage, MaximumHorseDamage, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, initialMaxHp, InitialMaxHp, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, maximumMaxHp, MaximumMaxHp, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, punishHpModifier, PunishHpModifier, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(LogicConfiguration::PunishHpRoundStrategy, punishHpRoundStrategy, PunishHpRoundStrategy, CONVERTTOTYPEPUNISHHPROUNDSTRATEGY, static_cast<int>)
IMPLEMENTATION_CONFIGURATION(bool, zeroHpAsDead, ZeroHpAsDead, CONVERTTOTYPEBOOL, )
IMPLEMENTATION_CONFIGURATION(bool, enableLetMove, EnableLetMove, CONVERTTOTYPEBOOL, )
IMPLEMENTATION_CONFIGURATION(bool, canBuyOnlyInInitialCity, CanBuyOnlyInInitialCity, CONVERTTOTYPEBOOL, )

#undef IMPLEMENTATION_CONFIGURATION
#undef CONVERTTOTYPEPUNISHHPROUNDSTRATEGY
#undef CONVERTTOTYPEINT
#undef CONVERTTOTYPEBOOL

/**
 * @brief deserialize @c QJsonValue to @c LogicConfiguration
 * @param value the value to be deserialized
 * @return if the deserialize succeeded.
 * @note It is possible to convert the value to @c QJsonObject and directly assign the value, since this class inherits @c QJsonObject, but the value check in this function will be nonexistent then.
 *
 * The value must be an object. Any absent key falls back to its default value (as returned by
 * @c defaults()), so a partial or empty object is accepted; a present key must still be valid. Each
 * numeric field must be a non-negative whole number (fractions, NaN and negative values are rejected);
 * @c punishHpRoundStrategy must be a valid @c PunishHpRoundStrategy; and each "initial" value must not
 * exceed its "maximum" counterpart (e.g. @c initialKnifeDamage <= @c maximumKnifeDamage).
 */
bool LogicConfiguration::deserialize(const QJsonValue &value) // NOLINT(readability-function-cognitive-complexity)
{
    if (!value.isObject())
        return false;

    const QJsonObject ob = value.toObject();
    QJsonObject result;

    int initialKnifeDamage = defaults().initialKnifeDamage();
    int maximumKnifeDamage = defaults().maximumKnifeDamage();
    int initialHorseDamage = defaults().initialHorseDamage();
    int maximumHorseDamage = defaults().maximumHorseDamage();
    int initialMaxHp = defaults().initialMaxHp();
    int maximumMaxHp = defaults().maximumMaxHp();
    int punishHpModifier = defaults().punishHpModifier();
    int punishHpRoundStrategy = static_cast<int>(defaults().punishHpRoundStrategy());

    // A numeric field must be a non-negative whole number: JSON numbers are doubles, so reject fractions
    // (e.g. 1.5), NaN, negatives, and values that do not fit in an int, which toInt() would otherwise
    // silently truncate or wrap.
    const auto parseNonNegativeInt = [](const QJsonValue &v, int *out) {
        if (!v.isDouble())
            return false;

        const double d = v.toDouble();
        if (d != std::floor(d) || d < 0.0 || d > static_cast<double>(std::numeric_limits<int>::max()))
            return false;

        *out = static_cast<int>(d);
        return true;
    };

#define CONF_INT(member)                                                     \
    {                                                                        \
        if (ob.contains(u"" #member ""_s)) {                                 \
            if (!parseNonNegativeInt(ob.value(u"" #member ""_s), &(member))) \
                return false;                                                \
            result.insert(u"" #member ""_s, ob.value(u"" #member ""_s));     \
        }                                                                    \
    }

    CONF_INT(initialKnifeDamage);
    CONF_INT(maximumKnifeDamage);
    CONF_INT(initialHorseDamage);
    CONF_INT(maximumHorseDamage);
    CONF_INT(initialMaxHp);
    CONF_INT(maximumMaxHp);
    CONF_INT(punishHpModifier);
    CONF_INT(punishHpRoundStrategy);

#undef CONF_INT

#define CONF_BOOL(member)                                                \
    {                                                                    \
        if (ob.contains(u"" #member ""_s)) {                             \
            if (!ob.value(u"" #member ""_s).isBool())                    \
                return false;                                            \
            result.insert(u"" #member ""_s, ob.value(u"" #member ""_s)); \
        }                                                                \
    }

    CONF_BOOL(zeroHpAsDead);
    CONF_BOOL(enableLetMove);
    CONF_BOOL(canBuyOnlyInInitialCity);

#undef CONF_BOOL

    if (punishHpRoundStrategy > static_cast<int>(PlusOne))
        return false;

    if (initialKnifeDamage > maximumKnifeDamage)
        return false;
    if (initialHorseDamage > maximumHorseDamage)
        return false;
    if (initialMaxHp > maximumMaxHp)
        return false;

    *this = std::move(result);
    return true;
}

/**
 * @class Room
 * @ingroup QMdmmCore
 * @brief The room which MDMM game is played in.
 *
 * It maintains all the players as well as global data.
 */

/**
 * @brief ctor.
 * @param logicConfiguration The configuration of the logic
 * @param parent The parent (QObject parent hierarchy)
 */
Room::Room(LogicConfiguration logicConfiguration, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<p::RoomP>())
{
    d->logicConfiguration = std::move(logicConfiguration);
}

/**
 * @brief dtor.
 */
Room::~Room() = default;

/**
 * @brief Get the configuration of current logic
 * @return current logic configuration
 */
const LogicConfiguration &Room::logicConfiguration() const
{
    return d->logicConfiguration;
}

/**
 * @brief Set the configuration of current logic
 * @param logicConfiguration updated logic configuration
 */
void Room::setLogicConfiguration(const LogicConfiguration &logicConfiguration)
{
    d->logicConfiguration = logicConfiguration;
}

/**
 * @brief Add a player to game
 * @param playerName the internal name of the newly added player
 * @return the newly added player
 */
Player *Room::addPlayer(const QString &playerName)
{
    if (d->players.contains(playerName))
        return nullptr;

    Player *ret = new Player(playerName, this);
    d->players.insert({playerName, ret});

    emit playerAdded(playerName, QPrivateSignal());

    return ret;
}

/**
 * @brief Remove a player from game
 * @param playerName the internal name of the removed player
 * @return if the player is successfully removed
 */
bool Room::removePlayer(const QString &playerName)
{
    if (std::map<QString, Player *>::iterator it = d->players.find(playerName); it != d->players.end()) {
        emit playerRemoved(playerName, QPrivateSignal());

        delete it->second;
        d->players.erase(it);
        return true;
    }

    return false;
}

/**
 * @brief get the player of specific internal name
 * @param playerName the internal name of the searched player
 * @return the player of the internal name, or @c nullptr if not found
 */
Player *Room::player(const QString &playerName)
{
    if (std::map<QString, Player *>::iterator it = d->players.find(playerName); it != d->players.end())
        return it->second;

    return nullptr;
}

/**
 * @brief get the player of specific internal name (const version)
 * @param playerName the internal name of the searched player
 * @return the player of the internal name, or @c nullptr if not found
 */
const Player *Room::player(const QString &playerName) const
{
    if (std::map<QString, Player *>::const_iterator it = d->players.find(playerName); it != d->players.cend())
        return it->second;

    return nullptr;
}

/**
 * @brief get a list of players
 * @return the list of players
 */
QList<Player *> Room::players()
{
    QList<Player *> ret;

    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::iterator it = d->players.begin(); it != d->players.end(); ++it)
        ret << it->second;

    return ret;
}

/**
 * @brief get a list of players (const version)
 * @return the list of players
 */
QList<const Player *> Room::players() const
{
    QList<const Player *> ret;

    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::const_iterator it = d->players.cbegin(); it != d->players.cend(); ++it)
        ret << it->second;

    return ret;
}

/**
 * @brief get a list of player interna names
 * @return the list of player internal names
 */
QStringList Room::playerNames() const
{
    QStringList ret;
    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::const_iterator it = d->players.cbegin(); it != d->players.cend(); ++it)
        ret << it->first;

    return ret;
}

/**
 * @brief get a list of alive players
 * @return the list of alive players
 */
QList<Player *> Room::alivePlayers()
{
    QList<Player *> res;

    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::iterator it = d->players.begin(); it != d->players.end(); ++it) {
        Player *player = it->second;
        if (player->alive())
            res << player;
    }

    return res;
}

/**
 * @brief get a list of alive players (const version)
 * @return the list of alive players
 */
QList<const Player *> Room::alivePlayers() const
{
    QList<const Player *> res;

    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::const_iterator it = d->players.cbegin(); it != d->players.cend(); ++it) {
        const Player *player = it->second;
        if (player->alive())
            res << player;
    }

    return res;
}

/**
 * @brief get a list of internal names of alive players
 * @return the list of names of alive players
 */
QStringList Room::alivePlayerNames() const
{
    QStringList res;
    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::const_iterator it = d->players.cbegin(); it != d->players.cend(); ++it) {
        if (it->second->alive())
            res.push_back(it->first);
    }

    return res;
}

/**
 * @fn Room::alivePlayersCount() const
 * @brief get the count of alive players
 * @return count of alive players
 *
 * equals to @verbatim alivePlayers().size() @endverbatim
 */

/**
 * @fn Room::isRoundOver() const
 * @brief judge if current game is round over
 * @return if current game is round over
 *
 * equals to @verbatim alivePlayersCount() <= 1 @endverbatim
 */

/**
 * @brief judge if current game is game over
 * @param winnerPlayerNames (out) winner player names
 * @return if current game is game over
 */
bool Room::isGameOver(QStringList *winnerPlayerNames) const
{
    bool ret = false;
    if (winnerPlayerNames != nullptr)
        winnerPlayerNames->clear();

    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::const_iterator it = d->players.cbegin(); it != d->players.cend(); ++it) {
        const Player *player = it->second;
        if (!player->canUpgradeHorse() && !player->canUpgradeKnife() && !player->canUpgradeMaxHp()) {
            ret = true;
            if (winnerPlayerNames != nullptr)
                *winnerPlayerNames << player->objectName();
        }
    }

    return ret;
}

/**
 * @brief prepare every players for round start
 *
 * @note This is a round-lifecycle method driven by the @c Logic state machine.
 * It is not meant to be called by arbitrary code holding a @c Room pointer.
 */
void Room::prepareForRoundStart()
{
    int i = 0;

    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::iterator it = d->players.begin(); it != d->players.end(); ++it) {
        Player *player = it->second;
        player->prepareForRoundStart(++i);
    }
}

/**
 * @brief reset upgrades of each player, for game start
 *
 * @note This is a round-lifecycle method driven by the @c Logic state machine.
 * It is not meant to be called by arbitrary code holding a @c Room pointer.
 */
void Room::resetUpgrades()
{
    // NOLINTNEXTLINE(modernize-loop-convert): std::map iteration is deliberately iterator-based (no range-based for over pairs)
    for (std::map<QString, Player *>::iterator it = d->players.begin(); it != d->players.end(); ++it) {
        Player *player = it->second;
        player->resetUpgrades();
    }
}

/**
 * @fn Room::playerAdded(const QString &playerName, QPrivateSignal)
 * @brief emitted when a player is added
 * @param playerName the internal name of the added player
 */

/**
 * @fn Room::playerRemoved(const QString &playerName, QPrivateSignal);
 * @brief emitted when a player is removed
 * @param playerName the internal name of the removed player
 */

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmplayer.h"
#include "qmdmmplayer_p.h"

#include "qmdmmlogic.h"
#include "qmdmmroom.h"

/**
 * @file qmdmmplayer.h
 * @brief This is the file where MDMM player is defined.
 */

namespace QMdmmCore {
#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class Player
 * @ingroup QMdmmCore
 * @brief The player playing MDMM Game
 *
 * This is the place where the data of each player is saved, and is the object where the data changed signal is emitted.
 */

/**
 * @property Player::hasKnife
 * @brief if the player has bought knife
 */

/**
 * @property Player::hasHorse
 * @brief if the player has bought horse
 */

/**
 * @property Player::hp
 * @brief the current HP of the player
 */

/**
 * @property Player::place
 * @brief the place of the player.
 *
 * @note The return value is not of type @c QMdmmData::Place . The reason is that the number of Cities should be same as the number of players in a room.
 * Making a enumeration value of each City is not feasible. Since even the enumeration is created the value is still @c static_cast-ed and the enumeration will be of no use.
 */

/**
 * @property Player::initialPlace
 * @brief the initial place of the player.
 *
 * @note This is also known as "seat", since one should be in "City No. N" where N is the seat number when game starts.
 */

/**
 * @property Player::knifeDamage
 * @brief the knife damage of the player.
 */

/**
 * @property Player::horseDamage
 * @brief the horse damage of the player.
 */

/**
 * @property Player::maxHp
 * @brief the maximum HP of the player.
 */

/**
 * @property Player::upgradePoint
 * @brief the upgrade point of the player.
 *
 * @note This is also known as "killed player per round"
 */

/**
 * @property Player::dead
 * @brief if the player is dead
 *
 * @note This property is not stored and is calculated from @c hp property.
 */

/**
 * @property Player::alive
 * @brief if the player is alive
 *
 * @note This property is not stored and is calculated from @c hp property.
 */

/**
 * @brief ctor.
 * @param name The internal name of the player
 * @param room The room the player is in (or parent as QObject hierarchy)
 *
 * Note: This ctor is meant to be called from Room
 */
Player::Player(const QString &name, Room *room)
    : QObject(room)
    , d(std::make_unique<p::PlayerP>(room))
{
    setObjectName(name);
}

/**
 * @brief dtor
 *
 * Note: This dtor is meant to be called from Room
 */
Player::~Player() = default;

/**
 * @brief Get the room the player is in
 * @return the room the player is in
 */
Room *Player::room()
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): Player always has Room as parent; skip dynamic_cast cost
    return static_cast<Room *>(parent());
}

/**
 * @brief Get the room the player is in (const version)
 * @return the room the player is in
 */
const Room *Player::room() const
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): same as above
    return static_cast<const Room *>(parent());
}

/**
 * @brief getter of property @c hasKnife
 * @return @c hasKnife
 */
bool Player::hasKnife() const noexcept
{
    return d->knife;
}

/**
 * @brief setter of property @c hasKnife
 * @param k @c hasKnife
 */
void Player::setHasKnife(bool k)
{
    if (d->knife != k) {
        d->knife = k;
        emit hasKnifeChanged(k, QPrivateSignal());
    }
}

/**
 * @brief getter of property @c hasHorse
 * @return @c hasHorse
 */
bool Player::hasHorse() const noexcept
{
    return d->horse;
}

/**
 * @brief setter of property @c hasHorse
 * @param h @c hasHorse
 */
void Player::setHasHorse(bool h)
{
    if (d->horse != h) {
        d->horse = h;
        emit hasHorseChanged(h, QPrivateSignal());
    }
}

/**
 * @brief getter of property @c hp
 * @return @c hp
 */
int Player::hp() const noexcept
{
    return d->hp;
}

/**
 * @brief setter of property @c hp
 * @param h @c hp
 * @param kills (OUT) if this HP kills the player
 *
 * This is not only a setter, but also the one triggers @c die() signal if this HP kills.
 * A value of HP kills means before setting this HP the player is alive, and after setting this HP the player is dead.
 *
 * A player is alive if @c hp is positive and dead if @c hp is negative. It depends on the configuration whether @c hp is 0 is treated as dead or alive.
 *
 * @sa QMdmm::LogicConfiguration::zeroHpAsDead
 */
void Player::setHp(int h, bool *kills)
{
    if (kills == nullptr) {
        static thread_local bool _kills;
        kills = &_kills;
    }

    *kills = false;

    if (d->hp != h) {
        bool wasDead = dead();
        d->hp = h;
        emit hpChanged(h, QPrivateSignal());
        if (!wasDead && dead()) {
            *kills = true;
            emit die(QPrivateSignal());
        }
    }
}

/**
 * @brief getter of property @c place
 * @return @c place
 */
int Player::place() const noexcept
{
    return d->place;
}

/**
 * @brief setter of property @c place
 * @param toPlace @c place
 */
void Player::setPlace(int toPlace)
{
    if (d->place != toPlace) {
        d->place = toPlace;
        emit placeChanged(d->place, QPrivateSignal());
    }
}

/**
 * @brief getter of property @c initialPlace
 * @return @c initialPlace
 */
int Player::initialPlace() const noexcept
{
    return d->initialPlace;
}

/**
 * @brief setter of property @c initialPlace
 * @param initialPlace @c initialPlace
 */
void Player::setInitialPlace(int initialPlace)
{
    if (d->initialPlace != initialPlace) {
        d->initialPlace = initialPlace;
        emit initialPlaceChanged(initialPlace, QPrivateSignal {});
    }
}

/**
 * @brief getter of property @c knifeDamage
 * @return @c knifeDamage
 */
int Player::knifeDamage() const noexcept
{
    return d->knifeDamage;
}

/**
 * @brief setter of property @c knifeDamage
 * @param k @c knifeDamage
 */
void Player::setKnifeDamage(int k)
{
    if (d->knifeDamage != k) {
        d->knifeDamage = k;
        emit knifeDamageChanged(k, QPrivateSignal());
    }
}

/**
 * @brief getter of property @c horseDamage
 * @return @c horseDamage
 */
int Player::horseDamage() const noexcept
{
    return d->horseDamage;
}

/**
 * @brief setter of property @c horseDamage
 * @param h @c horseDamage
 */
void Player::setHorseDamage(int h)
{
    if (d->horseDamage != h) {
        d->horseDamage = h;
        emit horseDamageChanged(h, QPrivateSignal());
    }
}

/**
 * @brief getter of property @c maxHp
 * @return @c maxHp
 */
int Player::maxHp() const noexcept
{
    return d->maxHp;
}

/**
 * @brief setter of property @c maxHp
 * @param m @c maxHp
 */
void Player::setMaxHp(int m)
{
    if (d->maxHp != m) {
        d->maxHp = m;
        emit maxHpChanged(m, QPrivateSignal());
    }
}

/**
 * @brief getter of property @c upgradePoint
 * @return @c upgradePoint
 */
int Player::upgradePoint() const noexcept
{
    return d->upgradePoint;
}

/**
 * @brief setter of property @c upgradePoint
 * @param u @c upgradePoint
 *
 * @note The value should not exceed the player's total remaining upgrade times,
 *       i.e. @c upgradeKnifeRemainingTimes() + @c upgradeHorseRemainingTimes() +
 *       @c upgradeMaxHpRemainingTimes(). The normal path enforces this cap at
 *       kill time (see @c PlayerP::applyDamage() in qmdmmplayer_p.cpp); a
 *       programmatic call bypasses the check.
 */
void Player::setUpgradePoint(int u)
{
    if (d->upgradePoint != u) {
        d->upgradePoint = u;
        emit upgradePointChanged(u, QPrivateSignal());
    }
}

/**
 * @brief whether a given HP would leave the player dead
 * @param hp the HP to read against the death threshold
 * @return @c true if @a hp is at or below that threshold
 *
 * A player is alive while its HP is positive and dead once it is negative; whether @c 0 still counts as alive depends on @c QMdmmLogicConfiguration::zeroHpAsDead . This is where that reading lives, so a strategy weighing a blow asks here instead of comparing on its own.
 *
 * @sa @c dead() , @c QMdmmLogicConfiguration::zeroHpAsDead()
 */
bool Player::deadAtHp(int hp) const
{
    bool zeroHpAsDead = room()->logicConfiguration().zeroHpAsDead();
    return zeroHpAsDead ? (hp <= 0) : (hp < 0);
}

/**
 * @brief getter of property @c dead
 * @return @c dead
 *
 * @sa @c deadAtHp()
 */
bool Player::dead() const
{
    return deadAtHp(d->hp);
}

/**
 * @fn Player::alive() const
 * @brief getter of property @c alive
 * @return @c alive
 */

/**
 * @brief If the player can buy knife
 * @return @c true if able, @c false if not
 *
 * @sa QMdmmLogicConfiguration::canBuyOnlyInInitialCity
 */
bool Player::canBuyKnife() const
{
    return alive() && !hasKnife() && (room()->logicConfiguration().canBuyOnlyInInitialCity() ? (place() == initialPlace()) : (place() != Data::Village));
}

/**
 * @brief If the player can buy horse
 * @return @c true if able, @c false if not
 *
 * @sa QMdmmLogicConfiguration::canBuyOnlyInInitialCity
 */
bool Player::canBuyHorse() const
{
    return alive() && !hasHorse() && (room()->logicConfiguration().canBuyOnlyInInitialCity() ? (place() == initialPlace()) : (place() != Data::Village));
}

/**
 * @brief If the player can slash a specific player
 * @param to the target player
 * @return @c true if able, @c false if not
 *
 * @note A player cannot slash himself / herself.
 */
bool Player::canSlash(const Player *to) const
{
    Q_ASSERT(room() == to->room());

    if (dead() || to->dead())
        return false;

    if (!hasKnife())
        return false;

    if (this == to)
        return false;

    if (place() != to->place())
        return false;

    return true;
}

/**
 * @brief The HP a player is punished with for slashing, at the place it stands
 * @return the punish HP, @c 0 if a slash is not punished here
 *
 * A slash is punished only in city and only when @c QMdmmLogicConfiguration::punishHpModifier is positive. The punish HP itself is worked out from the slasher's own @c maxHp() and @c QMdmmLogicConfiguration::punishHpRoundStrategy.
 *
 * @sa @c canSlash() , @c slash()
 */
int Player::slashPunishHp() const
{
    if (place() == Data::Village)
        return 0;

    const int punishHpModifier = room()->logicConfiguration().punishHpModifier();
    if (punishHpModifier <= 0)
        return 0;

    const LogicConfiguration::PunishHpRoundStrategy punishHpRoundStrategy = room()->logicConfiguration().punishHpRoundStrategy();

    int punishedHp = 0;
    switch (punishHpRoundStrategy) {
    default:
        [[fallthrough]];
    case LogicConfiguration::RoundDown:
        punishedHp = maxHp() / punishHpModifier;
        break;
    case LogicConfiguration::RoundToNearest45:
        punishedHp = (((maxHp() * 2) / punishHpModifier) + 1) / 2;
        break;
    case LogicConfiguration::RoundUp:
        punishedHp = (maxHp() + punishHpModifier - 1) / punishHpModifier;
        break;
    case LogicConfiguration::PlusOne:
        punishedHp = (maxHp() / punishHpModifier) + 1;
        break;
    }

    return punishedHp;
}

/**
 * @brief If the player can kick a specific player
 * @param to the target player
 * @return @c true if able, @c false if not
 *
 * @note A player cannot kick himself / herself.
 */
bool Player::canKick(const Player *to) const
{
    Q_ASSERT(room() == to->room());

    if (dead() || to->dead())
        return false;

    if (!hasHorse())
        return false;

    if (this == to)
        return false;

    if ((place() != to->place()) || (place() == Data::Village))
        return false;

    return true;
}

/**
 * @brief If the player can move to a specific place
 * @param toPlace the target place
 * @return @c true if able, @c false if not
 */
bool Player::canMove(int toPlace) const
{
    return alive() && Data::isPlaceAdjacent(place(), toPlace);
}

/**
 * @brief If the player can make a specific player move
 * @param to the target player
 * @param toPlace the target place
 * @return @c true if able, @c false if not
 *
 * @note if @c this == @c to then this function is same as @c canMove() , regardless of @ref QMdmmCore::LogicConfiguration::enableLetMove()
 *
 * @sa QMdmmCore::LogicConfiguration::enableLetMove
 */
bool Player::canLetMove(const Player *to, int toPlace) const
{
    Q_ASSERT(room() == to->room());

    if (this == to)
        return canMove(toPlace);

    if (!room()->logicConfiguration().enableLetMove())
        return false;

    if (dead() || to->dead())
        return false;

    // one movement should move player to adjacent place only
    if (!Data::isPlaceAdjacent(to->place(), toPlace))
        return false;

    // case 1: pull a player in adjacent place to self's place
    if (Data::isPlaceAdjacent(place(), to->place()) && toPlace == place())
        return true;

    // case 2: push a player in same place to adjacent place
    if (place() == to->place())
        return true;

    return false;
}

/**
 * @brief get the remained times a player can upgrade knife damage
 * @return the remained times a player can upgrade knife damage
 *
 * @sa QMdmmLogicConfiguration::maximumKnifeDamage
 */
int Player::upgradeKnifeRemainingTimes() const
{
    return room()->logicConfiguration().maximumKnifeDamage() - knifeDamage();
}

/**
 * @brief get the remained times a player can upgrade horse damage
 * @return the remained times a player can upgrade horse damage
 *
 * @sa QMdmm::LogicConfiguration::maximumHorseDamage
 */
int Player::upgradeHorseRemainingTimes() const
{
    return room()->logicConfiguration().maximumHorseDamage() - horseDamage();
}

/**
 * @brief get the remained times a player can upgrade maximum HP
 * @return the remained times a player can upgrade maximum HP
 *
 * @sa QMdmm::LogicConfiguration::maximumMaxHp
 */
int Player::upgradeMaxHpRemainingTimes() const
{
    return room()->logicConfiguration().maximumMaxHp() - maxHp();
}

/**
 * @fn Player::canUpgradeKnife() const
 * @brief if a player can upgrade knife damage
 * @return @c true if able, @c false if not
 */

/**
 * @fn Player::canUpgradeHorse() const
 * @brief if a player can upgrade horse damage
 * @return @c true if able, @c false if not
 */

/**
 * @fn Player::canUpgradeMaxHp() const
 * @brief if a player can upgrade maximum HP
 * @return @c true if able, @c false if not
 */

/**
 * @brief Action: Buy knife
 * @return @c true if succeed, @c false if not
 *
 * @sa @c canBuyKnife()
 */
bool Player::buyKnife()
{
    if (!canBuyKnife())
        return false;

    setHasKnife(true);
    return true;
}

/**
 * @brief Action: Buy horse
 * @return @c true if succeed, @c false if not
 *
 * @sa @c canBuyHorse()
 */
bool Player::buyHorse()
{
    if (!canBuyHorse())
        return false;

    setHasHorse(true);
    return true;
}

/**
 * @brief Action: Slash a specific player
 * @param to the target player
 * @return @c true if succeed, @c false if not
 *
 * If punish HP is enabled (@c QMdmmLogicConfiguration::punishHpModifier > 0) and this slash is occurred in city, the punish HP will take into account.
 *
 * @sa @c canSlash() , @c slashPunishHp()
 */
bool Player::slash(Player *to)
{
    Q_ASSERT(room() == to->room());

    if (!canSlash(to))
        return false;

    p::PlayerP::applyDamage(this, to, knifeDamage(), Data::Slashed);

    const int punishedHp = slashPunishHp();
    if (punishedHp > 0)
        p::PlayerP::applyDamage(to, this, punishedHp, Data::HpPunished);

    return true;
}

/**
 * @brief Action: Kick a specific player
 * @param to the target player
 * @return @c true if succeed, @c false if not
 *
 * @sa @c canKick()
 */
bool Player::kick(Player *to)
{
    Q_ASSERT(room() == to->room());

    if (!canKick(to))
        return false;

    p::PlayerP::applyDamage(this, to, horseDamage(), Data::Kicked);

    if (!to->dead()) {
        // bypass the canMove check, directly set place.
        // Since it is effect of the kick action
        to->setPlace(Data::Village);
    }

    return true;
}

/**
 * @brief Action: Move to a specific place
 * @param toPlace the target place
 * @return @c true if succeed, @c false if not
 *
 * Not to confuse with @c setPlace() where it sets the place forcefully. This function does checks before place is set.
 *
 * @sa @c canMove()
 */
bool Player::move(int toPlace)
{
    if (canMove(toPlace)) {
        setPlace(toPlace);
        return true;
    }

    return false;
}

/**
 * @brief Action: Let a specific player move to a specific place
 * @param to the target player
 * @param toPlace the target place
 * @return @c true if succeed, @c false if not
 *
 * @sa @c canLetMove()
 */
bool Player::letMove(Player *to, int toPlace) // NOLINT(readability-make-member-function-const): Operation is ought to be not const
{
    // for pull / push
    // kick (effect) is not implemented here since it should bypass canLetMove check.

    Q_ASSERT(room() == to->room());

    if (canLetMove(to, toPlace)) {
        to->setPlace(toPlace);
        return true;
    }

    return false;
}

/**
 * @brief Action: Do nothing
 * @return @c true if succeed, @c false if not
 */
bool Player::doNothing() // NOLINT(readability-make-member-function-const): Operation is ought to be not const
{
    return alive();
}

/**
 * @brief upgrade knife damage by one point
 * @return @c true if succeed, @c false if not
 */
bool Player::upgradeKnife()
{
    if (!canUpgradeKnife())
        return false;

    setKnifeDamage(knifeDamage() + 1);
    return true;
}

/**
 * @brief upgrade horse damage by one point
 * @return @c true if succeed, @c false if not
 */
bool Player::upgradeHorse()
{
    if (!canUpgradeHorse())
        return false;

    setHorseDamage(horseDamage() + 1);
    return true;
}

/**
 * @brief upgrade maximum HP by one point
 * @return @c true if succeed, @c false if not
 */
bool Player::upgradeMaxHp()
{
    if (!canUpgradeMaxHp())
        return false;

    setMaxHp(maxHp() + 1);
    return true;
}

/**
 * @brief reset all status to initial state of a round
 * @param seat the seat number (a.k.a. initial place)
 *
 * @note This is a round-lifecycle method driven by the @c Logic state machine.
 * It is not meant to be called by arbitrary code holding a @c Player pointer.
 */
void Player::prepareForRoundStart(int seat)
{
    setHasKnife(false);
    setHasHorse(false);
    setHp(maxHp());
    setInitialPlace(seat);
    setPlace(seat);
    setUpgradePoint(0);
}

/**
 * @brief reset all upgrades.
 *
 * Useful when a player leaves or joins a room between rounds, which causes a game over
 *
 * @note This is a round-lifecycle method driven by the @c Logic state machine.
 * It is not meant to be called by arbitrary code holding a @c Player pointer.
 */
void Player::resetUpgrades()
{
    setMaxHp(room()->logicConfiguration().initialMaxHp());
    setKnifeDamage(room()->logicConfiguration().initialKnifeDamage());
    setHorseDamage(room()->logicConfiguration().initialHorseDamage());
    setUpgradePoint(0);
}

/**
 * @fn Player::hasKnifeChanged(bool hasKnife, QPrivateSignal)
 * @brief notify signal for property @c hasKnife
 */

/**
 * @fn Player::hasHorseChanged(bool hasHorse, QPrivateSignal)
 * @brief notify signal for property @c hasHorse
 */

/**
 * @fn Player::hpChanged(int hp, QPrivateSignal)
 * @brief notify signal for property @c hp
 */

/**
 * @fn Player::placeChanged(int place, QPrivateSignal)
 * @brief notify signal for property @c place
 */

/**
 * @fn Player::initialPlaceChanged(int initialPlace, QPrivateSignal)
 * @brief notify signal for property @c initialPlace
 */

/**
 * @fn Player::knifeDamageChanged(int knifeDamage, QPrivateSignal)
 * @brief notify signal for property @c knifeDamage
 */

/**
 * @fn Player::horseDamageChanged(int horseDamage, QPrivateSignal)
 * @brief notify signal for property @c horseDamage
 */

/**
 * @fn Player::maxHpChanged(int maxHp, QPrivateSignal)
 * @brief notify signal for property @c maxHp
 */

/**
 * @fn Player::upgradePointChanged(int upgradePoint, QPrivateSignal)
 * @brief notify signal for property @c upgradePoint
 */

/**
 * @fn Player::damaged(const Player *from, int damagePoint, Data::DamageReason reason, QPrivateSignal)
 * @brief emitted when this player is damaged
 */

/**
 * @fn Player::die(QPrivateSignal)
 * @brief emitted when the player dies
 *
 * Acts as the notify signal for property @c alive and @c dead
 */

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore

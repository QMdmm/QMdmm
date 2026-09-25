// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmcoreglobal.h"

#include <map>

using namespace Qt::StringLiterals;

/**
 * @file qmdmmcoreglobal.h
 * @brief Global definition of QMdmmCore library
 */

/**
 * @namespace QMdmmCore
 * @brief The Core API, including game logic, configuration, and debug essentials.
 */

/**
 * @def QMDMMCORE_EXPORT
 * @brief Indicates this function is public and is exported from QMdmmCore library.
 */

/**
 * @def QMDMMCORE_PRIVATE_EXPORT
 * @brief Indicates this function is private but will be exported from QMdmmCore library if specified during build.
 */

/**
 * @def QMDMM_EXPORT_NAME
 * @brief Specify a file name for automatic header generation. Expand to nothing.
 * @param QMdmmCoreGlobal dummy parameter.
 */

namespace QMdmmCore {

#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @namespace QMdmmCore::Data
 * @headerfile <QMdmmData>
 * @brief Various data definition of MDMM game
 */

/**
 * @enum Data::Place
 * @brief Enumeration values for places
 *
 * The original @c Place enum was removed for overdesign: the number of places should
 * equal the number of players (City1, City2, ... up to the maximum supported player
 * count), and in theory the player count is unlimited since players do not differ.
 * The enum is kept with only the @c Village value for QMetaObject generation; all
 * place-related values are plain integers.
 */

/**
 * @var QMdmmCore::Data::Place Data::Village
 * @brief For use with @c QMdmmPlayer::place() , if it equals to @c Data::Village then this player is in Village.
 *
 * This enumeration variable equals to zero. Provided for readability.
 */

/**
 * @enum Data::DamageReason
 * @brief The reason for a damage.
 */

/**
 * @var QMdmmCore::Data::DamageReason Data::DamageReasonUnknown
 * @brief Unknown / erroneous damage reason.
 */

/**
 * @var QMdmmCore::Data::DamageReason Data::Slashed
 * @brief Damage is caused by a slash.
 */

/**
 * @var QMdmmCore::Data::DamageReason Data::Kicked
 * @brief Damage is caused by a kick.
 */

/**
 * @var QMdmmCore::Data::DamageReason Data::HpPunished
 * @brief Damage is caused by HP punish.
 *
 * There is a mechanism in more modern version of MDMM game, where punishment is applied for slash in city.
 * By default the punished HP is half of the maximum HP, rounded to nearest integer
 *
 * @sa @c QMdmmLogicConfiguration::PunishHpRoundStrategy
 */

/**
 * @enum Data::RockPaperScissors
 * @brief Rock-Paper-Scissors variables.
 */

/**
 * @var QMdmmCore::Data::RockPaperScissors Data::Rock
 * @brief Rock
 */

/**
 * @var QMdmmCore::Data::RockPaperScissors Data::Paper
 * @brief Paper
 */

/**
 * @var QMdmmCore::Data::RockPaperScissors Data::Scissors
 * @brief Scissors
 */

/**
 * @enum Data::Action
 * @brief Action taken each time a player is acting.
 */

/**
 * @var QMdmmCore::Data::Action Data::DoNothing
 * @brief Do Nothing
 */

/**
 * @var QMdmmCore::Data::Action Data::BuyKnife
 * @brief Buy Knife
 */

/**
 * @var QMdmmCore::Data::Action Data::BuyHorse
 * @brief Buy Horse
 */

/**
 * @var QMdmmCore::Data::Action Data::Slash
 * @brief Slash (toPlayer: the target player)
 */

/**
 * @var QMdmmCore::Data::Action Data::Kick
 * @brief Kick (toPlayer: the target player)
 */

/**
 * @var QMdmmCore::Data::Action Data::Move
 * @brief Move (toPlace: the target place)
 */

/**
 * @var QMdmmCore::Data::Action Data::LetMove
 * @brief Let Move (toPlayer: the target player, toPlace: the target place)
 */

/**
 * @enum Data::UpgradeItem
 * @brief Upgradeable items when a player wins a game.
 */

/**
 * @var QMdmmCore::Data::UpgradeItem Data::UpgradeKnife
 * @brief Upgrade knife
 */

/**
 * @var QMdmmCore::Data::UpgradeItem Data::UpgradeHorse
 * @brief Upgrade horse
 */

/**
 * @var QMdmmCore::Data::UpgradeItem Data::UpgradeMaxHp
 * @brief Upgrade maximum hp
 */

/**
 * @enum Data::AgentStateEnum
 * @brief State used for Agents.
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateMaskOnline
 * @brief Mask of online
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateMaskBot
 * @brief Mask of bot
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateMaskTrust
 * @brief Mask of the "managed" flag
 *
 * The flag only travels: the operation side declares it, the server owns it and reports it back
 * (see @c Protocol::NotifyManagedChanged). What being managed means below the wire is a
 * client-side decision: a client that manages its player gives up on that player's requests
 * instead of asking -- answering each with the protocol's give-up marker, a null reply value --
 * so the server answers each one with the default reply it keeps for that kind of request (see
 * @c Protocol::RequestId). The managed player stays connected while that happens, and those
 * default replies are broadcast like anyone else's.
 *
 * Entrusting a player therefore changes who chooses, not whether anything happens. The upgrade
 * point is the clearest case: the default reply there is an empty list, which the logic replaces
 * with a fallback of its own (@c Logic::upgradeReply) that spends every point, knife damage
 * first. A managed player goes through the upgrade phase and spends its points like anyone else.
 *
 * A reconnecting player must NOT be re-trusted by default.
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateOffline
 * @brief State of offline
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateOfflineBot
 * @brief State of offline bot
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateOnline
 * @brief State of online
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateOnlineBot
 * @brief State of online bot
 */

/**
 * @var QMdmmCore::Data::AgentStateEnum Data::StateOnlineTrust
 * @brief State of online and managed
 */

/**
 * @fn QMdmmCore::Data::isPlaceAdjacent(int p1, int p2)
 * @brief Judges if the 2 places are adjacent, for judgment like make-move ability or other things.
 * @param p1 Place 1
 * @param p2 Place 2
 * @return If the 2 places are adjacent.
 *
 * It is actually simplified to "only one of p1 and p2 is Village"
 */

namespace {
constexpr bool rpsGreater(Data::RockPaperScissors op1, Data::RockPaperScissors op2) noexcept
{
    return (op1 == Data::Rock && op2 == Data::Scissors) || (op1 == Data::Scissors && op2 == Data::Paper) || (op1 == Data::Paper && op2 == Data::Rock);
}
} // namespace

/**
 * @brief Judges winners of a Rock-Paper-Scissors output
 * @param judgers A kv-pair of the value a player outputs
 * @return A list of winners, with each value duplicates multiple times. The duplication times is equal to the number of players loses.
 *
 * This is the core logic of which Rock-Paper-Scissors.
 * Our rule is that winners can do actions on determined sequence by times that equals to the number of players loses.
 */
QStringList Data::rockPaperScissorsWinners(const QHash<QString, Data::RockPaperScissors> &judgers)
{
    std::map<Data::RockPaperScissors, QStringList> judgersMap;

    for (QHash<QString, Data::RockPaperScissors>::const_iterator it = judgers.cbegin(); it != judgers.cend(); ++it)
        judgersMap[it.value()] << it.key();

    if (judgersMap.size() == 2) {
        std::map<Data::RockPaperScissors, QStringList>::const_iterator it1 = judgersMap.cbegin();
        std::map<Data::RockPaperScissors, QStringList>::const_iterator it2 = judgersMap.cbegin();
        ++it2;

        Data::RockPaperScissors type1 = it1->first;
        Data::RockPaperScissors type2 = it2->first;

        if (!rpsGreater(type1, type2))
            std::swap(it1, it2);

        // now it1.value is winner, it2.value is loser
        // we'd make every winners repeat N times (N is loser.count), for the real judgment use
        QStringList d;
        for (int i = 0; i < it2->second.length(); ++i)
            d.append(it1->second);

        return d;
    }

    return {};
}

/**
 * @namespace QMdmmCore::Global
 * @headerfile <QMdmmGlobal>
 * @brief Global functions of QMdmm
 */

/**
 * @brief Returns the version number QMdmmCore is built with
 * @return the version number
 */
QVersionNumber Global::version()
{
    return QVersionNumber::fromString(u"" QMDMM_VERSION ""_s);
}

/**
 * @namespace QMdmmCore::Utilities
 * @headerfile <QMdmmUtilities>
 * @brief Convenience functions and types for working with QMdmm library
 */

/**
 * @class QMdmmCore::Utilities::DependentFalse
 * @brief a workaround helper class for CWG2518
 *
 * Before CWG2518, `static_assert(false, "")` is ill-formed even if in a template which will never instantiate.
 *
 * Workaround is to use `static_assert(QMdmmCore::Utilities::dependentFalse<T>, "")` instead.
 */

/**
 * @var QMdmmCore::Utilities::dependentFalse
 * @brief a workaround helper class for CWG2518
 *
 * @sa @c QMdmmCore::Utilities::DependentFalse
 */

/**
 * @fn QMdmmCore::Utilities::list2Set(const T &l)
 * @brief Convenience function of converting a QList to QSet
 * @tparam T The type of the list
 * @param l The list to convert
 * @return The converted set
 *
 * Qt deprecates QList::toSet since Qt 5.15 and instead suggests using the QSet iterator ctor.
 * This function calls the ctor while accepting any iterable types.
 *
 * Return type: `QSet<typename std::remove_cv_t<typename std::iterator_traits<decltype(std::cbegin((const T &)std::declval<T>()))>::value_type>>`
 */

/**
 * @fn QMdmmCore::Utilities::enumList2VariantList(const QList<T> &list)
 * @brief Convenience function of converting QList<enum> to QVariantList
 */

/**
 * @fn QMdmmCore::Utilities::enumList2VariantList(const QList<QFlags<T> > &list)
 * @brief Convenience function of converting QList<QFlags> to QVariantList
 */

/**
 * @brief Convenience function of converting QList<int> to QVariantList
 */
QVariantList Utilities::intList2VariantList(const QList<int> &list)
{
    QVariantList ret;
    ret.reserve(list.length());
    foreach (int i, list)
        ret << i;
    return ret;
}

/**
 * @brief Convenience function of converting QVariantList to QList<int>
 */
QList<int> Utilities::variantList2IntList(const QVariantList &list)
{
    QList<int> ret;
    ret.reserve(list.length());
    foreach (const QVariant &i, list)
        ret << i.toInt();
    return ret;
}

/**
 * @brief Convenience function of converting QStringList to QVariantList
 * @note Qt 5 QStringList is not QList<QString> but Qt 6 is. Use parameter type QList<QString> for compatible with Qt 5
 */
QVariantList Utilities::stringList2VariantList(const QList<QString> &list)
{
    QVariantList ret;
    ret.reserve(list.length());
    foreach (const QString &i, list)
        ret << i;
    return ret;
}

/**
 * @brief Convenience function of converting QList<int> to QStringList
 */
QStringList Utilities::variantList2StringList(const QVariantList &list)
{
    QStringList ret;
    ret.reserve(list.length());
    foreach (const QVariant &i, list)
        ret << i.toString();
    return ret;
}

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore

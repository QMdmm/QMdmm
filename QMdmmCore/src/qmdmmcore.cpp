// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmcoreglobal.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifdef QMDMM_MACOS_APP_BUNDLE
#include <QStandardPaths>
#endif

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
 * @ingroup QMdmmCore
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
 * @ingroup QMdmmCore
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
 * @ingroup QMdmmCore
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
 * @ingroup QMdmmCore
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
 * @ingroup QMdmmCore
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
 * @ingroup QMdmmCore
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

namespace {
#ifndef QMDMM_MACOS_APP_BUNDLE
// Two of the three shapes the configuration and the logs take carry their directory as a
// compile definition, and what such a definition holds is a recipe for a directory rather
// than a directory: see Global::configurationDirectory() for all three. The application
// bundle is the third, and it has no definition to resolve -- nothing reaches this function
// in that shape, so it is not built there.
[[nodiscard]] QString resolveConfiguredDirectory(const QString &configured)
{
    if (configured.startsWith(u'/'))
        return configured;

    // Relative to the directory the executable sits in, which is the one thing this process
    // knows without being told what layout it was installed in. The value was computed from
    // that layout when the project was configured, so this lands on the same place however
    // far the installed tree has been moved since.
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + u"/"_s + configured);
}
#endif

#ifdef QMDMM_MACOS_APP_BUNDLE
// Where this platform keeps an application's own persistent data, under the name the
// application is known by. That name is the bundle's identifier, the string the bundle itself
// is identified by and the same one for all three programs -- which is what keeps them on one
// directory rather than one each. QStandardPaths knows the platform's part; nothing here
// spells out a path that belongs to one platform's conventions.
[[nodiscard]] QString bundleApplicationDataDirectory()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/"_s + u"" QMDMM_MACOS_BUNDLE_IDENTIFIER ""_s);
}
#endif

// Q_DISABLE_COPY_MOVE below deletes copy and move deliberately; the check reads those four
// deleted members as a class with special members but no destructor, of which there is none to write.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct ConfigurationDirectory final
{
    QString dir;

    ConfigurationDirectory()
        : dir(
#ifdef QMDMM_MACOS_APP_BUNDLE
              bundleApplicationDataDirectory() + u"/etc"_s
#else
              resolveConfiguredDirectory(u"" QMDMM_CONFIGURATION_PREFIX ""_s)
#endif
          )
    {
    }

    Q_DISABLE_COPY_MOVE(ConfigurationDirectory);
};

// Q_DISABLE_COPY_MOVE below deletes copy and move deliberately; the check reads those four
// deleted members as a class with special members but no destructor, of which there is none to write.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct RuntimeDataDirectory final
{
    QString dir;

    RuntimeDataDirectory()
    {
#ifdef QMDMM_MACOS_APP_BUNDLE
        const QString configured = bundleApplicationDataDirectory() + u"/var"_s;
#else
        const QString configured = resolveConfiguredDirectory(u"" QMDMM_RUNTIME_DATA_PREFIX ""_s);
#endif

        if (QDir().mkpath(configured) && QFileInfo(configured).isWritable()) {
            dir = configured;
            return;
        }

        // An installation under a prefix owned by someone else is the ordinary reason the
        // directory above cannot be used, and the run would then have nowhere to report from.
        // Its own directory under the home is the second attempt; a run that can write to
        // neither is a run whose logs nobody will ever read.
        dir = QDir::home().absoluteFilePath(u".QMdmm/var"_s);

        if (QDir().mkpath(dir) && QFileInfo(dir).isWritable())
            return;

        qFatal("Runtime data can't be saved: neither %s nor %s can be created and written. Exiting.", qPrintable(configured), qPrintable(dir));
        Q_UNREACHABLE();
    }

    Q_DISABLE_COPY_MOVE(RuntimeDataDirectory);
};
} // namespace

/**
 * @brief Returns the directory the system-wide configuration file lives in
 * @return the configuration directory
 *
 * The directory comes from the build, from the platform or from the bundle, in three shapes.
 * A definition that holds an absolute path is used as it is -- the @c /usr and @c / prefixes,
 * where deriving one from the executable would land on the wrong side of the FHS. A
 * definition that holds anything else is relative to the directory the executable was
 * installed into, which is what lets an installed tree be moved afterwards without the
 * configuration staying behind. The application bundle has no such definition at all: it is
 * not installed under a prefix, so its directory is the platform's own place for an
 * application's data, under the name the bundle is identified by -- one directory for all
 * three programs, which is what makes it a system-wide configuration rather than three.
 *
 * It is looked up once, on the first call, and the same string is returned afterwards.
 */
const QString &Global::configurationDirectory()
{
    static ConfigurationDirectory i;

    return i.dir;
}

/**
 * @brief Returns the directory the runtime data -- the logs -- is written to
 * @return the runtime data directory
 *
 * Resolved the same way as @c configurationDirectory(), with @c var in place of @c etc, and
 * with one addition: this directory has to be writable, or the run would have nowhere to
 * report from. An installation under a prefix owned by someone else is the ordinary reason it
 * is not, and the second attempt is then @c $HOME/.QMdmm/var. A run that can write to neither
 * stops, rather than continuing as a process whose logs nobody will ever read. The directory
 * that is used is created here, so the caller may find it already in place.
 *
 * Both are looked up once, on the first call -- that is where the stopping happens too.
 */
const QString &Global::runtimeDataDirectory()
{
    static RuntimeDataDirectory i;

    return i.dir;
}

/**
 * @namespace QMdmmCore::Utilities
 * @headerfile <QMdmmUtilities>
 * @brief Convenience functions and types for working with QMdmm library
 */

/**
 * @class QMdmmCore::Utilities::DependentFalse
 * @ingroup QMdmmCore
 * @brief a workaround helper class for CWG2518
 *
 * Before CWG2518, `static_assert(false, "")` is ill-formed even if in a template which will never instantiate.
 *
 * Workaround is to use `static_assert(QMdmmCore::Utilities::dependentFalse<T>, "")` instead.
 */

/**
 * @var QMdmmCore::Utilities::dependentFalse
 * @brief a workaround helper variable for CWG2518, dependent on its template arguments
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

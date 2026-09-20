// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMCOREGLOBAL_H
#define QMDMMCOREGLOBAL_H

#include <QFlags>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QVersionNumber>
#include <QtGlobal>

#include <cstdint>
#include <iterator>
#include <type_traits>

#ifndef DOXYGEN
#ifndef QMDMM_STATIC
#ifdef QMDMMCORE_LIBRARY
#define QMDMMCORE_EXPORT Q_DECL_EXPORT
#else
#define QMDMMCORE_EXPORT Q_DECL_IMPORT
#endif
#else
#define QMDMMCORE_EXPORT
#endif
#ifdef QMDMM_NEED_EXPORT_PRIVATE
#define QMDMMCORE_PRIVATE_EXPORT QMDMMCORE_EXPORT
#else
#define QMDMMCORE_PRIVATE_EXPORT
#endif
#else
#define QMDMMCORE_EXPORT
#define QMDMMCORE_PRIVATE_EXPORT
#endif

#define QMDMM_EXPORT_NAME(QMdmmCoreGlobal)

namespace QMdmmCore {

#ifndef DOXYGEN
namespace p {
}
#endif

#ifndef DOXYGEN
namespace v0 {
}
inline namespace v1 {
}
#endif
} // namespace QMdmmCore

#include "qmdmmdebug.h" // NOLINT(misc-header-include-cycle): deliberate mutual include (guarded by include guards)

namespace QMdmmCore {
#ifndef DOXYGEN
namespace v0 {
#endif

namespace Data {

QMDMM_EXPORT_NAME(QMdmmData)

Q_NAMESPACE_EXPORT(QMDMMCORE_EXPORT)

enum Place : uint8_t
{
    Village = 0,
};
Q_ENUM_NS(Place)

enum DamageReason : uint8_t
{
    DamageReasonUnknown,
    Slashed,
    Kicked,
    HpPunished,
};
Q_ENUM_NS(DamageReason)

enum RockPaperScissors : uint8_t
{
    // The explicit values are the historical wire encoding carried over
    // from the former StoneScissorsCloth enum. Do not renumber: RPS
    // replies are serialized by value.
    Rock = 0,
    Paper = 2,
    Scissors = 1,
};
Q_ENUM_NS(RockPaperScissors)

enum Action : uint8_t
{
    DoNothing,
    BuyKnife,
    BuyHorse,
    Slash,
    Kick,
    Move,
    LetMove,
};
Q_ENUM_NS(Action)

enum UpgradeItem : uint8_t
{
    UpgradeKnife,
    UpgradeHorse,
    UpgradeMaxHp,
};
Q_ENUM_NS(UpgradeItem)

enum AgentStateEnum : uint8_t
{
    StateMaskOnline = 0x10,
    StateMaskBot = 0x01,
    StateMaskTrust = 0x08,

    StateOffline = 0x0,
    StateOfflineBot = 0x01,
    StateOnline = 0x10,
    StateOnlineBot = 0x11,
    StateOnlineTrust = 0x18,
};
Q_DECLARE_FLAGS(AgentState, AgentStateEnum)
Q_FLAG_NS(AgentState)

[[nodiscard]] constexpr bool isPlaceAdjacent(int p1, int p2) noexcept
{
    // simplifies to "only one of p1 and p2 is Village"
    // simplifies again to "p1 is Village xor p2 is Village"
    // (boolean xor == notequal)
    return (p1 == Village) != (p2 == Village);
}

[[nodiscard]] QMDMMCORE_EXPORT QStringList rockPaperScissorsWinners(const QHash<QString, Data::RockPaperScissors> &judgers);

} // namespace Data

namespace Global {
QMDMM_EXPORT_NAME(QMdmmGlobal)
[[nodiscard]] QMDMMCORE_EXPORT QVersionNumber version();
} // namespace Global

namespace Utilities {
QMDMM_EXPORT_NAME(QMdmmUtilities)

template<typename...>
struct DependentFalse : std::false_type
{
};

template<typename... Args>
inline constexpr bool dependentFalse = DependentFalse<Args...>::value;

template<typename T>
[[nodiscard]]
auto list2Set(const T &l)
{
    using it = std::iterator_traits<decltype(std::cbegin(l))>;
    return QSet<typename std::remove_cv_t<typename it::value_type>>(std::cbegin(l), std::cend(l));
}

template<typename T>
[[nodiscard]] QVariantList enumList2VariantList(const QList<T> &list)
{
    static_assert(std::is_enum_v<T>);

    QVariantList ret;
    ret.reserve(list.length());
    foreach (T i, list)
        ret << static_cast<int>(i);
    return ret;
}

template<typename T>
[[nodiscard]] QVariantList enumList2VariantList(const QList<QFlags<T>> &list)
{
    QVariantList ret;
    ret.reserve(list.length());
    foreach (const QFlags<T> &i, list)
        ret << static_cast<int>(typename QFlags<T>::Int(i));
    return ret;
}
[[nodiscard]] QMDMMCORE_EXPORT QVariantList intList2VariantList(const QList<int> &list);
[[nodiscard]] QMDMMCORE_EXPORT QList<int> variantList2IntList(const QVariantList &list);
[[nodiscard]] QMDMMCORE_EXPORT QVariantList stringList2VariantList(const QList<QString> &list);
[[nodiscard]] QMDMMCORE_EXPORT QStringList variantList2StringList(const QVariantList &list);

} // namespace Utilities

#ifndef DOXYGEN
} // namespace v0

inline namespace v1 {
namespace Data = v0::Data; // NOLINT(misc-unused-alias-decls)
namespace Global = v0::Global; // NOLINT(misc-unused-alias-decls)
namespace Utilities = v0::Utilities; // NOLINT(misc-unused-alias-decls)
} // namespace v1
#endif

} // namespace QMdmmCore

#endif // QMDMMCOREGLOBAL_H

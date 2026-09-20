// SPDX-License-Identifier: AGPL-3.0-or-later

#include "config.h"

#include <QMdmmSettings>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {
// The giant help literal lives in a function-local static so that a failed allocation surfaces
// at the call site instead of terminating the process during static initialization (cert-err58-cpp).
const QString &helpText()
{
    static const QString text = uR"help(Usage: QMdmmServer [options]

Options:
  -h, --help                         Show this help text and exit.
  -v, --version                      Show version information and exit.

Network transports (each can be enabled or disabled independently):
  -t, --tcp <on/off>                 Enable the TCP server (default: on).
  -p, --tcp-port <port>              TCP listen port (default: 6366).
  -l, --local <on/off>               Enable the local socket server (default: on).
  -L, --local-name <name>            Local socket name (default: "QMdmm").
  -w, --websocket <on/off>           Enable the WebSocket server (default: on).
  -W, --websocket-name <name>        WebSocket name (default: "QMdmm").
  -P, --websocket-port <port>        WebSocket listen port (default: 6367).

Room and connection:
  -n, --players <2~>                 Player number per room (default: 3). Nine is the soft cap:
                                     above it rock-paper-scissors ties become more likely.
  -2, -3, -4, -5, -6, -7, -8, -9     Shorthand for --players=<N> (for example -4 is equivalent
                                     to --players=4).
  -o, --timeout <0,15~>              Operation timeout in seconds; 0 disables the timeout
                                     (default: 20).

Logic:
  -s, --slash, --knife <1~>          Initial knife (slash) damage (default: 1).
  -S, --maximum-slash, --maximum-knife <3~>
                                     Maximum knife (slash) damage (default: 10).
  -k, --kick, --horse <2~>           Initial horse (kick) damage (default: 2).
  -K, --maximum-kick, --maximum-horse <5~>
                                     Maximum horse (kick) damage (default: 10).
  -m, --maxhp <7~>                   Initial max HP (default: 10).
  -M, --maximum-maxhp <7~>           Maximum max HP (default: 20).
  -r, --punish-hp-modifier <0,2~>    HP punish modifier; 0 disables the punishment (default: 2).
  -R, --punish-hp-round-strategy <strategy>
                                     How punished HP is rounded (default: RoundToNearest45).
                                     One of:
                                       RoundDown           round down (1.5 -> 1)
                                       PlusOne             round down, then add 1 (1.5 -> 2)
                                       RoundUp             round up (1.1 -> 2)
                                       RoundToNearest45    round to nearest (1.4 -> 1, 1.5 -> 2)
  -z, --zero-hp-as-dead <true/false>
                                     Treat zero HP as dead (default: true).
  -f, --enable-let-move <true/false>
                                     Enable "let move" (default: true).
  -i, --can-buy-only-in-initial-city <true/false>
                                     Only allow buying in the initial city (default: false).
  -1, --use-v1-presets               Use the v1 presets instead of the defaults
                                     (explicit options still override):
                                       slash                           1
                                       maximum-slash                   3
                                       kick                            3
                                       maximum-kick                    5
                                       maxhp                           7
                                       maximum-maxhp                   7
                                       punish-hp-modifier              0
                                       punish-hp-round-strategy        RoundToNearest45
                                       zero-hp-as-dead                 false
                                       enable-let-move                 false
                                       can-buy-only-in-initial-city    false

Configuration save / inspect:
  -c, --save-configuration           Save the full resolved configuration (all items, defaults
                                     included) to the per-user scope and exit.
  -C, --save-global-configuration    Save the full resolved configuration (all items, defaults
                                     included) to the system-global scope and exit.
  -d, --show-current-configuration   Print the current configuration as JSON.

Value ranges: <min~> means "at least min"; <0,min~> means "0, or at least min".
Each maximum must be at least its own initial value, so for example --slash must not
exceed --maximum-slash.
)help"_s;
    return text;
}
} // namespace

// NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if)
#if 0
ab  e g  j    q  u  xy
AB DEFGHIJ NO Q TUV XYZ
0
#endif

// Reports a fatal configuration error and exits with code 3.
//
// A bad command-line option or config-file value is a user error, not a program bug.
// Unlike qFatal() (which aborts with SIGABRT and looks like a crash), we print the
// message to stderr (the terminal is the only place the user is looking), also record
// it through qWarning() (so it lands in the log file via the installed message handler),
// and then exit cleanly.
//
// The vararg template + QString::arg() chain deliberately avoids a C-style variadic
// function and the va_list type: the project forbids writing both (calling third-party
// variadic functions such as qWarning() stays allowed). Callers pass a QString::arg-style
// format ("%1", "%2", ...) instead of a printf-style one ("%s", "%d", ...).
namespace {

[[noreturn]] void configErrorImpl(const QString &message)
{
    std::cerr << qPrintable(message) << '\n' << std::flush;
    qWarning().noquote() << message;

    std::exit(3);
}

// Applies each argument to the QString::arg() chain in order. Recursive (rather than a fold or an
// initializer_list expansion) so every forwarding-reference argument is truly std::forwarded once,
// which is what cppcoreguidelines-missing-std-forward asks for.
inline void configErrorArgs(QString &message)
{
    Q_UNUSED(message);
}

template<typename T, typename... Rest>
void configErrorArgs(QString &message, T &&arg, Rest &&...rest)
{
    if constexpr (requires { message.arg(std::forward<T>(arg)); })
        message = message.arg(std::forward<T>(arg));
    else if constexpr (requires { QAnyStringView(std::forward<T>(arg)); })
        message = message.arg(QAnyStringView(std::forward<T>(arg)).toString());
    else
        static_assert(QMdmmCore::Utilities::dependentFalse<T>, "configError: You are passing arguments which can't be passed to QString::arg.");
    configErrorArgs(message, std::forward<Rest>(rest)...);
}

template<typename... Args>
[[noreturn]] void configError(const QString &format, Args &&...args)
{
    QString message = format;
    configErrorArgs(message, std::forward<Args>(args)...);
    configErrorImpl(message);
}

} // namespace

Config::Config()
{
    QMdmmCore::Settings setting;

    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsCompactedShortOptions);

    // parser_.addHelpOption(); // do not fit my need
    parser.addOption(QCommandLineOption(QStringList {u"h"_s, u"help"_s}));
    parser.addVersionOption();

    parser.addOption(QCommandLineOption(QStringList {u"t"_s, u"tcp"_s}, {}, u"on/off"_s));
    parser.addOption(QCommandLineOption(QStringList {u"p"_s, u"tcp-port"_s}, {}, u"port"_s));

    parser.addOption(QCommandLineOption(QStringList {u"l"_s, u"local"_s}, {}, u"on/off"_s));
    parser.addOption(QCommandLineOption(QStringList {u"L"_s, u"local-name"_s}, {}, u"name"_s));

    parser.addOption(QCommandLineOption(QStringList {u"w"_s, u"websocket"_s}, {}, u"on/off"_s));
    parser.addOption(QCommandLineOption(QStringList {u"W"_s, u"websocket-name"_s}, {}, u"name"_s));
    parser.addOption(QCommandLineOption(QStringList {u"P"_s, u"websocket-port"_s}, {}, u"port"_s));

    parser.addOption(QCommandLineOption(QStringList {u"n"_s, u"players"_s}, {}, u"2~"_s));

    parser.addOption(QCommandLineOption(QStringList {u"2"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"3"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"4"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"5"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"6"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"7"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"8"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"9"_s}));

    parser.addOption(QCommandLineOption(QStringList {u"o"_s, u"timeout"_s}, {}, u"0,15~"_s));

    parser.addOption(QCommandLineOption(QStringList {u"s"_s, u"slash"_s, u"knife"_s}, {}, u"1~"_s));
    parser.addOption(QCommandLineOption(QStringList {u"S"_s, u"maximum-slash"_s, u"maximum-knife"_s}, {}, u"3~"_s));
    parser.addOption(QCommandLineOption(QStringList {u"k"_s, u"kick"_s, u"horse"_s}, {}, u"2~"_s));
    parser.addOption(QCommandLineOption(QStringList {u"K"_s, u"maximum-kick"_s, u"maximum-horse"_s}, {}, u"5~"_s));
    parser.addOption(QCommandLineOption(QStringList {u"m"_s, u"maxhp"_s}, {}, u"7~"_s));
    parser.addOption(QCommandLineOption(QStringList {u"M"_s, u"maximum-maxhp"_s}, {}, u"7~"_s));
    parser.addOption(QCommandLineOption(QStringList {u"r"_s, u"punish-hp-modifier"_s}, {}, u"0,2~"_s));
    parser.addOption(QCommandLineOption(QStringList {u"R"_s, u"punish-hp-round-strategy"_s}, {}, u"strategy"_s));
    parser.addOption(QCommandLineOption(QStringList {u"z"_s, u"zero-hp-as-dead"_s}, {}, u"true/false"_s));
    parser.addOption(QCommandLineOption(QStringList {u"f"_s, u"enable-let-move"_s}, {}, u"true/false"_s));
    parser.addOption(QCommandLineOption(QStringList {u"i"_s, u"can-buy-only-in-initial-city"_s}, {}, u"true/false"_s));

    parser.addOption(QCommandLineOption(QStringList {u"1"_s, u"use-v1-presets"_s}));

    parser.addOption(QCommandLineOption(QStringList {u"c"_s, u"save-configuration"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"C"_s, u"save-global-configuration"_s}));
    parser.addOption(QCommandLineOption(QStringList {u"d"_s, u"show-current-configuration"_s}));

    parser.process(*qApp);

    if (!parser.positionalArguments().isEmpty())
        configError(u"Unknown argument: %1"_s, parser.positionalArguments().join(u", "_s));

    if (parser.isSet(u"h"_s)) {
        std::cout << qPrintable(helpText()) << std::flush;
        std::exit(0);
    }

    QMdmmCore::Settings::Instance toSave = QMdmmCore::Settings::Specified;

    if (parser.isSet(u"c"_s)) {
        if (parser.isSet(u"C"_s))
            configError(u"It is not supported to save both per-user configuration and global configuration at one time. Exiting."_s);
        toSave = QMdmmCore::Settings::PerUser;
    } else if (parser.isSet(u"C"_s)) {
        toSave = QMdmmCore::Settings::Global;
    }

    read_(&setting, &parser);
    bool isShowSet = parser.isSet(u"d"_s);
    if (isShowSet)
        show_();
    if (toSave != QMdmmCore::Settings::Specified)
        // save_() returns saveConfig()'s QSettings::Status cast to int, and that value is used
        // directly as the process exit code: NoError exits 0, any error status exits non-zero.
        std::exit(save_(&setting, toSave));
    if (isShowSet)
        std::exit(0);
}

namespace {

inline std::optional<bool> stringToBool(const QString &value)
{
    // clang-format off
    static const QStringList falseValues {
        u"0"_s,
        u"false"_s,
        u"no"_s,
        u"n"_s,
        u"off"_s,
        u"disable"_s,
        u"disabled"_s,
    };
    static const QStringList trueValues {
        u"1"_s,
        u"true"_s,
        u"yes"_s,
        u"y"_s,
        u"on"_s,
        u"enable"_s,
        u"enabled"_s,
    };
    // clang-format on

    foreach (const QString &falseValue, falseValues) {
        if (value.compare(falseValue, Qt::CaseInsensitive) == 0)
            return false;
    }
    foreach (const QString &trueValue, trueValues) {
        if (value.compare(trueValue, Qt::CaseInsensitive) == 0)
            return true;
    }
    return std::nullopt;
}

inline std::optional<int> stringToInt(const QString &value)
{
    bool ok = false;
    int result = value.toInt(&ok);
    if (!ok)
        return std::nullopt;

    return result;
}

inline std::optional<uint16_t> stringToUint16(const QString &value)
{
    bool ok = false;
    unsigned int result = value.toUInt(&ok);
    if (!ok)
        return std::nullopt;
    if (result > UINT16_MAX)
        return std::nullopt;

    return result;
}

inline std::optional<QMdmmCore::LogicConfiguration::PunishHpRoundStrategy> stringToPunishHpRoundStrategy(const QString &value)
{
    static const QHash<QString, QMdmmCore::LogicConfiguration::PunishHpRoundStrategy> strategyHash {
        std::make_pair(u"RoundDown"_s, QMdmmCore::LogicConfiguration::RoundDown),
        std::make_pair(u"RoundToNearest45"_s, QMdmmCore::LogicConfiguration::RoundToNearest45),
        std::make_pair(u"RoundUp"_s, QMdmmCore::LogicConfiguration::RoundUp),
        std::make_pair(u"PlusOne"_s, QMdmmCore::LogicConfiguration::PlusOne),
    };

    for (QHash<QString, QMdmmCore::LogicConfiguration::PunishHpRoundStrategy>::const_iterator it = strategyHash.constBegin(); it != strategyHash.constEnd(); ++it) {
        if (it.key().compare(value, Qt::CaseInsensitive) == 0)
            return it.value();
    }

    return std::nullopt;
}

inline QString boolToString(bool value)
{
    return value ? u"on"_s : u"off"_s;
}

inline QString intToString(int value)
{
    return QString::number(value);
}

inline QString uint16ToString(uint16_t value)
{
    return QString::number(static_cast<unsigned int>(value));
}

inline QString punishHpRoundStrategyToString(QMdmmCore::LogicConfiguration::PunishHpRoundStrategy value)
{
    static const QHash<QMdmmCore::LogicConfiguration::PunishHpRoundStrategy, QString> strategyHash {
        std::make_pair(QMdmmCore::LogicConfiguration::RoundDown, u"RoundDown"_s),
        std::make_pair(QMdmmCore::LogicConfiguration::RoundToNearest45, u"RoundToNearest45"_s),
        std::make_pair(QMdmmCore::LogicConfiguration::RoundUp, u"RoundUp"_s),
        std::make_pair(QMdmmCore::LogicConfiguration::PlusOne, u"PlusOne"_s),
    };

    // Fall back to the default strategy for unknown values so that a write-back never emits an
    // empty string (which the next startup would reject and exit on). The deserialize() path
    // already rejects out-of-range values; this keeps the serialize side equally total.
    return strategyHash.value(value, u"RoundToNearest45"_s);
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void Config::read_(QMdmmCore::Settings *setting, QCommandLineParser *parser)
{
    // NOLINTBEGIN(cppcoreguidelines-avoid-do-while,cppcoreguidelines-macro-usage)
#define CONFIG_ITEM(type, conf, settingName, parserConvert, ValueName)                          \
    do {                                                                                        \
        QString s;                                                                              \
        int f = 0;                                                                              \
        if (parser->isSet(u"" settingName ""_s)) {                                              \
            f = 1;                                                                              \
            s = parser->value(u"" settingName ""_s);                                            \
        } else if (setting->contains(u"" settingName ""_s)) {                                   \
            f = 2;                                                                              \
            s = setting->value(u"" settingName ""_s).toString();                                \
        }                                                                                       \
        if (f != 0) {                                                                           \
            std::optional<type> v = parserConvert(s);                                           \
            if (v.has_value()) {                                                                \
                (conf).set##ValueName(v.value());                                               \
            } else {                                                                            \
                const char *from = nullptr;                                                     \
                if (f == 1)                                                                     \
                    from = "command line";                                                      \
                else if (f == 2)                                                                \
                    from = "config file";                                                       \
                else                                                                            \
                    from = "unknown config";                                                    \
                configError(u"Config item %1 (from %2) can't be parsed."_s, settingName, from); \
            }                                                                                   \
        }                                                                                       \
    } while (false)

    setting->beginGroup(u"server"_s);

    CONFIG_ITEM(bool, serverConfiguration_, "tcp", stringToBool, TcpEnabled);
    CONFIG_ITEM(uint16_t, serverConfiguration_, "tcp-port", stringToUint16, TcpPort);
    CONFIG_ITEM(bool, serverConfiguration_, "local", stringToBool, LocalEnabled);
    CONFIG_ITEM(QString, serverConfiguration_, "local-name", , LocalSocketName);
    CONFIG_ITEM(bool, serverConfiguration_, "websocket", stringToBool, WebsocketEnabled);
    CONFIG_ITEM(QString, serverConfiguration_, "websocket-name", , WebsocketName);
    CONFIG_ITEM(uint16_t, serverConfiguration_, "websocket-port", stringToUint16, WebsocketPort);
    CONFIG_ITEM(int, serverConfiguration_, "timeout", stringToInt, RequestTimeout);

    {
        int players = 0;

        // clang-format off
        static const std::array<QString, 8> shortForms {
            u"2"_s,
            u"3"_s,
            u"4"_s,
            u"5"_s,
            u"6"_s,
            u"7"_s,
            u"8"_s,
            u"9"_s,
        };
        // clang-format on

        for (size_t i = 0; i < shortForms.size(); ++i) {
            if (parser->isSet(shortForms.at(i))) {
                if (players == 0)
                    players = static_cast<int>(i + 2);
                else
                    configError(u"-%1 can't be specified along with -%2"_s, static_cast<int>(i + 2), players);
            }
        }

        if (players != 0) {
            if (parser->isSet(u"players"_s))
                configError(u"-%1 can't be specified along with -n / --players"_s, players);

            serverConfiguration_.setPlayerNumPerRoom(players);
        } else {
            CONFIG_ITEM(int, serverConfiguration_, "players", stringToInt, PlayerNumPerRoom);
        }
    }

    // Validate the resolved room size. A game needs at least two players (rock-paper-scissors
    // action-order resolution requires an opponent), so a size below 2 is rejected outright.
    // Anything above 9 is still allowed, but 9 is the soft cap: every extra player inflates the
    // chance of a rock-paper-scissors tie during action-order resolution, so warn without rejecting.
    const int roomSize = serverConfiguration_.playerNumPerRoom();
    if (roomSize < 2)
        configError(u"players must be at least 2 (got %1)"_s, roomSize);
    if (roomSize > 9)
        qWarning("players %d exceeds the soft cap of 9 (rock-paper-scissors ties become more likely)", roomSize);

    setting->endGroup();

    setting->beginGroup(u"logic"_s);

    if (parser->isSet(u"1"_s))
        logicConfiguration_ = QMdmmCore::LogicConfiguration::v1();

    CONFIG_ITEM(int, logicConfiguration_, "slash", stringToInt, InitialKnifeDamage);
    CONFIG_ITEM(int, logicConfiguration_, "maximum-slash", stringToInt, MaximumKnifeDamage);
    CONFIG_ITEM(int, logicConfiguration_, "kick", stringToInt, InitialHorseDamage);
    CONFIG_ITEM(int, logicConfiguration_, "maximum-kick", stringToInt, MaximumHorseDamage);
    CONFIG_ITEM(int, logicConfiguration_, "maxhp", stringToInt, InitialMaxHp);
    CONFIG_ITEM(int, logicConfiguration_, "maximum-maxhp", stringToInt, MaximumMaxHp);
    CONFIG_ITEM(int, logicConfiguration_, "punish-hp-modifier", stringToInt, PunishHpModifier);
    CONFIG_ITEM(QMdmmCore::LogicConfiguration::PunishHpRoundStrategy, logicConfiguration_, "punish-hp-round-strategy", stringToPunishHpRoundStrategy, PunishHpRoundStrategy);
    CONFIG_ITEM(bool, logicConfiguration_, "zero-hp-as-dead", stringToBool, ZeroHpAsDead);
    CONFIG_ITEM(bool, logicConfiguration_, "enable-let-move", stringToBool, EnableLetMove);
    CONFIG_ITEM(bool, logicConfiguration_, "can-buy-only-in-initial-city", stringToBool, CanBuyOnlyInInitialCity);

    setting->endGroup();

    // Validate numeric values against the ranges promised by --help. Running these checks
    // after the CONFIG_ITEM pass means both command-line and config-file values are covered,
    // so a config file that stores an out-of-range value is rejected the same as a bad flag.
    struct RangeCheck
    {
        int value;
        int min;
        const char *name;
    };
    const std::array rangeChecks = std::to_array<RangeCheck>({
        {.value = logicConfiguration_.initialKnifeDamage(), .min = 1, .name = "slash"},
        {.value = logicConfiguration_.maximumKnifeDamage(), .min = 3, .name = "maximum-slash"},
        {.value = logicConfiguration_.initialHorseDamage(), .min = 2, .name = "kick"},
        {.value = logicConfiguration_.maximumHorseDamage(), .min = 5, .name = "maximum-kick"},
        {.value = logicConfiguration_.initialMaxHp(), .min = 7, .name = "maxhp"},
        {.value = logicConfiguration_.maximumMaxHp(), .min = 7, .name = "maximum-maxhp"},
    });
    for (const RangeCheck &check : rangeChecks) {
        if (check.value < check.min)
            configError(u"Config item %1 must be at least %2 (got %3)"_s, check.name, check.min, check.value);
    }

    // Cross-item validation. The minimums above judge every value on its own, so they cannot see a
    // pair whose members are each in range but contradict one another (--slash 15 with
    // --maximum-slash 7, say). Such a pair is self-inflicting rather than merely odd: an attribute
    // grows within [initial, maximum], so inverting the pair leaves that attribute with no room to
    // grow at all -- and Room::isGameOver() declares a player whose three attributes are all
    // maxed out the winner of the game. Rejecting the three pairs here keeps the command line, the
    // config file and LogicConfiguration::deserialize() telling the user the same story.
    struct CrossCheck
    {
        int initial;
        int maximum;
        const char *initialName;
        const char *maximumName;
    };
    const std::array crossChecks = std::to_array<CrossCheck>({
        {.initial = logicConfiguration_.initialKnifeDamage(), .maximum = logicConfiguration_.maximumKnifeDamage(), .initialName = "slash", .maximumName = "maximum-slash"},
        {.initial = logicConfiguration_.initialHorseDamage(), .maximum = logicConfiguration_.maximumHorseDamage(), .initialName = "kick", .maximumName = "maximum-kick"},
        {.initial = logicConfiguration_.initialMaxHp(), .maximum = logicConfiguration_.maximumMaxHp(), .initialName = "maxhp", .maximumName = "maximum-maxhp"},
    });
    for (const CrossCheck &check : crossChecks) {
        if (check.initial > check.maximum)
            configError(u"Config item %1 must not exceed %2 (got %3 and %4)"_s, check.initialName, check.maximumName, check.initial, check.maximum);
    }

    const int timeout = serverConfiguration_.requestTimeout();
    if (timeout != 0 && timeout < 15)
        configError(u"Config item timeout must be 0 or at least 15 (got %1)"_s, timeout);

    const int punishHpModifier = logicConfiguration_.punishHpModifier();
    if (punishHpModifier != 0 && punishHpModifier < 2)
        configError(u"Config item punish-hp-modifier must be 0 or at least 2 (got %1)"_s, punishHpModifier);

#undef CONFIG_ITEM
}

int Config::save_(QMdmmCore::Settings *setting, QMdmmCore::Settings::Instance toSave)
{
    // NOLINTBEGIN(bugprone-macro-parentheses)

#define CONFIG_ITEM(type, conf, settingName, settingConvert, valueName) \
    do {                                                                \
        type v = conf.valueName();                                      \
        setting->setValue(u"" settingName ""_s, settingConvert(v));     \
    } while (false)

    // NOLINTEND(bugprone-macro-parentheses)

    setting->beginGroup(u"server"_s);

    CONFIG_ITEM(bool, serverConfiguration_, "tcp", boolToString, tcpEnabled);
    CONFIG_ITEM(uint16_t, serverConfiguration_, "tcp-port", uint16ToString, tcpPort);
    CONFIG_ITEM(bool, serverConfiguration_, "local", boolToString, localEnabled);
    CONFIG_ITEM(QString, serverConfiguration_, "local-name", , localSocketName);
    CONFIG_ITEM(bool, serverConfiguration_, "websocket", boolToString, websocketEnabled);
    CONFIG_ITEM(QString, serverConfiguration_, "websocket-name", , websocketName);
    CONFIG_ITEM(uint16_t, serverConfiguration_, "websocket-port", uint16ToString, websocketPort);
    CONFIG_ITEM(int, serverConfiguration_, "timeout", intToString, requestTimeout);
    CONFIG_ITEM(int, serverConfiguration_, "players", intToString, playerNumPerRoom);

    setting->endGroup();

    setting->beginGroup(u"logic"_s);

    CONFIG_ITEM(int, logicConfiguration_, "slash", intToString, initialKnifeDamage);
    CONFIG_ITEM(int, logicConfiguration_, "maximum-slash", intToString, maximumKnifeDamage);
    CONFIG_ITEM(int, logicConfiguration_, "kick", intToString, initialHorseDamage);
    CONFIG_ITEM(int, logicConfiguration_, "maximum-kick", intToString, maximumHorseDamage);
    CONFIG_ITEM(int, logicConfiguration_, "maxhp", intToString, initialMaxHp);
    CONFIG_ITEM(int, logicConfiguration_, "maximum-maxhp", intToString, maximumMaxHp);
    CONFIG_ITEM(int, logicConfiguration_, "punish-hp-modifier", intToString, punishHpModifier);
    CONFIG_ITEM(QMdmmCore::LogicConfiguration::PunishHpRoundStrategy, logicConfiguration_, "punish-hp-round-strategy", punishHpRoundStrategyToString, punishHpRoundStrategy);
    CONFIG_ITEM(bool, logicConfiguration_, "zero-hp-as-dead", boolToString, zeroHpAsDead);
    CONFIG_ITEM(bool, logicConfiguration_, "enable-let-move", boolToString, enableLetMove);
    CONFIG_ITEM(bool, logicConfiguration_, "can-buy-only-in-initial-city", boolToString, canBuyOnlyInInitialCity);

    setting->endGroup();

#undef CONFIG_ITEM

    return static_cast<int>(setting->saveConfig(toSave));
    // NOLINTEND(cppcoreguidelines-avoid-do-while,cppcoreguidelines-macro-usage)
}

void Config::show_()
{
    QJsonObject ob;
    ob.insert(u"ServerConfiguration"_s, serverConfiguration_);
    ob.insert(u"LogicConfiguration"_s, logicConfiguration_);

    QByteArray arr = QJsonDocument(ob).toJson(QJsonDocument::Indented);
    std::cout << arr.constData() << '\n' << std::flush;
}

const QMdmmNetworking::ServerConfiguration &Config::serverConfiguration() const
{
    return serverConfiguration_;
}

const QMdmmCore::LogicConfiguration &Config::logicConfiguration() const
{
    return logicConfiguration_;
}

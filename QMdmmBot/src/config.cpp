// SPDX-License-Identifier: AGPL-3.0-or-later

#include "config.h"
#include "bot.h"

#include <QMdmmSettings>

#include <QCommandLineParser>
#include <QHash>

#include <iostream>
#include <optional>

using namespace Qt::StringLiterals;

namespace {
// The giant help literal lives in a function-local static so that a failed allocation surfaces
// at the call site instead of terminating the process during static initialization (cert-err58-cpp).
const QString &helpText()
{
    static const QString text = uR"help(Usage: QMdmmBot [options]

Options:
  -h, --help                         Show this help text and exit.
  -v, --version                      Show version information and exit.

Connection:
  -l, --host <host url>              Server address to connect to (required).
  -n, --name <screen name>           Screen name shown to other players (default: empty).

Bot:
  -s, --playing-style <style>        Playing style of this bot (default: knifePreferred).
                                     One of:
                                       knifePreferred      prefer the knife
                                       horsePreferred      prefer the horse
                                       rl                  reinforcement learning (not implemented; exits
                                                           at startup)

Logging:
  --log-level <level>                Lowest level copied from the log to the terminal (default:
                                     off). The log file keeps every line either way, so this
                                     decides what a terminal shows rather than what is
                                     recorded. One of:
                                       off                 nothing
                                       debug               debug and above
                                       info                info and above
                                       warning             warning and above
                                       critical            critical and above
)help"_s;
    return text;
}
} // namespace

namespace {

[[noreturn]] void configErrorImpl(const QString &message)
{
    std::cerr << qPrintable(message) << '\n' << std::flush;
    qWarning().noquote() << message;

    std::exit(3);
}

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

namespace {

std::optional<QMdmmCore::LogLevel> stringToLogLevel(const QString &value)
{
    static const QHash<QString, QMdmmCore::LogLevel> levelHash {
        std::make_pair(u"off"_s, QMdmmCore::LogLevel::Off),           std::make_pair(u"debug"_s, QMdmmCore::LogLevel::Debug),
        std::make_pair(u"info"_s, QMdmmCore::LogLevel::Info),         std::make_pair(u"warning"_s, QMdmmCore::LogLevel::Warning),
        std::make_pair(u"critical"_s, QMdmmCore::LogLevel::Critical),
    };

    for (QHash<QString, QMdmmCore::LogLevel>::const_iterator it = levelHash.constBegin(); it != levelHash.constEnd(); ++it) {
        if (it.key().compare(value, Qt::CaseInsensitive) == 0)
            return it.value();
    }

    return std::nullopt;
}

} // namespace

Config::Config(const QStringList &arguments)
    : logLevel_(QMdmmCore::LogLevel::Off)
{
    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsCompactedShortOptions);

    parser.addOption(QCommandLineOption(QStringList {u"h"_s, u"help"_s}));
    parser.addVersionOption();

    parser.addOption(QCommandLineOption(QStringList {u"l"_s, u"host"_s}, {}, u"host url"_s));
    parser.addOption(QCommandLineOption(QStringList {u"n"_s, u"name"_s}, {}, u"Screen Name"_s));

    parser.addOption(QCommandLineOption(QStringList {u"s"_s, u"playing-style"_s}, {}, u"playing style"_s));

    parser.addOption(QCommandLineOption(QStringList {u"log-level"_s}, {}, u"level"_s));

    parser.process(arguments);

    if (!parser.positionalArguments().isEmpty())
        configError(u"Unknown argument: %1"_s, parser.positionalArguments().join(u", "_s));

    if (parser.isSet(u"h"_s)) {
        std::cout << qPrintable(helpText()) << std::flush;
        std::exit(0);
    }

    read_(&parser);
}

void Config::read_(QCommandLineParser *parser)
{
    if (!parser->isSet(u"host"_s))
        configError(u"Host is required."_s);
    host_ = parser->value(u"host"_s);

    if (parser->isSet(u"name"_s))
        name_ = parser->value(u"name"_s);

    playingStyle_ = u"knifePreferred"_s;
    if (parser->isSet(u"playing-style"_s))
        playingStyle_ = parser->value(u"playing-style"_s);

    // Validate the resolved style uniformly (default included), so the
    // whitelist in Bot::styleExist is the single source of truth.
    if (!Bot::styleExist(playingStyle_))
        configError(u"Specified playing style does not exist."_s);

    // The level decides how much of the log the terminal is told; the file the run opened keeps
    // everything either way. main() puts it on, so this half only reads it.
    if (parser->isSet(u"log-level"_s)) {
        const QString value = parser->value(u"log-level"_s);
        const std::optional<QMdmmCore::LogLevel> level = stringToLogLevel(value);
        if (!level.has_value())
            configError(u"Specified log level does not exist."_s);
        logLevel_ = level.value();
    }
}

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmdebug.h"
#include "qmdmmdebug_p.h"

#include <QDebug>
#include <QHash>
#include <QIODevice>

#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

/**
 * @file qmdmmdebug.h
 * @brief QMdmm Debug stuff
 */

namespace QMdmmCore {
#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @enum LogLevel
 * @ingroup QMdmmCore
 * @brief How much of the log is copied out to the console.
 *
 * The levels name the four kinds of line a run writes -- debug, information, warning, critical --
 * and @c Off, which copies nothing. A level is the lowest one that is copied: @c Warning copies
 * warnings and criticals and leaves the two below them to the file.
 */

/**
 * @var LogLevel LogLevel::Off
 * @brief Nothing is copied out; the log file is the only reader.
 *
 * @var LogLevel LogLevel::Debug
 * @brief Every line is copied out.
 *
 * @var LogLevel LogLevel::Info
 * @brief Information lines, warnings and criticals are copied out.
 *
 * @var LogLevel LogLevel::Warning
 * @brief Warnings and criticals are copied out.
 *
 * @var LogLevel LogLevel::Critical
 * @brief Only criticals are copied out.
 */

/**
 * @brief Set the device where QDebug outputs log to.
 * @param f The target output device
 *
 * By default Qt outputs log to a Qt defined buffer. This changes the buffer to our one, for collecting the log we generates
 *
 * A line written to the device carries the time, the level and the category ahead of the message, so that a log read
 * after the fact says when each line was written and how much of it to believe.
 *
 * The device is the record: it gets every line, whatever qMdmmDebugSetConsoleLevel() says. What
 * the terminal is told is the filtered copy of it, and with no device set the terminal is the
 * only reader a line has, so it keeps everything.
 */

void qMdmmDebugSetDevice(QIODevice *f)
{
    if (f != nullptr) {
        if (!f->isOpen())
            f->open(QIODevice::WriteOnly);
    }

    p::debugLogInstance()->f = f;
    if (p::DebugLogP::qtMessageHandler == nullptr) {
        // Without a pattern a line is the message alone: no time, no level and no category.
        // The shape belongs with the handler that produces the lines, so it is decided here,
        // where this module takes message output over.
        qSetMessagePattern(u"%{time yyyy-MM-ddTHH:mm:ss.zzz} %{type} %{category}: %{message}"_s);
        p::DebugLogP::qtMessageHandler = qInstallMessageHandler(&p::messageOutput);
    }
}

/**
 * @brief Set how much of the log is copied out to the console.
 * @param level The lowest level that is copied out
 *
 * This picks the console copy, not the record: with a device set the file keeps every line
 * whatever @p level says. Setting a level does not install the message handler --
 * qMdmmDebugSetDevice() does that -- and a run that never sets a device has nothing to filter,
 * so its terminal keeps every line as it did before.
 *
 * A run starts at @c LogLevel::Off, which is what setting a device used to mean: the file and
 * nothing on the terminal.
 */

void qMdmmDebugSetConsoleLevel(LogLevel level)
{
    p::debugLogInstance()->consoleLevel = level;
}

/**
 * @brief Read a level from the name it goes by on the command line.
 * @param value The name to read
 * @return The level @p value names, or nothing if it names none of them
 *
 * The five names are the ones @c --log-level takes, and case is not part of a name: @c Warning and
 * @c warning are the same level. This is the only place they are written down -- a program that
 * kept its own copy of them would go on refusing a name the level had gained, or go on accepting
 * one it had lost, and nothing would report the difference.
 */
std::optional<LogLevel> qMdmmDebugLogLevelFromString(const QString &value)
{
    static const QHash<QString, LogLevel> levelHash {
        std::make_pair(u"off"_s, LogLevel::Off),         std::make_pair(u"debug"_s, LogLevel::Debug),       std::make_pair(u"info"_s, LogLevel::Info),
        std::make_pair(u"warning"_s, LogLevel::Warning), std::make_pair(u"critical"_s, LogLevel::Critical),
    };

    for (QHash<QString, LogLevel>::const_iterator it = levelHash.constBegin(); it != levelHash.constEnd(); ++it) {
        if (it.key().compare(value, Qt::CaseInsensitive) == 0)
            return it.value();
    }

    return std::nullopt;
}

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore

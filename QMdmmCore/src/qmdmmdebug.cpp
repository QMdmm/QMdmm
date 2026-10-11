// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmdebug.h"
#include "qmdmmdebug_p.h"

#include <QDebug>
#include <QIODevice>

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
 * @brief Set the device where QDebug outputs log to.
 * @param f The target output device
 *
 * By default Qt outputs log to a Qt defined buffer. This changes the buffer to our one, for collecting the log we generates
 *
 * A line written to the device carries the time, the level and the category ahead of the message, so that a log read
 * after the fact says when each line was written and how much of it to believe.
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

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore

// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMDEBUG_H
#define QMDMMDEBUG_H

// qmdmmglobal.h includes this header file
#include "qmdmmcoreglobal.h" // NOLINT(misc-header-include-cycle): deliberate mutual include, see qmdmmcoreglobal.h

#include <QDebug>
#include <QFileDevice>

QMDMM_EXPORT_NAME(QMdmmDebug)

namespace QMdmmCore {
#ifndef DOXYGEN
namespace v0 {
#endif

enum class LogLevel : uint8_t
{
    Off,
    Debug,
    Info,
    Warning,
    Critical,
};

QMDMMCORE_EXPORT void qMdmmDebugSetDevice(QIODevice *f);
QMDMMCORE_EXPORT void qMdmmDebugSetConsoleLevel(LogLevel level);

#ifndef DOXYGEN
} // namespace v0
inline namespace v1 {
using v0::LogLevel;
using v0::qMdmmDebugSetConsoleLevel;
using v0::qMdmmDebugSetDevice;
} // namespace v1
#endif

} // namespace QMdmmCore

#endif

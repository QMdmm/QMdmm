// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef DOXYGEN
// qmdmmcoreglobal.h includes this header file. The include belongs outside the guard below:
// entered through qmdmmcoreglobal.h, this header gets here a second time with QMDMMDEBUG_H
// still undefined, and reads its body in full before qmdmmcoreglobal.h goes on -- that is what
// lets either header be the one included first. Doxygen instead stops at the cycle and
// publishes this header with nothing in it, so the include is kept out of its sight. DOXYGEN
// is never defined for the compiler, which therefore still sees the include where it was.
#include "qmdmmcoreglobal.h" // NOLINT(misc-header-include-cycle): deliberate mutual include, see qmdmmcoreglobal.h
#endif

#ifndef QMDMMDEBUG_H
#define QMDMMDEBUG_H

#include <QDebug>
#include <QFileDevice>

#include <optional>

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
QMDMMCORE_EXPORT std::optional<LogLevel> qMdmmDebugLogLevelFromString(const QString &value);

#ifndef DOXYGEN
} // namespace v0
inline namespace v1 {
using v0::LogLevel;
using v0::qMdmmDebugLogLevelFromString;
using v0::qMdmmDebugSetConsoleLevel;
using v0::qMdmmDebugSetDevice;
} // namespace v1
#endif

} // namespace QMdmmCore

#endif

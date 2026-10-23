// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMDEBUG_P
#define QMDMMDEBUG_P

#include "qmdmmdebug.h"

#include <QMutex>
#include <QPointer>

namespace QMdmmCore {

namespace p {

struct QMDMMCORE_PRIVATE_EXPORT DebugLogP
{
    DebugLogP();

    QMutex m;
    QPointer<QIODevice> f;

    // What the console is told, next to the file that is told everything. Off is what a run
    // starts at, so a program that sets a device behaves the way it did before there was a
    // level: the file and nothing on the terminal. The default is set in the constructor in
    // qmdmmdebug_p.cpp rather than here, so that changing it changes one out-of-line
    // definition instead of an initializer every translation unit has already compiled in.
    LogLevel consoleLevel;

    static QtMessageHandler qtMessageHandler;
};

QMDMMCORE_PRIVATE_EXPORT DebugLogP *debugLogInstance();
QMDMMCORE_PRIVATE_EXPORT void messageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg);

} // namespace p

} // namespace QMdmmCore

#endif

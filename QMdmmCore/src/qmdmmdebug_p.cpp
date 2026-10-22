// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmdebug_p.h"
#include "qmdmmdebug.h"

#include <QMutexLocker>

using namespace Qt::StringLiterals;

namespace QMdmmCore {

namespace {
// Where a message sits on the scale a level names.
//
// The two scales are matched here rather than compared as they are, because QtMsgType's own
// order is not the order of severity: QtInfoMsg sits past QtFatalMsg in it, so an information
// line reads as more severe than a fatal one to anything that compares the values.
//
// A fatal message counts as a critical one: the levels stop at critical because that is as far
// as an operator can ask to be shown, and a run that writes a fatal line is ending either way.
LogLevel levelOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return LogLevel::Debug;
    case QtInfoMsg:
        return LogLevel::Info;
    case QtWarningMsg:
        return LogLevel::Warning;
    case QtCriticalMsg:
    case QtFatalMsg:
        return LogLevel::Critical;
    }

    return LogLevel::Critical;
}
} // namespace

namespace p {

QtMessageHandler DebugLogP::qtMessageHandler = nullptr;

DebugLogP *debugLogInstance()
{
    static DebugLogP i;
    return &i;
}

void messageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    DebugLogP *log = debugLogInstance();

    if (log->f != nullptr) {
        const QString line = qFormatLogMessage(type, context, msg);
        QMutexLocker lock(&log->m);
        log->f->write((line + u"\n"_s).toUtf8());
        if (log->f->inherits("QFileDevice")) {
            QFileDevice *f = qobject_cast<QFileDevice *>(log->f.data());
            if (f != nullptr)
                f->flush();
        }
    }

    // The file, when there is one, has just been given every line; the console is the filtered
    // copy of it. With no file the console is the only reader a line has, so nothing is
    // filtered there -- that is also what handing the device back means, and what a run that
    // could not open its log file keeps doing.
    if (log->f != nullptr && (log->consoleLevel == LogLevel::Off || levelOf(type) < log->consoleLevel))
        return;

    if (DebugLogP::qtMessageHandler != nullptr && DebugLogP::qtMessageHandler != &messageOutput) {
        (*DebugLogP::qtMessageHandler)(type, context, msg);
    } else {
        // Is this public API?
        // But anyway it is declared as Q_CORE_EXPORT let's use it directly since we have no other good fallback
        qt_message_output(type, context, msg);
    }
}

} // namespace p

} // namespace QMdmmCore

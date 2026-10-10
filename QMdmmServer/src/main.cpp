// SPDX-License-Identifier: AGPL-3.0-or-later

#include "config.h"

#include <QMdmmGlobal>
#include <QMdmmServer>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>

#include <csignal>
#include <iostream>

using namespace Qt::StringLiterals;

namespace {

// A signal handler may only do what the standard calls async-signal-safe, and that rules out
// everything the event loop owns -- the server and the log device among them. The handler
// therefore raises a flag and nothing else, and the loop picks the flag up where those two
// can be touched safely. Polling the flag, rather than passing a byte through a socket pair,
// keeps this file free of anything that exists on one platform only.
//
// The flag is a member of a struct rather than a variable sitting in the namespace: clang-tidy
// reports a namespace-scope variable that is not const, and this one cannot be const -- the
// handler writes it. What it is -- a plain object with static storage duration -- is the only
// shape a handler may touch at all.
struct StopRequest
{
    static volatile std::sig_atomic_t flag;
};

volatile std::sig_atomic_t StopRequest::flag = 0;

extern "C" void stopSignalHandler(int signalNumber)
{
    Q_UNUSED(signalNumber)
    StopRequest::flag = 1;
}

// Reports the stop and exits with code 0.
//
// A run that is asked to stop is not a failed run, so it ends the way --help does rather than
// the way a rejected configuration does: the code is 0 and the reason is written down. The
// message goes to stderr as well as through qWarning() -- which lands in the log file via the
// installed message handler, one flushed line at a time -- following the shape
// configErrorImpl() uses for the outcomes a user reads from the outside.
//
// There is nothing else to wind down: a run holds no state of its own on disk, so closing the
// listeners is the whole of the work. The connections the listeners accepted are closed by
// the exit itself, which is an ordinary close for the peer rather than a reset.
void shutDown(QMdmmNetworking::Server &server)
{
    const QString message = u"Received a signal to stop; closing the listeners and exiting."_s;

    std::cerr << qPrintable(message) << '\n' << std::flush;
    qWarning().noquote() << message;

    server.close();
    QCoreApplication::exit(0);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setOrganizationName(u"Fsu0413.me"_s);
    QCoreApplication::setApplicationName(u"QMdmmServer"_s);
    QCoreApplication::setApplicationVersion(QMdmmCore::Global::version().toString());

    // Resolved rather than read from the definition: the directory may be relative to the
    // tree this program was installed into, and may fall back to the home directory when
    // that tree is not writable (see QMdmmCore::Global::runtimeDataDirectory()).
    QString logDirectory = QMdmmCore::Global::runtimeDataDirectory() + u"/log"_s;

    if (QDir().mkpath(logDirectory)) {
        QString logFilePath = QDir(logDirectory).absoluteFilePath(u"QMdmmServer-"_s + QString::number(QDateTime::currentMSecsSinceEpoch()));
        QFile *logFile = new QFile(logFilePath);

        if (logFile->open(QIODevice::WriteOnly)) {
            logFile->setParent(&a);
            QMdmmCore::qMdmmDebugSetDevice(logFile);
        } else {
            delete logFile;
            qCritical("Unable to create log file %s .", qPrintable(logFilePath));
        }
    } else {
        qCritical("Unable to create log directory %s .", qPrintable(logDirectory));
    }

    Config config;

    QMdmmNetworking::Server server(config.serverConfiguration(), config.logicConfiguration());

    QObject::connect(&server, &QMdmmNetworking::Server::listenError, &a, [&](const QString &transportName, const QString &errorString) {
        qCritical("Unable to listen on %s: %s", qPrintable(transportName), qPrintable(errorString));
    });

    bool listen = server.listen();
    if (!listen)
        qFatal("Unable to listen, exiting.");

    // The two signals a supervisor or a terminal sends to ask a process to stop. A handler
    // that cannot be installed is worth saying out loud: the run still works, it just ends on
    // the signal the way the kernel does, without the record and the code below.
    for (int signalNumber : {SIGTERM, SIGINT}) {
        if (std::signal(signalNumber, stopSignalHandler) == SIG_ERR)
            qWarning("Unable to install the handler for signal %d; the run will end on it without shutting down.", signalNumber);
    }

    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &a, [&server, &stopPoll]() {
        if (StopRequest::flag == 0)
            return;

        stopPoll.stop();
        StopRequest::flag = 0;
        shutDown(server);
    });
    stopPoll.start(100);

    return QCoreApplication::exec();
}

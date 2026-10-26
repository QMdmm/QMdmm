// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bot.h"
#include "config.h"

#include <QMdmmAgent>
#include <QMdmmClient>
#include <QMdmmGlobal>

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
// everything the event loop owns -- the client and the log device among them. The handler
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
// There is nothing else to wind down, and nothing here has to be closed by hand: unlike the
// server, a bot holds no listener, and the one socket it owns goes away with the client,
// which is destroyed as main() returns from the call this exit makes. The server then reads
// an ordinary close rather than a reset.
void shutDown()
{
    const QString message = u"Received a signal to stop; exiting."_s;

    std::cerr << qPrintable(message) << '\n' << std::flush;
    qWarning().noquote() << message;

    QCoreApplication::exit(0);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setOrganizationName(u"Fsu0413.me"_s);
    QCoreApplication::setApplicationName(u"QMdmmBot"_s);
    QCoreApplication::setApplicationVersion(QMdmmCore::Global::version().toString());

    // Resolved rather than read from the definition: the directory may be relative to the
    // tree this program was installed into, and may fall back to the home directory when
    // that tree is not writable (see QMdmmCore::Global::runtimeDataDirectory()).
    QString logDirectory = QMdmmCore::Global::runtimeDataDirectory() + u"/log"_s;

    if (QDir().mkpath(logDirectory)) {
        QString logFilePath = QDir(logDirectory).absoluteFilePath(u"QMdmmBot-"_s + QString::number(QDateTime::currentMSecsSinceEpoch()));
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

    Config config(QCoreApplication::arguments());

    // The file opened above is the record and keeps every line; the level decides how much of it
    // the terminal is told. It can only be set here, once the command line has been read, so what
    // the configuration code reported while reading it was written before any level applied: those
    // lines followed the starting level, off, and the record is the only place they went. The one
    // thing a configuration in progress does put on the terminal is what configErrorImpl() writes
    // to stderr itself, which never passes through the level at all. A run with no record to keep
    // -- one whose log file could not be opened -- filters nothing, before this line and after it.
    QMdmmCore::qMdmmDebugSetConsoleLevel(config.logLevel());

    QMdmmNetworking::ClientConfiguration conf;
    conf.setScreenName(config.name());

    QMdmmNetworking::Client client(conf);

    Bot *bot = Bot::createBot(config.playingStyle(), &client);

    // The bot needs no further driving and the pointer is discarded on purpose:
    // it is a child of the client, so it is destroyed with the client and needs
    // no owner here, and its whole behaviour is the set of signal connections its
    // constructor makes. Signing in below is all that is left for this process.
    //
    // createBot() answers nullptr for a style it does not know, which would leave
    // this run a strategy-less client that still signs in and plays. That is out
    // of reach today because Config::read_() rejects an unknown style through
    // Bot::styleExist before we get here, but the guarantee is implicit and worth
    // stating.
    Q_UNUSED(bot);

    // The two signals a supervisor or a terminal sends to ask a process to stop. A handler
    // that cannot be installed is worth saying out loud: the run still works, it just ends on
    // the signal the way the kernel does, without the record and the code below.
    //
    // They are installed before the run dials out, which is what lets a connection this
    // process has opened stand for the point a stop is answered at rather than merely fatal.
    for (int signalNumber : {SIGTERM, SIGINT}) {
        if (std::signal(signalNumber, stopSignalHandler) == SIG_ERR)
            qWarning("Unable to install the handler for signal %d; the run will end on it without shutting down.", signalNumber);
    }

    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &a, [&stopPoll]() {
        if (StopRequest::flag == 0)
            return;

        stopPoll.stop();
        StopRequest::flag = 0;
        shutDown();
    });
    stopPoll.start(100);

    // A run is one match. The server ends a session with the game-over broadcast, and the client
    // drops its socket there (ClientP::notifyGameOver); a bot that stayed past it would sit in its
    // event loop holding nothing but the room mirror it built while playing, waiting for a match
    // nobody is going to hand it. Starting the next one is not this run's business -- a second bot
    // program, a GUI's local game or a test leg starts it -- so this run ends where the match ends,
    // the way one asked to stop does: the loop leaves with code 0.
    QObject::connect(client.agent(), &QMdmmNetworking::Agent::gameOverNotified, &a, []() { QCoreApplication::exit(0); });

    client.connectToHost(config.host(), QMdmmCore::Data::StateOnlineBot);

    return QCoreApplication::exec();
}

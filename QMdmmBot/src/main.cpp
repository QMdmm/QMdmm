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
#include <utility>

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

// How long a bot waits before it dials in for the next match.
//
// Not zero: the game-over broadcast is the last thing a session does, and the server tears the
// finished room down while sending it, so a sign-in that arrived in that instant would be asking to
// join a room on its way out. A second is far more than that teardown takes, and short enough that
// a bot is never far from its next match. It steers how a bot behaves between matches rather than
// how one is played, which is why it is a constant and not an option.
constexpr int nextMatchDelay = 1000;

// A bot's match loop: one match per client, and a fresh client for the next one.
//
// A match is a client's lifetime. The server ends a session with the game-over broadcast and the
// client tears its socket down there (ClientP::notifyGameOver); what the client keeps is the room
// mirror it built while the match ran -- the players, their upgrades, the place each of them stood
// in -- because a client was only ever meant to be destroyed when its session ended. So a bot that
// dialled back in on the same client would play the next match against the last one's ghosts. The
// next match gets a client of its own instead, which starts it in the state a fresh run starts in,
// down to the player name.
class MatchLoop : public QObject
{
public:
    MatchLoop(const QMdmmNetworking::ClientConfiguration &configuration, QString host, QString playingStyle)
        : configuration_(configuration)
        , host_(std::move(host))
        , playingStyle_(std::move(playingStyle))
    {
    }

    ~MatchLoop() override
    {
        delete client_;
    }

    Q_DISABLE_COPY_MOVE(MatchLoop)

    // Dials in for a match, and for the next one once this one ends.
    void start()
    {
        client_ = new QMdmmNetworking::Client(configuration_);

        // The bot is a child of the client, which is what destroys it together with the match it
        // played, and its whole behaviour is the set of signal connections its constructor makes.
        // createBot() answers nullptr for a style it does not know, which would leave this run a
        // strategy-less client that still signs in and plays; that is out of reach because
        // Config::read_() rejects an unknown style through Bot::styleExist before main() gets here.
        Bot *bot = Bot::createBot(playingStyle_, client_);
        Q_UNUSED(bot);

        connect(client_->agent(), &QMdmmNetworking::Agent::gameOverNotified, this, &MatchLoop::matchEnded);

        client_->connectToHost(host_, QMdmmCore::Data::StateOnlineBot);
    }

private:
    // Schedules the next match, and does not start it here: the broadcast arrives from inside the
    // client's own handling of it, so the client that reports it is not ours to delete while it is
    // still running that code.
    // NOLINTNEXTLINE(readability-make-member-function-const): a slot is an entry point, not a read
    void matchEnded()
    {
        QTimer::singleShot(nextMatchDelay, this, &MatchLoop::startNextMatch);
    }

    void startNextMatch()
    {
        QMdmmNetworking::Client *finished = client_;
        start();
        finished->deleteLater();
    }

    QMdmmNetworking::ClientConfiguration configuration_;
    QString host_;
    QString playingStyle_;
    // The client playing the match that is running, or the one about to start one.
    QMdmmNetworking::Client *client_ = nullptr;
};

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

    QMdmmNetworking::ClientConfiguration conf;
    conf.setScreenName(config.name());

    // Constructed here and started below, once the stop signals are in place: a run has to be able
    // to be asked to stop from the moment it holds a connection.
    MatchLoop loop(conf, config.host(), config.playingStyle());

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

    loop.start();

    return QCoreApplication::exec();
}

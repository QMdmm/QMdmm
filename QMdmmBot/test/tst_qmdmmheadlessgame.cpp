// SPDX-License-Identifier: AGPL-3.0-or-later

// The headless leg: the built programs, one room, played to game over, read from outside.
//
// Every other suite reads a library inside this process, or one executable as a child: the server
// suite never gets past the command line, and the bot suite drives whole games through the Bot
// class in its own address space. Neither says that QMdmmServer6 and QMdmmBot6 work together, and
// that pair of programs is what a user starts. This file is that reading -- one server and N bots,
// all of them real programs, from an empty room to a finished match, and on to the one a caller
// starts afterwards.
//
// What it reads through is the server's read-only control plane (--control-port, GET /status),
// which is the only surface a process outside the server has: the server writes no lines of its
// own to either stream, and a match is over in a fraction of a second, so there is nothing to read
// afterwards. The snapshot carries the three counts and each room's round counter; the counter
// having climbed is what says a match was played, and the room being gone afterwards what says
// that match ended.
//
// A bot's run is one match (see QMdmmBot/src/main.cpp): the program leaves with code 0 when the
// game-over broadcast arrives, and the next match is somebody else's to start. That is the second
// reading here -- every bot the case started ends on its own and cleanly while the server is still
// up -- and the reason the case goes on to start a second set of bots: a leg that only watched the
// first match go quiet would read the same whether the bots ended or merely stopped answering.

#include "test.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace Qt::StringLiterals;

namespace {

// How many players the room every case here fills holds. Three is the default the server starts
// with, and the number the trio this leg grew out of was measured at.
constexpr int playerCount = 3;

// What one answer from the control plane says, as far as this file reads it.
struct Snapshot
{
    int rooms = 0;
    int games = 0;
    int players = 0;
    // The largest round counter any room reports. Zero on a server whose rooms have yet to start a
    // match, and on one with no rooms at all.
    int rounds = 0;
};

// A port nothing is listening on, taken from the kernel and handed back before the caller passes
// it on. It is a reservation rather than a guarantee -- the kernel may hand the same port to
// someone else in between -- so a run that cannot bind what it was given comes back as that case
// failing with what the run printed, not as a port of this file's choosing.
quint16 freePort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::Any, 0))
        qFatal("this case could not reserve a port of its own");

    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

// One HTTP/1.0 exchange with the control plane at @p port, answering what /status says.
//
// A run that is not listening yet and one that is listening but says nothing are both silent here,
// so the caller waits on the answer rather than on the connect. A connect on its own would not say
// that the run is there: Qt reports a connection to a port with no listener on this machine as one
// that was made, with a peer port of 0 and no peer address at all (measured on macOS 27 with
// Qt 6.12, and the reason the server suite's helper reads the peer too). The peer port is what
// tells the two apart.
//
// The plane keeps a connection for exactly one answer, so the end of the stream is what says the
// answer is complete; the bound inside is there so that a run which holds the connection open
// comes back as a reading the caller can retry rather than as one that never finishes.
bool askStatus(quint16 port, Snapshot *snapshot)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(1000) || socket.peerPort() != port)
        return false;

    socket.write(QByteArray("GET /status HTTP/1.0\r\n\r\n"));
    socket.flush();

    QByteArray answer;
    QElapsedTimer answerTimer;
    answerTimer.start();
    while (socket.state() == QAbstractSocket::ConnectedState && answerTimer.elapsed() < 5000) {
        if (socket.waitForReadyRead(1000))
            answer += socket.readAll();
    }
    answer += socket.readAll();
    socket.disconnectFromHost();

    if (!answer.startsWith(QByteArray("HTTP/1.0 200 ")))
        return false;

    const qsizetype bodyStart = answer.indexOf(QByteArray("\r\n\r\n"));
    if (bodyStart < 0)
        return false;

    const QJsonObject reported = QJsonDocument::fromJson(answer.mid(bodyStart + 4)).object();
    if (!reported.contains(u"rooms"_s) || !reported.contains(u"roomList"_s))
        return false;

    Snapshot read;
    read.rooms = reported.value(u"rooms"_s).toInt();
    read.games = reported.value(u"games"_s).toInt();
    read.players = reported.value(u"players"_s).toInt();

    const QJsonArray roomList = reported.value(u"roomList"_s).toArray();
    for (const QJsonValue &room : roomList)
        read.rounds = std::max(read.rounds, room.toObject().value(u"rounds"_s).toInt());

    *snapshot = read;
    return true;
}

// Asks the plane until a snapshot is the one @p reached describes, or until @p deadlineMs has gone
// by.
//
// What the caller passes is a condition on the snapshot rather than an interval: a run that is up
// but has yet to reach the state being waited for is waited on rather than read as a failure, and
// the pauses between attempts are only what the attempts themselves cost.
std::optional<Snapshot> waitForStatus(quint16 port, int deadlineMs, const std::function<bool(const Snapshot &)> &reached)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < deadlineMs) {
        Snapshot snapshot;
        if (askStatus(port, &snapshot) && reached(snapshot))
            return snapshot;
    }

    return std::nullopt;
}

// Waits for every bot from @p from on to end its run, and names the ones that did not end the way
// a finished match ends one: on its own, and with code 0. Empty when they all did.
//
// @p timeoutMs bounds the group rather than each bot, so a case that has several bots still
// playing cannot add up to more than the suite is allowed. A bot that outlives the match it played
// is one whose run does not end where the program says it does, and waiting longer would only turn
// that into a killed process with no reading attached.
QString botsThatDidNotEnd(const std::vector<std::unique_ptr<QProcess>> &bots, qsizetype from, int timeoutMs)
{
    QStringList complaints;
    QElapsedTimer timer;
    timer.start();

    for (qsizetype i = from; i < static_cast<qsizetype>(bots.size()); ++i) {
        QProcess *bot = bots[i].get();
        const int remaining = std::max(0, timeoutMs - static_cast<int>(timer.elapsed()));

        if (!bot->waitForFinished(remaining)) {
            complaints.append(u"bot %1 was still running when its match ended"_s.arg(i));
            continue;
        }

        if (bot->exitStatus() != QProcess::NormalExit)
            complaints.append(u"bot %1 did not leave on its own"_s.arg(i));
        else if (bot->exitCode() != 0)
            complaints.append(u"bot %1 left with %2"_s.arg(i).arg(bot->exitCode()));
    }

    return complaints.join(u"; "_s);
}

// The bots and the server a case starts, and what takes them down.
//
// They are separate processes and one left behind holds the port the next case wants, so a case
// that fails half way has to take them down as well as one that passes -- which is why they live
// in one object whose destructor is the cleanup rather than in a set of locals a QVERIFY returns
// past.
class Processes
{
public:
    Processes() = default;
    Processes(const Processes &) = delete;
    Processes &operator=(const Processes &) = delete;

    ~Processes()
    {
        stopAll();
    }

    // Starts the server with @p arguments and leaves it running: this returns once the control
    // plane on @p controlPort has answered, which is the point the run has reached its listeners.
    bool startServer(const QStringList &arguments, quint16 controlPort)
    {
        m_server.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), arguments);
        if (!m_server.waitForStarted(60000))
            return false;

        return waitForStatus(controlPort, 60000, [](const Snapshot &) { return true; }).has_value();
    }

    QProcess &server()
    {
        return m_server;
    }

    // Starts one bot against @p port under @p name. The name is what tells the bots apart in the
    // server's snapshot and in a failure report; it is not a playing style, so the run is the
    // default one.
    void addBot(quint16 port, const QString &name)
    {
        std::unique_ptr<QProcess> bot = std::make_unique<QProcess>();
        bot->start(QString::fromLatin1(QMDMMBOT_EXECUTABLE), {u"-l"_s, hostUrl(port), u"-n"_s, name});
        if (!bot->waitForStarted(60000))
            qFatal("a bot did not start");
        m_bots.push_back(std::move(bot));
    }

    const std::vector<std::unique_ptr<QProcess>> &bots() const
    {
        return m_bots;
    }

    static QString hostUrl(quint16 port)
    {
        return u"qmdmm://127.0.0.1:"_s + QString::number(port);
    }

    void stopAll()
    {
        for (const std::unique_ptr<QProcess> &bot : m_bots)
            stopRun(*bot);
        m_bots.clear();
        stopRun(m_server);
    }

private:
    // Asks the run to stop, and does not come back while it is still there.
    //
    // A run that is asked first writes down why it stopped, which one killed outright cannot; what
    // carries that request is a signal, and the handlers are POSIX's alone (see
    // QMdmmBot/src/main.cpp), so elsewhere the run is killed. The split is the server suite's, and
    // for the same reason: Qt's terminate() would not have moved a console run on Windows in any
    // case, it posts WM_CLOSE to windows and a message loop a console run has neither of.
    //
    // The bound is well under the 60 s each case here is bounded at, so a run that outlasts it --
    // one that no longer exits where it is expected to -- is killed here rather than leaving the
    // suite to hang on it.
    static void stopRun(QProcess &process)
    {
        if (process.state() == QProcess::NotRunning)
            return;

#ifdef Q_OS_UNIX
        process.terminate();
#else
        process.kill();
#endif

        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished();
        }
    }

    QProcess m_server;
    std::vector<std::unique_ptr<QProcess>> m_bots;
};

// The arguments every leg here gives its server: TCP only, on @p gamePort, with the control plane
// on @p controlPort. The local socket and the WebSocket are turned off rather than left at their
// defaults because their names are machine-wide -- a second copy of this suite, or a server
// someone is running, would otherwise be a collision the case reads as its own failure -- and
// because TCP is the transport a leg about processes is expected to carry.
QStringList serverArguments(quint16 gamePort, quint16 controlPort)
{
    return {u"-p"_s, QString::number(gamePort), u"-n"_s, QString::number(playerCount), u"-l"_s, u"off"_s, u"-w"_s, u"off"_s, u"--control-port"_s, QString::number(controlPort)};
}

// What a leg that could not get where it was going says: the last answer the plane gave, and the
// streams of every process involved. A reading that never arrived and a reading that says the run
// died look the same from the outside; this is what makes the failure say which one it was.
QString diagnosis(quint16 controlPort, Processes &processes)
{
    QStringList lines;

    Snapshot snapshot;
    if (askStatus(controlPort, &snapshot)) {
        lines.append(u"the last snapshot: rooms=%1 games=%2 players=%3 rounds=%4"_s.arg(snapshot.rooms).arg(snapshot.games).arg(snapshot.players).arg(snapshot.rounds));
    } else {
        lines.append(u"the control plane answered nothing"_s);
    }

    const QString serverError = QString::fromLocal8Bit(processes.server().readAllStandardError());
    lines.append(u"the server said: %1"_s.arg(serverError.isEmpty() ? u"nothing"_s : serverError.trimmed()));

    const std::vector<std::unique_ptr<QProcess>> &bots = processes.bots();
    for (qsizetype i = 0; i < static_cast<qsizetype>(bots.size()); ++i) {
        const QString botError = QString::fromLocal8Bit(bots[i]->readAllStandardError());
        lines.append(u"bot %1 said: %2"_s.arg(i).arg(botError.isEmpty() ? u"nothing"_s : botError.trimmed()));
    }

    return lines.join(u"\n"_s);
}

} // namespace

class tst_QMdmmHeadlessGame : public QObject
{
    Q_OBJECT

public:
    // The shared main builds the object through the meta object, so the constructor has to be one
    // it can call.
    Q_INVOKABLE tst_QMdmmHeadlessGame() = default;

private slots:
    // A room fills, a match is played to its end, and every bot leaves with code 0 while the
    // server stays up. The round counter climbing is what says a match was played rather than a
    // room vacated; the bots ending on their own is what says each run stopped where its match
    // stopped, and the second set of bots afterwards is the half this leg owes the caller -- the
    // next match is not started by the bots that played the last one.
    void headlessGame_aMatchEndsAndTheBotsExit();

    // The same leg one seat short: the third bot is pointed at a port nobody listens on, and the
    // room stays recruiting for as long as it is watched. This is the negative control for the
    // case above -- without it, a room that filled on its own, or a round counter that climbed on
    // its own, would read exactly the same.
    //
    // The reading is the room rather than the bot's exit code. A bot whose host has no listener
    // retries five times with a backoff that ends eight seconds out, and then sits there: measured
    // here, a run pointed at a closed port is still alive after twenty seconds and exits 0 when it
    // is asked to stop. So "the stray bot must fail" is not something this leg can assert; what it
    // can assert is that the seat it would have taken stays empty.
    void headlessGame_aMissingBot_leavesTheRoomRecruiting();

    // What the run resolved is what the leg passed it. The leg hands its arguments to the same
    // program with -d and reads what it prints, so a run whose configuration differs from the one
    // the case believes it started is a red entry here rather than an unattributed difference in
    // the readings above.
    void headlessGame_printsTheConfigurationTheLegPassed();
};

void tst_QMdmmHeadlessGame::headlessGame_aMatchEndsAndTheBotsExit()
{
    const quint16 gamePort = freePort();
    const quint16 controlPort = freePort();

    Processes processes;
    QVERIFY2(processes.startServer(serverArguments(gamePort, controlPort), controlPort),
             qPrintable(u"the server never answered on its control plane\n%1"_s.arg(diagnosis(controlPort, processes))));

    // One seat short of a full room. The room sits recruiting until the last bot arrives, which is
    // what makes the wait below quiet rather than a race with the match: a room holding two of
    // three players stays exactly where it is.
    for (int i = 0; i < playerCount - 1; ++i)
        processes.addBot(gamePort, u"bot"_s + QString::number(i));

    const std::optional<Snapshot> recruiting
        = waitForStatus(controlPort, 60000, [](const Snapshot &snapshot) { return snapshot.rooms == 1 && snapshot.players == playerCount - 1; });
    QVERIFY2(recruiting.has_value(), qPrintable(u"the room never filled to %1 of %2 players\n%3"_s.arg(playerCount - 1).arg(playerCount).arg(diagnosis(controlPort, processes))));

    // Nothing has been played yet, which is what makes the round counter's climb below a reading
    // about the match rather than about a room that was always busy.
    QCOMPARE(recruiting->games, 0);
    QCOMPARE(recruiting->rounds, 0);

    processes.addBot(gamePort, u"last"_s);

    // The match is over in a fraction of a second, so the reading is taken as fast as the plane
    // answers rather than at an interval. What the loop is after is the pair -- a round counter
    // that has climbed, and the room that is gone afterwards -- and only the first half of it is
    // short-lived. The room is known to be there before the last bot is started, so a snapshot
    // with no rooms in it is one taken after the match; there is nothing left to wait for at that
    // point, which is why the loop ends there rather than spinning out the deadline.
    int highestRound = 0;
    bool sawTheEnd = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 60000) {
        Snapshot snapshot;
        if (!askStatus(controlPort, &snapshot))
            continue;

        highestRound = std::max(highestRound, snapshot.rounds);
        if (snapshot.rooms == 0 && snapshot.players == 0) {
            sawTheEnd = true;
            break;
        }
    }

    QVERIFY2(sawTheEnd, qPrintable(u"the room never came to an end\n%1"_s.arg(diagnosis(controlPort, processes))));
    QVERIFY2(highestRound >= 1, qPrintable(u"the room ended without a single round having been played\n%1"_s.arg(diagnosis(controlPort, processes))));

    // Each bot ends its own run there, and ends it cleanly. What the exit code carries is the whole
    // reading: a bot leaves 0 from the one place a finished match leads to, so a bot that crashed,
    // one that was killed and one that is still holding its event loop all read differently from
    // the outside.
    const QString stillPlaying = botsThatDidNotEnd(processes.bots(), 0, 20000);
    QVERIFY2(stillPlaying.isEmpty(), qPrintable(stillPlaying + u"\n"_s + diagnosis(controlPort, processes)));

    // The server is what stays: its room went away with the match, but the program is still there
    // taking rooms, which is what makes the second half below possible at all.
    QVERIFY2(processes.server().state() == QProcess::Running, qPrintable(u"the server exited during the match\n%1"_s.arg(diagnosis(controlPort, processes))));

    // The next match is the caller's to start -- that is the other half of what a run ending
    // means. Three more bot programs, a room of their own, and every one of them ending the same
    // way the first three did.
    for (int i = 0; i < playerCount; ++i)
        processes.addBot(gamePort, u"next"_s + QString::number(i));

    const QString secondStillPlaying = botsThatDidNotEnd(processes.bots(), playerCount, 20000);
    QVERIFY2(secondStillPlaying.isEmpty(), qPrintable(secondStillPlaying + u"\n"_s + diagnosis(controlPort, processes)));
}

void tst_QMdmmHeadlessGame::headlessGame_aMissingBot_leavesTheRoomRecruiting()
{
    const quint16 gamePort = freePort();
    const quint16 controlPort = freePort();
    // Nothing listens here for the length of the case: the port is taken and given back, and the
    // stray bot below is the only thing pointed at it.
    const quint16 deadPort = freePort();

    Processes processes;
    QVERIFY2(processes.startServer(serverArguments(gamePort, controlPort), controlPort),
             qPrintable(u"the server never answered on its control plane\n%1"_s.arg(diagnosis(controlPort, processes))));

    for (int i = 0; i < playerCount - 1; ++i)
        processes.addBot(gamePort, u"bot"_s + QString::number(i));

    const std::optional<Snapshot> recruiting
        = waitForStatus(controlPort, 60000, [](const Snapshot &snapshot) { return snapshot.rooms == 1 && snapshot.players == playerCount - 1; });
    QVERIFY2(recruiting.has_value(), qPrintable(u"the room never filled to %1 of %2 players\n%3"_s.arg(playerCount - 1).arg(playerCount).arg(diagnosis(controlPort, processes))));

    processes.addBot(deadPort, u"stray"_s);

    // Five seconds is many times what the third bot above needed to sign in, so a room still short
    // of a seat at the end of it is one nothing in this case is going to fill. Every answer is read
    // rather than only the last one, and the room has to still be there with the same two players
    // in it: a seat taken late, and a room that filled, played its match and ended, both read as a
    // snapshot without that room, and neither is a room recruiting. A round counter that climbed
    // would say the same thing from the other side -- a match started with a player that is not
    // connected.
    bool answered = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        Snapshot snapshot;
        if (!askStatus(controlPort, &snapshot))
            continue;

        answered = true;
        QVERIFY2(snapshot.rooms == 1 && snapshot.players == playerCount - 1,
                 qPrintable(u"the room did not stay at %1 of %2 players\n%3"_s.arg(playerCount - 1).arg(playerCount).arg(diagnosis(controlPort, processes))));
        QCOMPARE(snapshot.rounds, 0);
    }

    QVERIFY2(answered, qPrintable(u"the control plane answered nothing while the room was watched"_s));
}

void tst_QMdmmHeadlessGame::headlessGame_printsTheConfigurationTheLegPassed()
{
    const quint16 gamePort = freePort();
    const quint16 controlPort = freePort();

    // The same arguments the other two cases start the server with, plus the one that asks for the
    // configuration instead of serving it. -d prints and exits 0, so this run finishes on its own.
    QProcess server;
    server.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), serverArguments(gamePort, controlPort) + QStringList {u"-d"_s});
    if (!server.waitForStarted(60000))
        qFatal("the server did not start");

    if (!server.waitForFinished(60000)) {
        server.kill();
        server.waitForFinished();
        qFatal("the server did not print its configuration and exit");
    }

    QVERIFY(server.exitStatus() == QProcess::NormalExit);
    QCOMPARE(server.exitCode(), 0);

    const QString printed = QString::fromLocal8Bit(server.readAllStandardOutput());
    const QJsonObject configuration = QJsonDocument::fromJson(printed.toUtf8()).object();
    const QJsonObject resolved = configuration.value(u"ServerConfiguration"_s).toObject();
    QVERIFY2(!resolved.isEmpty(), qPrintable(u"the run printed no server configuration: %1"_s.arg(printed)));

    // What the run prints is what the command line named, whether or not it equals the default --
    // -d is a reading of the resolved configuration, not a list of the values that differ from it.
    // Every key below is one the leg passes, which is what makes this a reading about the leg
    // rather than about -d.
    QCOMPARE(resolved.value(u"tcpPort"_s).toInt(), static_cast<int>(gamePort));
    QCOMPARE(resolved.value(u"playerNumPerRoom"_s).toInt(), playerCount);
    QCOMPARE(resolved.value(u"controlPort"_s).toInt(), static_cast<int>(controlPort));
    QCOMPARE(resolved.value(u"localEnabled"_s).toBool(), false);
    QCOMPARE(resolved.value(u"websocketEnabled"_s).toBool(), false);
}

namespace {
RegisterTestObject<tst_QMdmmHeadlessGame> _;
}
#include "tst_qmdmmheadlessgame.moc"

// NOLINTEND

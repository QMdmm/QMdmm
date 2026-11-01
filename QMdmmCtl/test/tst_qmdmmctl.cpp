// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <algorithm>
#include <array>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace Qt::StringLiterals;

namespace {

// Everything a user sees after running the program once. The two streams are kept as bytes: what
// this suite is about is that the snapshot arrives unaltered, and a round trip through a QString
// would be a place where that could stop being true.
struct RunResult
{
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
};

RunResult runCtl(const QStringList &arguments)
{
    QProcess process;
    process.start(QString::fromLatin1(QMDMMCTL_EXECUTABLE), arguments);
    if (!process.waitForStarted(60000))
        qFatal("the executable under test did not start");
    if (!process.waitForFinished(60000))
        qFatal("the executable under test did not finish");

    RunResult result;
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

// Whether a run that came back a success complained about anything of its own.
//
// Emptiness is deliberately not what this asks for. A Debug build of this project is instrumented
// -- `--coverage` arrives through QMdmm6::Core's interface flags -- and an instrumented process may
// write notes of its own to stderr as it exits, such as a profile file from an older build of
// itself that it could not merge. Those notes describe the build rather than this program, and
// holding a case to an empty stream would make it fail in the coverage cell for a reason that has
// nothing to do with what it checks. What the program owes a caller on a run that worked is that
// none of its own failure lines appeared, and they are specific enough to ask for by name.
bool saidSomethingWentWrong(const QByteArray &standardError)
{
    return standardError.contains("nothing answered") || standardError.contains("ended before its header block did") || standardError.contains("rather than the snapshot")
        || standardError.contains("did not finish") || standardError.contains("grew past") || standardError.contains("is not the snapshot");
}

// Every way a run can end without a snapshot, as one list: the words the program puts on stderr,
// and the clause doc/command-line.md tells a reader about it.
//
// That list is copied out three times, and only a reader has ever kept the copies together -- the
// six `*failure =` messages in main.cpp, the six reasons `saidSomethingWentWrong` asks for here,
// and the six clauses of the page's own paragraph. A seventh way could be added to the program with
// the page and this suite both still green, which is what the case at the end of this file is for.
//
// The two columns are written differently on purpose. The first is a fragment of the message, short
// enough to be a substring of exactly one of them; the second is the page's phrasing, kept verbatim
// because the case looks for it there and a paraphrase would read as the page having lost it.
struct EndingWithoutASnapshot
{
    const char *reason;
    const char *clause;
};

const std::array<EndingWithoutASnapshot, 6> &endingsWithoutASnapshot()
{
    // clang-format off
    static const std::array<EndingWithoutASnapshot, 6> rows {{
        {.reason = "nothing answered",                  .clause = "a port with nothing listening"},
        {.reason = "ended before its header block did", .clause = "an answer that ends before its headers do"},
        {.reason = "rather than the snapshot",          .clause = "a `404` from the control plane"},
        {.reason = "did not finish",                    .clause = "an answer that does not finish within five seconds"},
        {.reason = "grew past",                         .clause = "an answer that grows past 1 MiB without ending"},
        {.reason = "is not the snapshot",               .clause = "a `200` whose body is not the snapshot"},
    }};
    // clang-format on
    return rows;
}

quint16 freePort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::Any, 0))
        qFatal("this case could not reserve a port of its own");

    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

// A stand-in for the control plane: it answers one connection with the bytes a case hands it.
//
// The answers worth checking here are the ones a real server never produces -- a body that never
// ends, a body that is not the snapshot, a body that keeps growing -- so the far end has to be
// something other than the server. It does not look at the request: none of these cases turn on
// what the program asks for. It is taken out of the socket before the answer is written all the
// same, for the reason given at answerAndClose().
class StandInPlane
{
public:
    StandInPlane()
    {
        if (!m_server.listen(QHostAddress::LocalHost, 0))
            qFatal("this case could not put a stand-in control plane in place");
    }

    quint16 port() const
    {
        return m_server.serverPort();
    }

    // Waits for the run to knock and hands back the connection it made; the plane owns it.
    QTcpSocket *accept(int timeoutMs)
    {
        if (!m_server.waitForNewConnection(timeoutMs))
            qFatal("the run never knocked on the stand-in control plane");
        return m_server.nextPendingConnection();
    }

private:
    QTcpServer m_server;
};

// An answer built the way the plane builds one, under the status line a case names: the headers it
// closes with, and a body of the length it declares.
QByteArray answerWithStatus(const QByteArray &statusLine, const QByteArray &body)
{
    QByteArray answer = statusLine + "\r\nContent-Type: application/json\r\nContent-Length: ";
    answer += QByteArray::number(static_cast<qlonglong>(body.size()));
    answer += "\r\nConnection: close\r\n\r\n";
    answer += body;
    return answer;
}

QByteArray answerWith(const QByteArray &body)
{
    return answerWithStatus("HTTP/1.0 200 OK", body);
}

// Answers, closes, and waits for the close to have happened.
//
// Both halves are carried out by this side's socket rather than by the program under test, and a
// socket needs its events to drain what was written and to finish closing. A case that skipped the
// wait would have its answer left in this process and would be read as a peer that went quiet
// instead of one that answered and left -- which is a different thing being tested.
//
// The request is taken out of the socket first, and the answer is waited for rather than only
// handed over: a close that leaves received bytes unread is answered with a reset, and Windows
// sends that reset without first putting the answer on the wire -- the run then reads a connection
// that ended with nothing on it and reports an answer that ended before its header block did,
// which is exactly the reading a case about the content of an answer must not be looking at.
void answerAndClose(QTcpSocket *connection, const QByteArray &answer)
{
    if (connection->bytesAvailable() == 0 && !connection->waitForReadyRead(5000))
        qFatal("the run never asked the stand-in control plane anything");
    connection->readAll();

    connection->write(answer);
    connection->flush();
    if (connection->bytesToWrite() > 0 && !connection->waitForBytesWritten(5000))
        qFatal("the stand-in could not put its answer on the wire");
    connection->disconnectFromHost();

    if (connection->state() != QAbstractSocket::UnconnectedState && !connection->waitForDisconnected(5000))
        qFatal("the stand-in could not close its side of the connection");
}

// Starts the program under test against a stand-in plane, so a case only has to say what the far
// end answers.
void startCtlAgainst(QProcess &process, quint16 port)
{
    process.start(QString::fromLatin1(QMDMMCTL_EXECUTABLE), {u"status"_s, u"--control-port"_s, QString::number(port)});
    if (!process.waitForStarted(60000))
        qFatal("the executable under test did not start");
}

// Everything the program owes a caller whose answer was not a snapshot: the exit status carries the
// failure, nothing that could be read as a snapshot lands on stdout, and the reason on stderr is the
// one this case is about -- each of them is a different bound or a different check, and a case that
// only asked for "some reason" would pass on whichever of them happened to fire.
void checkNoSnapshotWasReported(QProcess &process, const QByteArray &reason)
{
    // A run that is already over needs no waiting: the status it left behind is the one to read.
    if (process.state() != QProcess::NotRunning && !process.waitForFinished(60000))
        qFatal("the run did not finish");

    const QByteArray standardOutput = process.readAllStandardOutput();
    const QByteArray standardError = process.readAllStandardError();

    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 1);
    QVERIFY2(standardOutput.isEmpty(), qPrintable(QString::fromLocal8Bit(standardOutput)));
    QVERIFY2(standardError.contains(reason), qPrintable(QString::fromLocal8Bit(standardError)));
}

// One HTTP/1.0 exchange with the control plane, tried again while the run has not reached its
// listener yet. What the wait is made of is the answer, not the connection: Qt reports a connection
// to a loopback port with nothing listening as one that was made (peer port 0, no peer address), so
// an attempt is only counted as reaching the run when the port this case asked for comes back as
// the peer's.
QByteArray askControlPlane(QProcess &process, quint16 port, const QByteArray &request, QString *diagnosis)
{
    QElapsedTimer timer;
    timer.start();

    bool reached = false;
    while (timer.elapsed() < 15000) {
        if (process.state() == QProcess::NotRunning) {
            *diagnosis = u"the run exited before it answered: "_s + QString::fromLocal8Bit(process.readAllStandardError());
            return {};
        }

        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        const bool connected = socket.waitForConnected(1000) && socket.peerPort() == port;
        reached = reached || connected;

        if (connected) {
            socket.write(request);
            socket.flush();

            // The plane serves one answer per connection and then closes, so the end of the stream
            // is what says the answer is complete; the bound inside keeps a run that holds the
            // connection open from turning into a case that never finishes.
            QByteArray answer;
            QElapsedTimer answerTimer;
            answerTimer.start();
            while (socket.state() == QAbstractSocket::ConnectedState && answerTimer.elapsed() < 10000) {
                if (socket.waitForReadyRead(1000))
                    answer += socket.readAll();
            }
            answer += socket.readAll();
            socket.disconnectFromHost();

            if (!answer.isEmpty())
                return answer;
        }

        QTest::qWait(50);
    }

    *diagnosis = reached ? u"the control plane answered nothing on 127.0.0.1:%1"_s.arg(int(port)) : u"the run never listened on 127.0.0.1:%1"_s.arg(int(port));
    return {};
}

void stopRun(QProcess &process)
{
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

} // namespace

class tst_QMdmmCtl : public QObject
{
    Q_OBJECT

private slots:
    void help_printsTheTableAndExitsZero();
    void status_printsWhatTheControlPlaneServesByteForByte();
    void status_exitsNonZeroWhenNothingAnswers();
    void answerThatNeverEnds_exitsOneWithinTheDeadline();
    void answerThatKeepsGrowing_isGivenUpOn();
    void answersThatAreNotTheSnapshot_exitOne_data();
    void answersThatAreNotTheSnapshot_exitOne();
    void answersThatStopShortOfTheSnapshot_exitOne_data();
    void answersThatStopShortOfTheSnapshot_exitOne();
    void rejectedCommandLines_exitThree_data();
    void rejectedCommandLines_exitThree();
    void everyEndingWithoutASnapshot_isDocumentedAndRecognised();
};

void tst_QMdmmCtl::help_printsTheTableAndExitsZero()
{
    const RunResult result = runCtl({u"--help"_s});

    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);
    QVERIFY(result.standardOutput.contains("Usage: QMdmmCtl6"));
    QVERIFY(result.standardOutput.contains("--control-port"));
}

// The whole point of the tool: what it prints is the snapshot, unaltered.
//
// The two readings are taken separately -- one by this case over a socket of its own, one by the
// program under test -- because a case that only checked the shape of the output would pass just as
// well on a copy that never asked the server anything. Byte equality with the plane's own body is
// what rules that out, and the snapshot carries nothing that moves between two readings of an idle
// run: the counts are zero and the room list is empty, with no clock and no uptime in it.
void tst_QMdmmCtl::status_printsWhatTheControlPlaneServesByteForByte()
{
    const quint16 controlPort = freePort();

    // The three game transports are turned off: the smallest run that has a snapshot worth reading,
    // and one that opens nothing but the plane this case talks to.
    QProcess server;
    server.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), {u"-t"_s, u"off"_s, u"-l"_s, u"off"_s, u"-w"_s, u"off"_s, u"--control-port"_s, QString::number(controlPort)});
    if (!server.waitForStarted(60000))
        qFatal("the server under test did not start");

    const auto stopServer [[maybe_unused]] = qScopeGuard([&server] { stopRun(server); });

    QString diagnosis;
    const QByteArray answer = askControlPlane(server, controlPort, QByteArray("GET /status HTTP/1.0\r\n\r\n"), &diagnosis);
    QVERIFY2(!answer.isEmpty(), qPrintable(diagnosis));
    QVERIFY2(answer.startsWith("HTTP/1.0 200 "), qPrintable(QString::fromLatin1(answer)));

    const qsizetype bodyStart = answer.indexOf("\r\n\r\n");
    QVERIFY2(bodyStart >= 0, qPrintable(QString::fromLatin1(answer)));
    const QByteArray servedBody = answer.mid(bodyStart + 4);

    const RunResult result = runCtl({u"status"_s, u"--control-port"_s, QString::number(controlPort)});

    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);
    QVERIFY2(!saidSomethingWentWrong(result.standardError), qPrintable(QString::fromLocal8Bit(result.standardError)));
    QVERIFY2(result.standardOutput == servedBody,
             qPrintable(u"the program printed '%1' where the control plane served '%2'"_s.arg(QString::fromLocal8Bit(result.standardOutput), QString::fromLatin1(servedBody))));

    // A body that is empty on both sides would satisfy the comparison above, so the reading also
    // has to be a snapshot: the keys the plane promises, and nothing else.
    const QJsonObject snapshot = QJsonDocument::fromJson(result.standardOutput).object();
    QCOMPARE(int(snapshot.size()), 4);
    QCOMPARE(snapshot.value(u"rooms"_s).toInt(), 0);
    QCOMPARE(snapshot.value(u"games"_s).toInt(), 0);
    QCOMPARE(snapshot.value(u"players"_s).toInt(), 0);
    QVERIFY(snapshot.value(u"roomList"_s).isArray());
}

// A server that is not there is not an empty answer: the exit status has to carry the failure, and
// nothing may be mistaken for a snapshot on stdout.
//
// The status is pinned to 1 rather than "not 0": 3 is what the program itself gives a command line
// it will not run with, decided before anything is asked, and 1 is what a run ends at when no
// snapshot came back -- the arm this case is about. 1 does not promise that a question was asked:
// an option Qt's own parser does not know leaves with 1 as well, without anything being asked, so
// the two arms of 1 are told apart by the reason on stderr rather than by the status.
void tst_QMdmmCtl::status_exitsNonZeroWhenNothingAnswers()
{
    const quint16 silentPort = freePort();

    const RunResult result = runCtl({u"status"_s, u"--control-port"_s, QString::number(silentPort)});

    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 1);
    QVERIFY2(result.standardOutput.isEmpty(), qPrintable(QString::fromLocal8Bit(result.standardOutput)));
    QVERIFY2(result.standardError.contains("nothing answered"), qPrintable(QString::fromLocal8Bit(result.standardError)));
}

// A peer that keeps a byte coming just inside every read never runs out of reads, so the only thing
// that can end this run is a deadline over the whole exchange rather than one per read. What the
// program owes a caller is the same answer as for a peer that said nothing: no snapshot, exit 1 --
// and a reason that says the answer did not finish, not one that reads as if it had ended early.
void tst_QMdmmCtl::answerThatNeverEnds_exitsOneWithinTheDeadline()
{
    StandInPlane plane;

    QElapsedTimer elapsed;
    elapsed.start();

    QProcess process;
    startCtlAgainst(process, plane.port());
    QTcpSocket *connection = plane.accept(15000);

    // A byte that arrives half a second inside the deadline, and nothing after it: a run that
    // restarted its clock on every read would wait another full five seconds for the next one,
    // while a run with one clock over the exchange gives up half a second after this byte.
    QTest::qWait(4500);
    connection->write("H");
    connection->flush();

    if (!process.waitForFinished(15000))
        qFatal("the run outlived its own deadline");

    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 1);
    QVERIFY2(process.readAllStandardOutput().isEmpty(), "a run that read no snapshot printed something");
    const QByteArray standardError = process.readAllStandardError();
    QVERIFY2(standardError.contains("did not finish"), qPrintable(QString::fromLocal8Bit(standardError)));

    // And what bounded it was the deadline over the exchange: restarting the five seconds on every
    // read would have put the end of this run a second of silence past this point.
    QVERIFY2(elapsed.elapsed() < 8000, qPrintable(u"the run took %1 ms to give up"_s.arg(elapsed.elapsed())));
}

// An answer that keeps growing is a peer that is sending rather than answering. The deadline alone
// does not bound what the program holds -- over a loopback connection it can be handed gigabytes
// inside the same five seconds -- so the size of an answer is bounded on its own.
void tst_QMdmmCtl::answerThatKeepsGrowing_isGivenUpOn()
{
    StandInPlane plane;

    QProcess process;
    startCtlAgainst(process, plane.port());
    QTcpSocket *connection = plane.accept(15000);

    // Well-formed headers, and a body the declared length never contradicts: past the snapshot, the
    // only thing wrong with this answer is how much of it there is.
    connection->write("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: 1000000000\r\nConnection: close\r\n\r\n");

    const QByteArray chunk(64 * 1024, 'x');
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (connection->state() != QAbstractSocket::ConnectedState)
            break;
        connection->write(chunk);
        connection->flush();
        QVERIFY2(timer.elapsed() <= 30000, "the run kept holding an answer that never ended");
        QTest::qWait(1);
    }

    checkNoSnapshotWasReported(process, "grew past");
}

// A 200 is not the snapshot. Each of these is an answer the program used to print and report as a
// healthy server: the body carries no snapshot, so a caller reading stdout would take an empty or
// unrelated answer for the state of the world.
void tst_QMdmmCtl::answersThatAreNotTheSnapshot_exitOne_data()
{
    QTest::addColumn<QByteArray>("answer");

    QTest::newRow("emptyBody") << answerWith(QByteArray());
    QTest::newRow("notJson") << answerWith("not the snapshot\n");
    QTest::newRow("noKeys") << answerWith("{}");
}

void tst_QMdmmCtl::answersThatAreNotTheSnapshot_exitOne()
{
    QFETCH(QByteArray, answer);

    StandInPlane plane;

    QProcess process;
    startCtlAgainst(process, plane.port());
    QTcpSocket *connection = plane.accept(15000);

    // Answered and closed the way the plane itself ends a connection: what these cases are about is
    // the content of the answer, not whether one arrived.
    answerAndClose(connection, answer);

    checkNoSnapshotWasReported(process, "is not the snapshot");
}

// The two ways of stopping short of a snapshot that the case above does not cover, and the two the
// suite had no case for at all: a plane that answers with a status line other than 200, and one
// that closes before its header block is done.
//
// They are read apart from the three above because the reason is a column here -- a caller has to
// be able to tell "this port is something else" from "this answer is not a snapshot", and a case
// that accepted either would pass on whichever check happened to fire first.
void tst_QMdmmCtl::answersThatStopShortOfTheSnapshot_exitOne_data()
{
    QTest::addColumn<QByteArray>("answer");
    QTest::addColumn<QByteArray>("reason");

    // What the control plane answers a path it does not serve: its own text, under a status line
    // that is not the one a snapshot arrives under.
    QTest::newRow("notFound") << answerWithStatus("HTTP/1.0 404 Not Found", "no path here by that name\n") << QByteArray("rather than the snapshot");

    // Headers that were begun and never closed: the exchange ended, so the run has an answer, and
    // what it has is not one it can read a status line out of.
    QTest::newRow("headerThatNeverClosed") << QByteArray("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n") << QByteArray("ended before its header block did");
}

void tst_QMdmmCtl::answersThatStopShortOfTheSnapshot_exitOne()
{
    QFETCH(QByteArray, answer);
    QFETCH(QByteArray, reason);

    StandInPlane plane;

    QProcess process;
    startCtlAgainst(process, plane.port());
    QTcpSocket *connection = plane.accept(15000);

    answerAndClose(connection, answer);

    checkNoSnapshotWasReported(process, reason);
}

// Everything the program refuses before it asks anything ends the same way: the reason on stderr,
// nothing on stdout, exit 3. The set is what a caller that scripts this tool has to be able to tell
// apart from a question that was asked and not answered.
void tst_QMdmmCtl::rejectedCommandLines_exitThree_data()
{
    QTest::addColumn<QStringList>("arguments");

    QTest::newRow("noCommand") << QStringList {u"-p"_s, u"7000"_s};
    QTest::newRow("unknownCommand") << QStringList {u"frobnicate"_s};
    QTest::newRow("twoCommands") << QStringList {u"status"_s, u"status"_s};
    QTest::newRow("statusWithoutAPort") << QStringList {u"status"_s};
    QTest::newRow("portIsNotANumber") << QStringList {u"status"_s, u"--control-port"_s, u"not-a-port"_s};
    QTest::newRow("portIsZero") << QStringList {u"status"_s, u"--control-port"_s, u"0"_s};
    QTest::newRow("portIsAboveTheRange") << QStringList {u"status"_s, u"--control-port"_s, u"65536"_s};
}

void tst_QMdmmCtl::rejectedCommandLines_exitThree()
{
    QFETCH(QStringList, arguments);

    const RunResult result = runCtl(arguments);

    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);
    QVERIFY2(result.standardOutput.isEmpty(), qPrintable(QString::fromLocal8Bit(result.standardOutput)));
    QVERIFY2(!result.standardError.isEmpty(), "a rejected run said nothing about why");
}

// The list of endings, held together from all three sides at once.
//
// Each copy drifts on its own: the program can grow a seventh way of giving up, this suite can stop
// recognising one it has, and the page can lose the clause a reader would look the ending up under.
// None of the three changes anything that runs, so nothing else here -- and nothing in the build --
// would notice. That is what this case is for, and it is the reason the table at the top of this
// file exists rather than the three copies being compared as text against each other.
//
// The program and the page are read as text, the way the server's suite reads its own source and
// its page: neither is linked into this target, the six messages are string literals, and the page
// is not a build input at all.
void tst_QMdmmCtl::everyEndingWithoutASnapshot_isDocumentedAndRecognised()
{
    // The program's side. Every place it gives up is one `*failure = u"..."` line, so the count is
    // looked at before the messages are placed: a seventh ending then fails as a seventh ending
    // rather than as one message this case cannot account for.
    QFile source(QString::fromLatin1(QMDMMCTL_SOURCE));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.fileName() + u": "_s + source.errorString()));

    static const QRegularExpression failureLine(u"\\*failure = u\"([^\"]*)\""_s);

    QStringList messages;
    QRegularExpressionMatchIterator matches = failureLine.globalMatch(QString::fromLocal8Bit(source.readAll()));
    while (matches.hasNext())
        messages.append(matches.next().captured(1));

    QVERIFY2(!messages.isEmpty(), "no failure message was read out of the program's source");
    QCOMPARE(int(messages.size()), int(endingsWithoutASnapshot().size()));

    // One message per ending: a message belonging to two of them would leave another message
    // belonging to none, and the count above could still agree.
    for (const QString &message : messages) {
        QStringList claimed;
        for (const EndingWithoutASnapshot &ending : endingsWithoutASnapshot()) {
            const QString reason = QString::fromLatin1(ending.reason);
            if (message.contains(reason))
                claimed.append(reason);
        }

        QString claimMessage = u"the program's message '"_s + message + u"' is claimed by "_s;
        claimMessage += QString::number(claimed.size()) + u" of this suite's reasons, not one: ["_s + claimed.join(u", "_s) + u"]"_s;
        QVERIFY2(claimed.size() == 1, qPrintable(claimMessage));
    }

    // And no ending may be one the program no longer has: every reason has to turn up somewhere in
    // that source, or the suite is asking for a message that is gone.
    for (const EndingWithoutASnapshot &ending : endingsWithoutASnapshot()) {
        const QString reason = QString::fromLatin1(ending.reason);
        const bool anywhere = std::any_of(messages.cbegin(), messages.cend(), [&reason](const QString &message) { return message.contains(reason); });
        QVERIFY2(anywhere, qPrintable(u"no failure message of the program carries '%1'"_s.arg(reason)));
    }

    // The page's side. Only the tool's own section is read, so that the server's section below it
    // -- which documents endings of its own -- cannot answer for this one. The section is joined
    // into a single run of text first: the page wraps its sentences, so a clause would otherwise be
    // found or missed by where a line happens to break.
    QFile page(QString::fromLatin1(QMDMM_COMMAND_LINE_DOC));
    QVERIFY2(page.open(QIODevice::ReadOnly), qPrintable(page.fileName() + u": "_s + page.errorString()));

    QString section;
    bool inSection = false;
    bool foundSection = false;
    const QStringList lines = QString::fromUtf8(page.readAll()).split(u'\n');
    for (const QString &line : lines) {
        if (line.startsWith(u"## ")) {
            if (foundSection)
                break;
            inSection = line.trimmed() == u"## QMdmmCtl"_s;
            foundSection = inSection;
            continue;
        }
        if (inSection)
            section += line + u' ';
    }

    QVERIFY2(foundSection, "the page has no '## QMdmmCtl' section for this case to read");

    static const QRegularExpression whitespace(u"\\s+"_s);
    section.replace(whitespace, u" "_s);
    QVERIFY2(!section.trimmed().isEmpty(), "the tool's section of the page came out empty");

    for (const EndingWithoutASnapshot &ending : endingsWithoutASnapshot()) {
        const QString clause = QString::fromLatin1(ending.clause);

        QString message = u"the page no longer says how a run can end with '"_s + QString::fromLatin1(ending.reason);
        message += u"' -- the clause '"_s + clause + u"' is not in its QMdmmCtl section"_s;
        QVERIFY2(section.contains(clause), qPrintable(message));
    }

    // And this suite's own copy, which is what tells a run that ended this way apart from one that
    // worked: none of the six may have slipped out of what the helper above looks for.
    for (const EndingWithoutASnapshot &ending : endingsWithoutASnapshot()) {
        const QByteArray reason(ending.reason);

        QVERIFY2(saidSomethingWentWrong(reason), qPrintable(u"this suite does not recognise '%1' as a failure"_s.arg(QString::fromLatin1(reason))));
    }
}

QTEST_GUILESS_MAIN(tst_QMdmmCtl)
#include "tst_qmdmmctl.moc"

// NOLINTEND

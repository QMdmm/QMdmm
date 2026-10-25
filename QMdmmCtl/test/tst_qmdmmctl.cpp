// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

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
// something other than the server. What it does not do is read the request: none of these cases
// turn on what the program asks for.
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

// An answer built the way the plane builds one: the headers it closes with, and a body of the
// length it declares.
QByteArray answerWith(const QByteArray &body)
{
    QByteArray answer = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
    answer += QByteArray::number(static_cast<qlonglong>(body.size()));
    answer += "\r\nConnection: close\r\n\r\n";
    answer += body;
    return answer;
}

// Answers, closes, and waits for the close to have happened.
//
// Both halves are carried out by this side's socket rather than by the program under test, and a
// socket needs its events to drain what was written and to finish closing. A case that skipped the
// wait would have its answer left in this process and would be read as a peer that went quiet
// instead of one that answered and left -- which is a different thing being tested.
void answerAndClose(QTcpSocket *connection, const QByteArray &answer)
{
    connection->write(answer);
    connection->flush();
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
    void rejectedCommandLines_exitThree_data();
    void rejectedCommandLines_exitThree();
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
void tst_QMdmmCtl::status_exitsNonZeroWhenNothingAnswers()
{
    const quint16 silentPort = freePort();

    const RunResult result = runCtl({u"status"_s, u"--control-port"_s, QString::number(silentPort)});

    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QVERIFY2(result.exitCode != 0, "a run that could ask nothing came back as a success");
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

QTEST_GUILESS_MAIN(tst_QMdmmCtl)
#include "tst_qmdmmctl.moc"

// NOLINTEND

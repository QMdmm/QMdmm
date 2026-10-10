// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace Qt::StringLiterals;

namespace {

// Everything a user sees after running the executable once.
struct RunResult
{
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
};

// The per-user configuration file, at the exact path doc/getting-started.md names.
// The per-user configuration lives under $HOME, so the cases below can redirect
// HOME to a directory of their own and watch this one file appear in it.
QString perUserConfigurationFile(const QString &home)
{
    return QDir(home).absoluteFilePath(u".QMdmm/Fsu0413.me/QMdmm.ini"_s);
}

// A copy of the current environment with HOME pointed at @p home.
QProcessEnvironment environmentWithHome(const QString &home)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"HOME"_s, home);
    return environment;
}

// Runs the executable under test with @p arguments and collects what it printed.
// @p environment replaces the process environment when a case has to steer where the
// run reads and writes.
//
// The runs made here all finish on their own, though not all of them cleanly:
// --help and --show-current-configuration print and exit, -c and -C save and exit
// with the save's own status, and everything the configuration code rejects -- a value
// out of range, a value that cannot be parsed, an option pair that cannot both be
// meant -- goes through configError(), which writes the reason to stderr and exits 3.
// A run that does get past the configuration reaches the server itself, and a server
// that cannot bind a transport it was asked for gives up through qFatal(): that run
// ends by aborting rather than by returning an exit code. Either way it ends on its
// own, so a run that outlives the timeout means the executable stopped exiting where
// it is expected to -- the helper kills it and fails rather than reporting a
// half-collected result.
RunResult runServer(const QStringList &arguments, int timeoutMs = 60000, const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment())
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), arguments);

    if (!process.waitForStarted(timeoutMs))
        qFatal("the executable under test did not start");

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        qFatal("the executable under test did not exit; it probably started listening");
    }

    RunResult result;
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.standardError = QString::fromLocal8Bit(process.readAllStandardError());
    return result;
}

} // namespace

class tst_QMdmmServer : public QObject
{
    Q_OBJECT

private slots:
    void help_printsTheUsageAndExitsZero();
    void savingToBothInstances_isRejected();
    void savingPerUserConfiguration_writesTheFileUnderHome();
    void savingPerUserConfiguration_reportsASaveThatCannotReachItsHome();
    void v1Presets_reachThePrintedConfiguration();
    void v1Presets_outrankAStoredLogicSection_data();
    void v1Presets_outrankAStoredLogicSection();
    void outOfRangeValue_isRejected();
    void crossedPairs_areRejected_data();
    void crossedPairs_areRejected();
    void unknownPunishHpRoundStrategy_isRejected();
    void unknownArgument_isRejected();
    void unparsableOptionValue_isRejected_data();
    void unparsableOptionValue_isRejected();
    void twoPlayerShorthands_areRejected_data();
    void twoPlayerShorthands_areRejected();
    void playerShorthandWithPlayersOption_isRejected();
    void roomSizeBelowTwo_isRejected();
    void timeoutBelowFloor_isRejected();
    void punishHpModifierBelowFloor_isRejected();
    void switchValues_reachThePrintedConfiguration_data();
    void switchValues_reachThePrintedConfiguration();
    void portThatIsAlreadyTaken_makesTheRunGiveUp();
    void stopSignal_closesTheListenersAndExitsZero();
};

// --help is the first option a user reaches for, and it is the only one that
// exits 0 on purpose: it prints to stdout and leaves, without going through the
// error path the rejected values take.
void tst_QMdmmServer::help_printsTheUsageAndExitsZero()
{
    const RunResult result = runServer({u"--help"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QVERIFY(result.standardOutput.contains(u"Usage: QMdmmServer [options]"_s));
    // The usage text is the point of the option, so check that it carries the
    // options too rather than stopping after the first line.
    QVERIFY(result.standardOutput.contains(u"--save-configuration"_s));
}

// -c and -C name two different destinations for one run's worth of configuration,
// and they are not interchangeable: the two files sit under different prefixes, only
// one of them normally needs elevated rights to write, and a later run reads the
// per-user one before the global one. A run that asks to save both is therefore a
// usage error rather than a case where one of them quietly wins -- picking either
// would write where the user did not ask.
void tst_QMdmmServer::savingToBothInstances_isRejected()
{
    const RunResult result = runServer({u"-c"_s, u"-C"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"save both per-user configuration and global configuration"_s));
}

// -c saves the resolved configuration to the per-user file and exits 0. The file
// used to be a native-format store that ignored HOME; as an INI file it follows
// HOME, which is what lets this case point HOME at a directory of its own and
// assert on that one file -- the real per-user configuration of whoever runs the
// tests is never touched.
void tst_QMdmmServer::savingPerUserConfiguration_writesTheFileUnderHome()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());

    const RunResult result = runServer({u"-c"_s}, 60000, environmentWithHome(home.path()));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    const QString configurationFile = perUserConfigurationFile(home.path());
    QVERIFY2(QFile::exists(configurationFile), qPrintable(configurationFile));

    QFile file(configurationFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString contents = QString::fromUtf8(file.readAll());

    // -c writes the resolved configuration, defaults included, so values that were
    // never named on the command line are in the file as well.
    QVERIFY(contents.contains(u"[server]"_s));
    QVERIFY(contents.contains(u"tcp-port=6366"_s));
    QVERIFY(contents.contains(u"[logic]"_s));
    QVERIFY(contents.contains(u"slash=1"_s));
}

// The other half of what -c promises: the run exits with the save's own
// QSettings::Status, so a save that cannot reach its destination is visible from
// the outside instead of passing as a success. A regular file where the per-user
// directory would go makes the write fail on any account, and the run then exits
// with AccessError rather than 0.
void tst_QMdmmServer::savingPerUserConfiguration_reportsASaveThatCannotReachItsHome()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());

    QFile blocker(QDir(home.path()).absoluteFilePath(u".QMdmm"_s));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    const RunResult result = runServer({u"-c"_s}, 60000, environmentWithHome(home.path()));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, static_cast<int>(QSettings::AccessError));

    QVERIFY(!QFile::exists(perUserConfigurationFile(home.path())));
}

// -1 selects the v1 presets. Their maximum-maxhp is 7, exactly the floor the
// range checks enforce, so the preset is one edit away from being rejected the
// moment it is selected -- which is how it once broke. --show-current-configuration
// prints the resolved values without saving anything, so it shows the preset
// surviving the range checks and reaching the configuration.
//
// HOME points at a directory of its own so that what is printed comes from the
// preset alone, whatever per-user configuration the machine running the tests
// happens to hold.
void tst_QMdmmServer::v1Presets_reachThePrintedConfiguration()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());

    const RunResult result = runServer({u"-1"_s, u"-d"_s}, 60000, environmentWithHome(home.path()));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QVERIFY(result.standardOutput.contains(u"\"maximumMaxHp\": 7"_s));
    QVERIFY(result.standardOutput.contains(u"\"initialKnifeDamage\": 1"_s));
    QVERIFY(result.standardOutput.contains(u"\"zeroHpAsDead\": false"_s));
}

// -1 outranks a stored configuration, not only the built-in defaults: while the
// preset is selected a logic value in the per-user file is not read at all. The
// stored server values keep being read, so what the option hides is the stored
// logic section, not the file.
//
// Both rows read a file with the same content and differ only in whether the
// preset is selected, which is what pins the order the logic values resolve in:
// command line, then the preset, then the file, then the defaults. maximum-maxhp
// is stored as 10, neither the default 20 nor the preset's 7, so the printed
// value says which source won; tcp-port is stored as 7777 to show the file
// itself is still read in both rows.
void tst_QMdmmServer::v1Presets_outrankAStoredLogicSection_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("printed");

    QTest::newRow("with -1 the stored logic is not read") << QStringList {u"-1"_s, u"-d"_s} << u"\"maximumMaxHp\": 7"_s;
    QTest::newRow("without -1 the stored logic wins over the default") << QStringList {u"-d"_s} << u"\"maximumMaxHp\": 10"_s;
}

void tst_QMdmmServer::v1Presets_outrankAStoredLogicSection()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, printed);

    QTemporaryDir home;
    QVERIFY(home.isValid());
    QVERIFY(QDir(home.path()).mkpath(u".QMdmm/Fsu0413.me"_s));

    QFile file(perUserConfigurationFile(home.path()));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("[server]\ntcp-port=7777\n\n[logic]\nmaximum-maxhp=10\n") > 0);
    file.close();

    const RunResult result = runServer(arguments, 60000, environmentWithHome(home.path()));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QVERIFY(result.standardOutput.contains(u"\"tcpPort\": 7777"_s));
    QVERIFY(result.standardOutput.contains(printed));
}

// Each value the usage text promises a floor for is judged on its own, after the
// command line and the config file have been merged. 6 is under the floor the
// usage text promises for maximum-maxhp, and the value reaches the check through
// the short form -- both halves of that combination are what this pins down.
//
// HOME is a directory of its own for the same reason as the case above: this is
// a case about a value the command line carries, so nothing else may contribute.
void tst_QMdmmServer::outOfRangeValue_isRejected()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());

    const RunResult result = runServer({u"-1"_s, u"-M"_s, u"6"_s}, 60000, environmentWithHome(home.path()));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"maximum-maxhp must be at least 7"_s));
}

// A pair whose members are each in range but contradict one another is a
// different check from the per-value floors: 15 is a legal maxhp and 7 is a legal
// maximum-maxhp, yet an attribute cannot grow from 15 to 7. Left unrejected this
// pair leaves the attribute with no room to grow at all, which Room::isGameOver()
// reads as "this player is the winner".
//
// One row per pair the check walks. It is a loop over an array, so a row dropped
// from that array puts the self-inflicting configuration back; with a case on every
// pair, that turns this suite red instead of leaving it green.
void tst_QMdmmServer::crossedPairs_areRejected_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("message");

    QTest::newRow("slash") << QStringList {u"-s"_s, u"15"_s, u"-S"_s, u"7"_s} << u"slash must not exceed maximum-slash"_s;
    QTest::newRow("kick") << QStringList {u"-k"_s, u"12"_s, u"-K"_s, u"5"_s} << u"kick must not exceed maximum-kick"_s;
    QTest::newRow("maxhp") << QStringList {u"-m"_s, u"15"_s, u"-M"_s, u"7"_s} << u"maxhp must not exceed maximum-maxhp"_s;
}

void tst_QMdmmServer::crossedPairs_areRejected()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, message);

    const RunResult result = runServer(arguments);

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(message));
}

// An unknown --punish-hp-round-strategy is a typo, not a value to fall back on,
// so it is rejected like every other unparsable value rather than silently
// becoming the default strategy.
void tst_QMdmmServer::unknownPunishHpRoundStrategy_isRejected()
{
    const RunResult result = runServer({u"--punish-hp-round-strategy"_s, u"NoSuchStrategy"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"punish-hp-round-strategy"_s));
    QVERIFY(result.standardError.contains(u"can't be parsed"_s));
}

// A word that names no option is left over as a positional argument. The run then has
// no way to tell whether the user meant something by it, so it refuses to start with
// the defaults and say nothing -- the message quotes what it could not place.
void tst_QMdmmServer::unknownArgument_isRejected()
{
    const RunResult result = runServer({u"extra"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"Unknown argument: extra"_s));
}

// A value that cannot be read as the type its own option promises is a typo, not a
// value to fall back on: keeping the default silently would leave the user with a
// server configured differently from what they typed. The message names the item and
// says where the value came from, so the same rejection is readable from the command
// line and from a configuration file.
//
// Each row is a different way a value fails to parse: a word where a port number goes,
// a number that does not fit the 16 bits a port is carried in, and a word outside the
// family of spellings an on/off switch accepts.
void tst_QMdmmServer::unparsableOptionValue_isRejected_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("message");

    QTest::newRow("tcp-port is not a number") << QStringList {u"-p"_s, u"notaport"_s} << u"tcp-port (from command line) can't be parsed"_s;
    QTest::newRow("tcp-port does not fit a uint16") << QStringList {u"-p"_s, u"65536"_s} << u"tcp-port (from command line) can't be parsed"_s;
    QTest::newRow("tcp is not an on/off word") << QStringList {u"-t"_s, u"maybe"_s} << u"tcp (from command line) can't be parsed"_s;
}

void tst_QMdmmServer::unparsableOptionValue_isRejected()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, message);

    const RunResult result = runServer(arguments);

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(message));
}

// Two player-count shorthands name two different sizes for one run, and there is no
// order that makes either of them right -- taking one would fill a room the user did
// not ask for. The message names both so the clash is visible.
//
// The rows are written in both command-line orders: the shorthands are read off the
// parser rather than off the argument list, so -5 -2 is the same run as -2 -5.
void tst_QMdmmServer::twoPlayerShorthands_areRejected_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("message");

    QTest::newRow("2 then 3") << QStringList {u"-2"_s, u"-3"_s} << u"-3 can't be specified along with -2"_s;
    QTest::newRow("5 then 2") << QStringList {u"-5"_s, u"-2"_s} << u"-5 can't be specified along with -2"_s;
}

void tst_QMdmmServer::twoPlayerShorthands_areRejected()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, message);

    const RunResult result = runServer(arguments);

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(message));
}

// A shorthand and --players are one item written two ways, so a run that gives both
// has to be told that only one of them can count.
void tst_QMdmmServer::playerShorthandWithPlayersOption_isRejected()
{
    const RunResult result = runServer({u"-3"_s, u"-n"_s, u"4"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"-3 can't be specified along with -n / --players"_s));
}

// A room of one has no opponent, so action-order resolution -- a rock-paper-scissors
// round between the players -- cannot be run at all. The floor is 2 and the room size
// is settled before anything else is resolved, so a run that asks for less is turned
// away here rather than starting a room that can never finish.
void tst_QMdmmServer::roomSizeBelowTwo_isRejected()
{
    const RunResult result = runServer({u"-n"_s, u"1"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"players must be at least 2 (got 1)"_s));
}

// The operation timeout is either off (0) or long enough to be usable; a value under
// the floor would cut players off in the middle of a round, so it is rejected rather
// than raised to the floor behind the user's back.
void tst_QMdmmServer::timeoutBelowFloor_isRejected()
{
    const RunResult result = runServer({u"-o"_s, u"5"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"timeout must be 0 or at least 15 (got 5)"_s));
}

// The punish-HP modifier is either off (0) or at least 2; 1 is a value that rounds
// every punishment away to nothing, so it is a typo rather than a setting.
void tst_QMdmmServer::punishHpModifierBelowFloor_isRejected()
{
    const RunResult result = runServer({u"-r"_s, u"1"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);

    QVERIFY(result.standardError.contains(u"punish-hp-modifier must be 0 or at least 2 (got 1)"_s));
}

// An on/off switch takes a family of spellings, and --show-current-configuration prints
// the resolved configuration without saving anything, which is what lets the effect be
// read off from the outside. Both directions are here because a switch whose reading is
// inverted would still print a well-formed configuration, just the wrong one.
void tst_QMdmmServer::switchValues_reachThePrintedConfiguration_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("printed");

    QTest::newRow("off") << u"off"_s << u"\"tcpEnabled\": false"_s;
    QTest::newRow("on") << u"on"_s << u"\"tcpEnabled\": true"_s;
}

void tst_QMdmmServer::switchValues_reachThePrintedConfiguration()
{
    QFETCH(QString, value);
    QFETCH(QString, printed);

    const RunResult result = runServer({u"-t"_s, value, u"-d"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QVERIFY(result.standardOutput.contains(printed));
}

// A run that gets past the configuration reaches the server itself, and a server that
// cannot bind a transport it was asked for gives up through qFatal() rather than
// staying up unreachable. The port cannot be picked by the child alone -- it may
// already be held by another process, which is exactly the case worth pinning -- so
// this case holds a port of its own and hands that port to the child.
//
// The run ends by aborting, not by returning an exit code, which is what tells it
// apart from a run that hangs: the timeout guard in runServer() fails this case if the
// child never exits. The messages on this path are not asserted here because they do
// not reach stderr: QMdmmServer installs a message handler that writes to its log file
// before it builds the server, so the transport error and the fatal notice land in
// that file. What a user observes from this side is the run giving up.
void tst_QMdmmServer::portThatIsAlreadyTaken_makesTheRunGiveUp()
{
    QTcpServer occupant;
    QVERIFY(occupant.listen(QHostAddress::Any, 0));
    const quint16 port = occupant.serverPort();

    const RunResult result = runServer({u"-t"_s, u"on"_s, u"-p"_s, QString::number(port), u"-l"_s, u"off"_s, u"-w"_s, u"off"_s});

    QVERIFY(result.exitStatus == QProcess::CrashExit);
}

// A run that is asked to stop -- the signal a supervisor or a terminal sends -- closes its
// listeners, writes down why it stopped, and leaves with a code of its own. Left unwired, the
// signal ends the process the way the kernel does: no record, and nothing beyond the signal
// itself as a code (143).
//
// The peer on the socket is the second half of that: the run closes its own ends on the way
// out, so what the peer reads is an ordinary close rather than a reset. The connection is
// also what says the run got as far as listening, which is past the point its handlers are
// installed at.
//
// SIGTERM is a POSIX notion, so this case has nothing to send on Windows and skips there.
void tst_QMdmmServer::stopSignal_closesTheListenersAndExitsZero()
{
#ifndef Q_OS_UNIX
    QSKIP("SIGTERM is POSIX; there is nothing to send on this platform.");
#else
    // The run needs a free port of its own, so this case takes one and hands it back before
    // the run starts -- the reservation the case above makes, read the other way round.
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::Any, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();

    QProcess process;
    process.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), {u"-t"_s, u"on"_s, u"-p"_s, QString::number(port), u"-l"_s, u"off"_s, u"-w"_s, u"off"_s});
    if (!process.waitForStarted(60000))
        qFatal("the executable under test did not start");

    // An attempt that is refused means the run is not there yet, which is a wait rather than
    // a failure: it has to reach the loop that accepts this connection. A refused attempt
    // comes back at once, so the pause is what the wait is made of.
    QTcpSocket peer;
    bool connected = false;
    for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
        peer.connectToHost(QHostAddress::LocalHost, port);
        connected = peer.waitForConnected(200);
        if (!connected) {
            peer.abort();
            QTest::qWait(50);
        }
    }
    if (!connected) {
        process.kill();
        process.waitForFinished();
        qFatal("the executable under test never started listening");
    }

    process.terminate();

    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished();
        qFatal("the executable under test did not exit on the signal; a run that handles it exits with a code of its own");
    }

    QVERIFY(process.exitStatus() == QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);

    const QString standardError = QString::fromLocal8Bit(process.readAllStandardError());
    QVERIFY2(standardError.contains(u"Received a signal to stop"_s), qPrintable(standardError));

    // The peer is disconnected by the run's own exit, without having to time out first.
    QVERIFY(peer.state() == QAbstractSocket::UnconnectedState || peer.waitForDisconnected(60000));
#endif
}

QTEST_GUILESS_MAIN(tst_QMdmmServer)

#include "tst_qmdmmserver.moc"

// NOLINTEND

// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QProcess>
#include <QString>
#include <QStringList>
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

// Runs the executable under test with @p arguments and collects what it printed.
//
// The runs made here all finish on their own: --help and --show-current-configuration
// print and exit, and a value the configuration code rejects goes through
// configError(), which writes the reason to stderr and exits 3. None of them
// starts a server, so a run that outlives the timeout means the executable
// stopped exiting where it is expected to -- abort rather than report a
// half-collected result.
RunResult runServer(const QStringList &arguments, int timeoutMs = 60000)
{
    QProcess process;
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
    void v1Presets_reachThePrintedConfiguration();
    void outOfRangeValue_isRejected();
    void crossedPairs_areRejected_data();
    void crossedPairs_areRejected();
    void unknownPunishHpRoundStrategy_isRejected();
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

// -1 selects the v1 presets. Their maximum-maxhp is 7, exactly the floor the
// range checks enforce, so the preset is one edit away from being rejected the
// moment it is selected -- which is how it once broke. --show-current-configuration
// prints the resolved values without saving anything, so it shows the preset
// surviving the range checks and reaching the configuration.
void tst_QMdmmServer::v1Presets_reachThePrintedConfiguration()
{
    const RunResult result = runServer({u"-1"_s, u"-d"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QVERIFY(result.standardOutput.contains(u"\"maximumMaxHp\": 7"_s));
    QVERIFY(result.standardOutput.contains(u"\"initialKnifeDamage\": 1"_s));
    QVERIFY(result.standardOutput.contains(u"\"zeroHpAsDead\": false"_s));
}

// Each value the usage text promises a floor for is judged on its own, after the
// command line and the config file have been merged. 6 is under the floor the
// usage text promises for maximum-maxhp, and the value reaches the check through
// the short form -- both halves of that combination are what this pins down.
void tst_QMdmmServer::outOfRangeValue_isRejected()
{
    const RunResult result = runServer({u"-1"_s, u"-M"_s, u"6"_s});

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

QTEST_GUILESS_MAIN(tst_QMdmmServer)

#include "tst_qmdmmserver.moc"

// NOLINTEND

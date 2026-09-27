// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QString>
#include <QStringList>
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
// The runs made here all finish on their own: --help and --show-current-configuration
// print and exit, -c and -C save and exit with the save's own status, and everything
// the configuration code rejects -- a value out of range, a value that cannot be
// parsed, an option pair that cannot both be meant -- goes through configError(),
// which writes the reason to stderr and exits 3. None of them starts a server, so a
// run that outlives the timeout means the executable stopped exiting where it is
// expected to -- abort rather than report a half-collected result.
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

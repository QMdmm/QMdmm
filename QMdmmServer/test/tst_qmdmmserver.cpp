// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <array>

#ifdef Q_OS_UNIX
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

// The per-user configuration file, at the exact path doc/getting-started.md names. It goes under
// the home a run resolves, so a case hands this the same home it handed the run.
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

// The home a case that watches the per-user file is to run against, or an empty string when there
// is none to be had here.
//
// A case that watches that file has to write where the run it starts will read. Off CI it does that
// under a home of its own: HOME is pointed at @p scratch, so what the case writes is its own and
// the configuration the machine holds is never touched. On CI there is nothing to protect -- the
// machine is thrown away after the run -- and the cases run against the home the machine already
// has instead, which is the same home the runs started there resolve.
//
// That last part is what the platform whose home does not follow the environment leaves out:
// Windows reads the home off the process token, so the file a run reaches there is the machine's
// own whichever home it was handed. A case with nothing to write under but the user's own file
// comes back empty, and the case that asked skips rather than reach into a configuration someone
// is using.
//
// A case that writes there puts its writing back on the way out (PerUserFileRestore). On CI that is
// not politeness: the file belongs to the machine and the tests that run after this one read it too.
QString perUserCaseHome(QTemporaryDir &scratch)
{
    if (qEnvironmentVariableIsSet("CI"))
        return QDir::homePath();

#ifdef Q_OS_WIN
    Q_UNUSED(scratch)
    return {};
#else
    if (!scratch.isValid())
        return {};
    return scratch.path();
#endif
}

// The environment a per-user case hands the run it starts. On CI the run is meant to reach the
// machine's own home, which is where it already looks, so nothing is pointed anywhere. Off CI the
// home is the case's own and has to be handed over.
QProcessEnvironment perUserCaseEnvironment(const QString &home)
{
    if (qEnvironmentVariableIsSet("CI"))
        return QProcessEnvironment::systemEnvironment();
    return environmentWithHome(home);
}

// The per-user file under the home a case was handed, put back the way the case found it when this
// goes out of scope.
//
// On CI that file is not the case's own: it is the machine's, and more than the run the case starts
// reads it -- tst_qmdmmgui6 starts a real server later in the same ctest run, and that server
// resolves its logic configuration from this very file. So a case that writes there has to take its
// writing back out, or it decides what the GUI reads. It did: the [logic] section the case below
// plants was still in the file when the GUI ran, and its readings came out as the planted values --
// a maximum-maxhp of 10 where that case expected the default 20, and an upgrade list built from it
// with one offer missing. Every Build & test entry in that run went down with it, none of them near
// this file.
//
// Off CI the home is one of the case's own and nothing else can reach the file, but the same guard
// is used there rather than a second shape.
class PerUserFileRestore
{
public:
    explicit PerUserFileRestore(const QString &home)
        : m_path(perUserConfigurationFile(home))
    {
        QFile file(m_path);
        m_existed = file.exists();
        if (m_existed && file.open(QIODevice::ReadOnly))
            m_contents = file.readAll();
    }

    ~PerUserFileRestore()
    {
        if (!m_existed) {
            QFile::remove(m_path);
            return;
        }

        QFile file(m_path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(m_contents);
    }

private:
    const QString m_path;
    bool m_existed = false;
    QByteArray m_contents;
};

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
// that cannot bind a transport it was asked for is refused in that same shape: the
// reason on stderr and status 3. Either way it ends on its own, so a run that outlives
// the timeout means the executable stopped exiting where it is expected to -- the
// helper kills it and fails rather than reporting a half-collected result.
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

// Every letter a short option can be spelled with. The free-slot table in config.cpp is a subset
// of it, so this is what the table has to account for.
QString shortOptionAlphabet()
{
    return u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"_s;
}

// @p letters as one stable string, for a failure message that names what drifted.
QString sortedLetters(const QSet<QChar> &letters)
{
    QStringList list;
    list.reserve(letters.size());
    for (const QChar letter : letters)
        list.append(QString(letter));
    list.sort();
    return list.join(u", "_s);
}

// @p words as one stable string, for a failure message that names what drifted.
QString sortedWords(QSet<QString> words)
{
    QStringList list(words.constBegin(), words.constEnd());
    list.sort();
    return list.join(u", "_s);
}

// The keys one section of a stored configuration file holds: the words before the '=' on the lines
// that follow that section's header, up to the next header.
//
// The line endings are the platform's rather than this reader's subject, and the two platforms do
// not agree on them -- a run on Windows writes a carriage return in front of every newline, so a
// line read by splitting on '\n' carries one at its end and is equal to neither a header nor a key
// until it is taken off. The split therefore takes the ending apart at the same time, and the case
// below reads the same file with both endings so that the difference is a reading on either
// platform instead of a red entry on one of them.
QSet<QString> storedSectionKeys(const QString &contents, const QString &header)
{
    static const QRegularExpression lineEnding(u"\\r?\\n"_s);

    QSet<QString> keys;
    bool inSection = false;
    const QStringList lines = contents.split(lineEnding);
    for (const QString &line : lines) {
        if (line.startsWith(u'[')) {
            inSection = line == header;
            continue;
        }
        if (!inSection)
            continue;
        const int equals = line.indexOf(u'=');
        if (equals > 0)
            keys.insert(line.left(equals));
    }
    return keys;
}

// The stored face of ServerConfiguration: one row per key, naming the property the run prints, the
// key the configuration file holds and a value that differs from the default so that a reading
// says which source won. A value is spelled twice because the two faces spell it differently -- the
// file holds a bare word, the printed JSON holds a quoted string where the property is one.
//
// The rows are the subject of two cases that pull in opposite directions: each row is walked by
// `storedKeys_reachThePrintedConfiguration` (a row whose key the program does not read leaves the
// printed value at its default, and that case goes red), and the set of rows is held against both
// the properties the header declares and the keys a save writes by
// `declaredConfigurationKeys_areTheStoredOnes` and `savedConfiguration_carriesEveryStoredKey` (a
// property with no row, or a saved key with no row, makes a set differ and a case go red). A key
// an operator has no business tuning would go in `unexposedServerKeys` with its reason instead of
// here; there is none today, and the doxygen on ServerConfiguration says what makes a key eligible
// for either list.
struct StoredServerKey
{
    const char *property;
    const char *key;
    const char *value;
    const char *json;
};

const std::array<StoredServerKey, 12> &storedServerKeys()
{
    // clang-format off
    static const std::array<StoredServerKey, 12> rows {{
        {.property = "tcpEnabled",         .key = "tcp",                 .value = "false",          .json = "false"},
        {.property = "tcpPort",            .key = "tcp-port",            .value = "7001",           .json = "7001"},
        {.property = "localEnabled",       .key = "local",               .value = "false",          .json = "false"},
        {.property = "localSocketName",    .key = "local-name",          .value = "gate-local",     .json = "\"gate-local\""},
        {.property = "websocketEnabled",   .key = "websocket",           .value = "false",          .json = "false"},
        {.property = "websocketName",      .key = "websocket-name",      .value = "gate-websocket", .json = "\"gate-websocket\""},
        {.property = "websocketPort",      .key = "websocket-port",      .value = "7002",           .json = "7002"},
        {.property = "controlPort",        .key = "control-port",        .value = "7003",           .json = "7003"},
        {.property = "playerNumPerRoom",   .key = "players",             .value = "4",              .json = "4"},
        {.property = "requestTimeout",     .key = "timeout",             .value = "30",             .json = "30"},
        {.property = "maximumPacketSize",  .key = "maximum-packet-size", .value = "2048",           .json = "2048"},
        {.property = "signInTimeout",      .key = "sign-in-timeout",     .value = "30",             .json = "30"},
    }};
    // clang-format on
    return rows;
}

// A port on the loopback address that this machine is not using, taken by listening on it and
// handed straight back. A case that needs the run to open a port of its own cannot pick one out of
// the air -- another process may hold it -- so it asks the kernel and then lets it go.
quint16 freePort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::Any, 0))
        qFatal("this case could not reserve a port of its own");

    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

// One HTTP/1.0 exchange with the control plane, tried again while the run has not reached its
// listener yet. A run that is not there yet and a run that is there but says nothing are both
// silent from here, so what the wait is made of is the answer: it is the reading that says the run
// was listening, rather than the connect that carried the request.
//
// A connect on its own would not say that. Qt reports a successful connection to a port with no
// listener on this machine as one that was made -- a ConnectedState with a peer port of 0 and no
// peer address, measured on macOS 27 with Qt 6.12 -- so a wait built on it ends the moment it
// starts, on a run that has still to call listen(). The peer is read instead: an attempt that ends
// with the port as its peer is one the run took, and an attempt to nothing is one it did not.
//
// The bound is 10 s, the same wait the stop-signal case gives the run to reach its listener, and
// the pauses between attempts are what it is made of -- an attempt to nothing comes back at once.
// @p process is watched as well: a run that is refused outright exits instead of listening, and
// the wait then ends with the reason it printed. Which of the two shapes a failure had -- nothing
// listening, or a listener that answered nothing -- and the run's own last words, are left in
// @p diagnosis. Why no answer came back is not: the socket's reason is the same refusal or close
// for both shapes, and the shapes are the ones a reader can act on.
QByteArray askControlPlane(QProcess &process, quint16 port, const QByteArray &request, QString *diagnosis)
{
    QElapsedTimer timer;
    timer.start();

    bool reached = false;
    while (timer.elapsed() < 10000) {
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

            // The plane keeps the connection for exactly one answer, so the end of the stream is
            // what says the answer is complete; the bound inside is there so that a run which holds
            // the connection open comes back as this case failing rather than as one that never
            // finishes.
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

    *diagnosis = reached ? u"the control plane answered nothing on 127.0.0.1:%1"_s.arg(port) : u"the run never listened on 127.0.0.1:%1"_s.arg(port);
    return {};
}

// Asks a run of the executable under test to stop, and does not come back while it is still
// there.
//
// The run is asked first, rather than killed outright, so that one whose log is buffered writes
// it out before it goes. What asks it is a stop signal, and that request is POSIX's alone -- the
// handler is installed there only (see the stop-signal case) -- so elsewhere the run is killed,
// the same split the networking suite's corpus case takes. Qt's terminate() would not have moved
// a console run there in any case: it posts WM_CLOSE to the run's top-level windows and to its
// main thread, and a console run has neither a window to close nor a message loop to read the
// request from, so the run would have gone on until the wait behind it ran out.
//
// Either way the wait is bounded well under the 60 s these cases are bounded at, so that a run
// which outlasts it -- one that no longer exits where the case expects it to -- is killed here
// rather than leaving the suite to hang on it.
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

class tst_QMdmmServer : public QObject
{
    Q_OBJECT

private slots:
    void help_printsTheUsageAndExitsZero();
    void freeShortOptions_areTheOnesNoOptionClaims();
    void savingToBothInstances_isRejected();
    void savingPerUserConfiguration_writesTheFileUnderHome();
    void savingPerUserConfiguration_reportsASaveThatCannotReachItsHome();
    void v1Presets_reachThePrintedConfiguration();
    void v1Presets_outrankAStoredLogicSection_data();
    void v1Presets_outrankAStoredLogicSection();
    void storedKeys_reachThePrintedConfiguration_data();
    void storedKeys_reachThePrintedConfiguration();
    void declaredConfigurationKeys_areTheStoredOnes();
    void savedConfiguration_carriesEveryStoredKey();
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
    void onlyStatusOverGet_getsTheSnapshot_data();
    void onlyStatusOverGet_getsTheSnapshot();
    void controlConnectionThatSaysNothing_isClosed();
    void controlPlaneIsOnlyReachableOnLoopback();
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

// config.cpp carries three short lines above its option list holding the letters that are still
// free -- a lookup table for whoever adds the next option, so that nobody has to walk the usage
// text looking for a slot. Nothing keeps it honest but a reader's attention: register an option
// and forget the table, and the next reader takes a letter that is already spent. This case is
// that attention written down -- the letters the table holds have to be exactly the ones the
// options in that same file do not claim.
//
// Both halves are read out of config.cpp as text, the way QMdmmCore's protocol case reads its
// page: the configuration code is not linked into this target, and the table is a comment.
//
// The usage text is deliberately not the source of truth. It is a second, hand-written copy of
// the same list, so a run can register an option and leave it out of that copy -- which is the
// very mistake this case is here to catch -- and the copy would still agree with a stale table.
void tst_QMdmmServer::freeShortOptions_areTheOnesNoOptionClaims()
{
    QFile source(QString::fromLatin1(QMDMMSERVER_CONFIG_SOURCE));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.fileName() + u": "_s + source.errorString()));

    const QString text = QString::fromLocal8Bit(source.readAll());

    // Every option the file registers names its short forms first: QCommandLineOption takes a
    // QStringList, and a one-character entry in it is a short form. An option carrying only a long
    // name is passed over here, which is right -- it claims no letter.
    const QRegularExpression declaredShortForm(u"QCommandLineOption\\(QStringList \\{\\s*u\"([A-Za-z0-9])\""_s);

    QSet<QChar> taken;
    QRegularExpressionMatchIterator declared = declaredShortForm.globalMatch(text);
    while (declared.hasNext())
        taken.insert(declared.next().captured(1).at(0));

    QVERIFY2(!taken.isEmpty(), "no short option was read out of the source");

    // v is the one short form the file does not spell out: addVersionOption() owns it. It is why
    // the table below leaves v out while --version is right there in the usage text.
    if (text.contains(u"addVersionOption()"_s))
        taken.insert(u'v');

    // The table itself: what sits between the #if 0 that keeps it out of the build and the
    // #endif that closes the block.
    const QRegularExpression blockStart(u"^#if 0\\s*$"_s);
    const QRegularExpression blockEnd(u"^#endif\\s*$"_s);

    QSet<QChar> listed;
    bool inBlock = false;
    const QStringList lines = text.split(u'\n');
    for (const QString &line : lines) {
        if (line.contains(blockStart))
            inBlock = true;
        else if (line.contains(blockEnd))
            inBlock = false;
        else if (inBlock) {
            for (const QChar character : line) {
                if (character.isLetterOrNumber())
                    listed.insert(character);
            }
        }
    }

    QVERIFY2(!listed.isEmpty(), "no letter was read out of the table in the source");

    QSet<QChar> expected;
    const QString alphabet = shortOptionAlphabet();
    for (const QChar letter : alphabet) {
        if (!taken.contains(letter))
            expected.insert(letter);
    }

    QString message = u"the free-slot table and the options disagree -- listed but already taken: ["_s + sortedLetters(listed - expected);
    message += u"]; free but not listed: ["_s + sortedLetters(expected - listed) + u"]"_s;
    QVERIFY2(listed == expected, qPrintable(message));
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

// -c saves the resolved configuration to the per-user file and exits 0. The file used to be a
// native-format store that ignored HOME; as an INI file it follows HOME, which is what lets this
// case assert on that one file rather than on whatever configuration the machine happens to hold.
//
// It writes the file it watches, so it writes where perUserCaseHome sends it: a home of its own off
// CI, and the machine's own on it. The writing is taken back out on the way out, because on CI that
// file is read by the runs tst_qmdmmgui6 starts as well. The platform whose home does not follow the
// environment is the one place that choice does not exist, and there the case has nothing of its own
// to watch off CI, so it skips.
void tst_QMdmmServer::savingPerUserConfiguration_writesTheFileUnderHome()
{
    QTemporaryDir scratch;
    const QString home = perUserCaseHome(scratch);
    if (home.isEmpty())
        QSKIP("the home does not follow the environment here and this machine is not one to be thrown away; a run would write the machine's own per-user file");

    const PerUserFileRestore restore(home);

    const RunResult result = runServer({u"-c"_s}, 60000, perUserCaseEnvironment(home));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    const QString configurationFile = perUserConfigurationFile(home);
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
//
// The blocker is what makes this case need more than a home it may write under: the file goes
// where the per-user directory would, and on the machine's own home that place is the user's
// configuration directory, which is not this case's to take away. So unlike its two neighbours
// it always asks for a home of its own -- on CI as well, where it is the platform's ability to
// move the home that is being used rather than a disposable machine. Where the platform has no
// home to offer -- its home does not follow the environment -- the blocker would land beside the
// write instead of in its way, the save would reach the machine's own file, and there would be
// nothing here to assert on. So it skips there.
void tst_QMdmmServer::savingPerUserConfiguration_reportsASaveThatCannotReachItsHome()
{
#ifndef Q_OS_WIN
    QTemporaryDir home;
    QVERIFY(home.isValid());

    QFile blocker(QDir(home.path()).absoluteFilePath(u".QMdmm"_s));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    const RunResult result = runServer({u"-c"_s}, 60000, environmentWithHome(home.path()));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, static_cast<int>(QSettings::AccessError));

    QVERIFY(!QFile::exists(perUserConfigurationFile(home.path())));
#else
    QSKIP("the home does not follow the environment here, and this case needs a home of its own to bend: the blocker would go where the machine's own per-user directory is");
#endif
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
//
// The file both rows read is one the case plants itself, under the home perUserCaseHome sends it: a
// home of its own off CI, and the machine's own on it, where a file being the machine's is the point
// rather than a problem. Where the home does not follow the environment there is nothing to plant
// under off CI -- a run reads the machine's own file whichever home the case asks for -- so the case
// skips rather than read that file and call whatever is in it its own setup. The file is taken back
// out on the way out, the way the case above takes its own writing back out: on CI it is under the
// machine's own home, and the server tst_qmdmmgui6 starts reads it from there, so a planted [logic]
// section left behind there would decide what that test's readings come out as.
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

    QTemporaryDir scratch;
    const QString home = perUserCaseHome(scratch);
    if (home.isEmpty())
        QSKIP("the home does not follow the environment here and this machine is not one to be thrown away; the stored file would have to be the machine's own");

    const PerUserFileRestore restore(home);

    // The directory the file goes in. Only the write below needs it, and a case that finds it already
    // there -- the machine's own home on CI, where the case above has written -- is not an error, so
    // what mkpath() returned is not the assertion; the directory being there is.
    const QDir perUserDirectory(QDir(home).absoluteFilePath(u".QMdmm/Fsu0413.me"_s));
    QDir(home).mkpath(u".QMdmm/Fsu0413.me"_s);
    QVERIFY(perUserDirectory.exists());

    QFile file(perUserConfigurationFile(home));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("[server]\ntcp-port=7777\n\n[logic]\nmaximum-maxhp=10\n") > 0);
    file.close();

    const RunResult result = runServer(arguments, 60000, perUserCaseEnvironment(home));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QVERIFY(result.standardOutput.contains(u"\"tcpPort\": 7777"_s));
    QVERIFY(result.standardOutput.contains(printed));
}

// The stored face of each key, one row at a time: a file naming one key and nothing else, and the
// printed configuration has to carry that value. The home handling is the same as the case above,
// and for the same reason -- the file is planted, so it has to be one the run will read.
void tst_QMdmmServer::storedKeys_reachThePrintedConfiguration_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("printed");

    for (const StoredServerKey &row : storedServerKeys()) {
        QTest::newRow(row.key) << QString::fromLatin1(row.key) + u"="_s + QString::fromLatin1(row.value)
                               << u"\""_s + QString::fromLatin1(row.property) + u"\": "_s + QString::fromLatin1(row.json);
    }
}

void tst_QMdmmServer::storedKeys_reachThePrintedConfiguration()
{
    QFETCH(QString, line);
    QFETCH(QString, printed);

    QTemporaryDir scratch;
    const QString home = perUserCaseHome(scratch);
    if (home.isEmpty())
        QSKIP("the home does not follow the environment here and this machine is not one to be thrown away; the stored file would have to be the machine's own");

    const PerUserFileRestore restore(home);

    const QDir perUserDirectory(QDir(home).absoluteFilePath(u".QMdmm/Fsu0413.me"_s));
    QDir(home).mkpath(u".QMdmm/Fsu0413.me"_s);
    QVERIFY(perUserDirectory.exists());

    QFile file(perUserConfigurationFile(home));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("[server]\n" + line.toUtf8() + "\n") > 0);
    file.close();

    const RunResult result = runServer({u"-d"_s}, 60000, perUserCaseEnvironment(home));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);
    QVERIFY2(result.standardOutput.contains(printed), qPrintable(u"the run did not print "_s + printed));
}

// The other direction, and the reason the table cannot rot: every property the class declares has to
// be a row above. Declaring a key in ServerConfiguration is all it takes for the key to exist, and a
// key with no option and no stored key is one nobody can reach -- so the declared set and the table
// have to be the same set.
//
// The header is read as text, the way the free-slot case reads config.cpp, and for the same reason:
// the class is in another module and is not linked into this target. Two readings, so that neither a
// property with no row nor a property the pattern fails to see can pass: the properties are declared
// one per line, and the number the pattern finds has to be the number of Q_PROPERTY() blocks in the
// file. The sets are compared before the counts, so that the case that is about a key names the key
// it disagrees about; the counts cover what the sets cannot see, a declaration made twice or one the
// pattern walks past.
void tst_QMdmmServer::declaredConfigurationKeys_areTheStoredOnes()
{
    QFile header(QString::fromLatin1(QMDMMSERVERCONFIGURATION_HEADER));
    QVERIFY2(header.open(QIODevice::ReadOnly), qPrintable(header.fileName() + u": "_s + header.errorString()));

    const QString text = QString::fromLocal8Bit(header.readAll());

    const QRegularExpression declaredProperty(u"Q_PROPERTY\\([^\\n]*?\\bREAD\\s+([A-Za-z_][A-Za-z0-9]*)"_s);

    QSet<QString> declared;
    QRegularExpressionMatchIterator matches = declaredProperty.globalMatch(text);
    while (matches.hasNext())
        declared.insert(matches.next().captured(1));

    QVERIFY2(!declared.isEmpty(), "no property was read out of the header");

    QSet<QString> stored;
    for (const StoredServerKey &row : storedServerKeys())
        stored.insert(QString::fromLatin1(row.property));

    QString message = u"the declared properties and the stored keys disagree -- declared but not stored: ["_s + sortedWords(declared - stored);
    message += u"]; stored but not declared: ["_s + sortedWords(stored - declared) + u"]"_s;
    QVERIFY2(declared == stored, qPrintable(message));

    const qsizetype declaredCount = text.count(u"Q_PROPERTY("_s);
    QVERIFY2(
        declared.size() == declaredCount,
        qPrintable(u"the header declares %1 properties and %2 were read out of it -- a property declared twice, or one whose READ the pattern does not see"_s.arg(declaredCount)
                       .arg(declared.size())));
}

// The write side: a key the program reads but does not save would be lost the moment a run replays
// its configuration through -c, and a file missing a key reads back as that key's default without
// saying so. One run, one file, and the keys of its [server] section have to be the table's keys --
// both ways, so a key the file carries but no row names is caught here as well.
void tst_QMdmmServer::savedConfiguration_carriesEveryStoredKey()
{
    QTemporaryDir scratch;
    const QString home = perUserCaseHome(scratch);
    if (home.isEmpty())
        QSKIP("the home does not follow the environment here and this machine is not one to be thrown away; a run would write the machine's own per-user file");

    const PerUserFileRestore restore(home);

    const RunResult result = runServer({u"-c"_s}, 60000, perUserCaseEnvironment(home));

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QFile file(perUserConfigurationFile(home));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString contents = QString::fromUtf8(file.readAll());

    const QSet<QString> saved = storedSectionKeys(contents, u"[server]"_s);
    QVERIFY2(!saved.isEmpty(), "the saved file has no [server] section");

    // The same reading over the file with the ending this platform does not write, so that the
    // reader is held to both: the run that added this case went red on the Windows entries alone,
    // where the section header was read back as "[server]\r" and the section came out empty.
    QString otherEnding = contents;
    otherEnding.replace(u"\r\n"_s, u"\n"_s);
    otherEnding.replace(u"\n"_s, u"\r\n"_s);
    QVERIFY2(storedSectionKeys(otherEnding, u"[server]"_s) == saved, "the reading of the saved file depends on the line endings it was written with");

    QSet<QString> stored;
    for (const StoredServerKey &row : storedServerKeys())
        stored.insert(QString::fromLatin1(row.key));

    QString message = u"the saved keys and the table disagree -- saved but not stored: ["_s + sortedWords(saved - stored);
    message += u"]; stored but not saved: ["_s + sortedWords(stored - saved) + u"]"_s;
    QVERIFY2(saved == stored, qPrintable(message));
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
// cannot bind a transport it was asked for is refused rather than staying up
// unreachable. The port cannot be picked by the child alone -- it may already be held
// by another process, which is exactly the case worth pinning -- so this case holds a
// port of its own and hands that port to the child.
//
// The refusal is the one a bad command line gets: the reason on stderr and status 3.
// That status is what tells the run apart from a run that hangs, together with the
// timeout guard in runServer(), which fails this case if the child never exits. The
// reason on stderr is the only line a caller sees: the transport error qCritical()
// reports goes to the log file instead, because QMdmmServer installs a message handler
// that writes there before it builds the server.
void tst_QMdmmServer::portThatIsAlreadyTaken_makesTheRunGiveUp()
{
    QTcpServer occupant;
    QVERIFY(occupant.listen(QHostAddress::Any, 0));
    const quint16 port = occupant.serverPort();

    const RunResult result = runServer({u"-t"_s, u"on"_s, u"-p"_s, QString::number(port), u"-l"_s, u"off"_s, u"-w"_s, u"off"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 3);
    QVERIFY2(result.standardError.contains(u"Unable to listen"_s), qPrintable(result.standardError));
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
// That difference is one the wire carries, not one Qt's socket does: a QTcpSocket reports a
// reset and an ordinary close the same way (RemoteHostClosedError, measured on both platforms
// this case runs on), so the peer here is a socket of this case's own and the reading is the
// one the syscall gives back -- the end of the stream, not ECONNRESET. Letting a QTcpSocket
// read it, or asking for peer.error(), would pass just as well on a run that reset instead of
// closing, which is the whole thing this half is here to tell apart.
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
    // comes back at once, so the pause is what the wait is made of. A refused connect leaves
    // its socket in no shape to be tried again, so each attempt takes a socket of its own.
    int peer = -1;
    const auto closePeer [[maybe_unused]] = qScopeGuard([&peer] {
        if (peer >= 0)
            ::close(peer);
    });
    for (int attempt = 0; attempt < 200 && peer < 0; ++attempt) {
        const int candidate = ::socket(AF_INET, SOCK_STREAM, 0);
        if (candidate < 0)
            qFatal("this case could not open a socket of its own");

        // The read below waits for the end of the stream: a run that neither closes nor resets
        // its ends -- one that hangs on to the socket -- has to come back as this case failing
        // rather than as one that never finishes. The timeout is a property of the socket, so
        // it goes on here, while the socket is still the case's own to set up.
        timeval receiveTimeout {};
        receiveTimeout.tv_sec = 60;
        if (::setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout)) != 0) {
            ::close(candidate);
            qFatal("this case could not put a receive timeout on its socket");
        }

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(candidate, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
            peer = candidate;
            break;
        }

        ::close(candidate);
        QTest::qWait(50);
    }
    if (peer < 0) {
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

    // What the run wrote before stopping -- the version it introduces its sockets with -- is
    // not what this half reads. What it is after is how the stream ends: a close hands that
    // back as a read of zero bytes, and a reset fails the same read with ECONNRESET instead.
    char buffer[4096];
    int readResult = -1;
    do {
        readResult = int(::recv(peer, buffer, sizeof(buffer), 0));
    } while (readResult > 0);

    QVERIFY2(readResult == 0, qPrintable(u"the peer read the stream as %1 bytes with errno %2 rather than as its end"_s.arg(readResult).arg(errno)));
#endif
}

// The control plane is the read-only face of the server's status snapshot: an HTTP GET on
// /status answers with the same snapshot an in-process caller reads from status(). It is
// asked for with a port of its own -- with no option there is no listener at all -- and it binds
// the loopback address, so this case reaches it the way an operator with a plain client would.
//
// The run it talks to listens on the control plane alone: the three game transports are turned
// off, which is the smallest run that has a status worth reading and also keeps this case from
// reaching anything but the plane.
//
// The request is sent again until the run answers it, which is also how this case waits for the run
// to reach its listener -- a run that is not there yet and a plane that says nothing are the same
// silence from here (see the note on askControlPlane).
//
// A path the plane does not serve, and a method it does not serve, are answered as misses rather
// than as the snapshot, so a mistyped path cannot be read as a healthy server.
void tst_QMdmmServer::onlyStatusOverGet_getsTheSnapshot_data()
{
    QTest::addColumn<QByteArray>("request");
    QTest::addColumn<int>("statusCode");

    QTest::newRow("status") << QByteArray("GET /status HTTP/1.0\r\n\r\n") << 200;
    QTest::newRow("unknownPath") << QByteArray("GET /nowhere HTTP/1.0\r\n\r\n") << 404;
    QTest::newRow("methodNotServed") << QByteArray("POST /status HTTP/1.0\r\n\r\n") << 404;
}

void tst_QMdmmServer::onlyStatusOverGet_getsTheSnapshot()
{
    QFETCH(QByteArray, request);
    QFETCH(int, statusCode);

    const quint16 controlPort = freePort();

    QProcess process;
    process.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), {u"-t"_s, u"off"_s, u"-l"_s, u"off"_s, u"-w"_s, u"off"_s, u"--control-port"_s, QString::number(controlPort)});
    if (!process.waitForStarted(60000))
        qFatal("the executable under test did not start");

    const auto stopProcess [[maybe_unused]] = qScopeGuard([&process] { stopRun(process); });

    QString diagnosis;
    const QByteArray answer = askControlPlane(process, controlPort, request, &diagnosis);

    QVERIFY2(!answer.isEmpty(), qPrintable(diagnosis));
    QVERIFY2(answer.startsWith(QByteArray("HTTP/1.0 ") + QByteArray::number(statusCode) + ' '), qPrintable(QString::fromLatin1(answer)));

    if (statusCode != 200)
        return;

    const qsizetype bodyStart = answer.indexOf("\r\n\r\n");
    QVERIFY2(bodyStart >= 0, qPrintable(QString::fromLatin1(answer)));

    // The answer is the snapshot itself: the keys status() promises, and nothing else. This run has
    // nothing going on -- no rooms, no games, nobody signed in -- so the three counts read as zero
    // and the per-room list as empty. A plane that answered with a constant would read the same
    // here, so what these say is the shape of what the plane serves and that an idle run reads as
    // idle; that the numbers are the run's own, and move with it, is what the in-process case in
    // the networking suite watches them do.
    const QJsonObject snapshot = QJsonDocument::fromJson(answer.mid(bodyStart + 4)).object();
    QCOMPARE(int(snapshot.size()), 4);
    QCOMPARE(snapshot.value(u"rooms"_s).toInt(), 0);
    QCOMPARE(snapshot.value(u"games"_s).toInt(), 0);
    QCOMPARE(snapshot.value(u"players"_s).toInt(), 0);
    QVERIFY(snapshot.value(u"roomList"_s).isArray());
    QCOMPARE(snapshot.value(u"roomList"_s).toArray().size(), 0);
}

// A control-plane connection that never asks anything is let go of by the run rather than held.
// This plane answers one request per connection and closes, so its other exit -- a request that
// grows past the size limit -- is one a peer that sends nothing never reaches. Without a deadline
// such a peer keeps the socket, the buffer behind it and the run's memory for as long as it cares
// to, which is a bound every other accepted socket in the run already has.
//
// The silence has to be unambiguous for this case to mean anything, so the plane is known to be
// serving before the connection is opened: the case asks for the snapshot first. A connection to
// a run that had not reached its listener yet fails outright rather than hanging, and a case that
// took that for the deadline firing would pass for the wrong reason -- see the note on
// askControlPlane about a connect that reports success with nothing behind it.
void tst_QMdmmServer::controlConnectionThatSaysNothing_isClosed()
{
    const quint16 controlPort = freePort();

    QProcess process;
    process.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), {u"-t"_s, u"off"_s, u"-l"_s, u"off"_s, u"-w"_s, u"off"_s, u"--control-port"_s, QString::number(controlPort)});
    if (!process.waitForStarted(60000))
        qFatal("the executable under test did not start");

    const auto stopProcess [[maybe_unused]] = qScopeGuard([&process] { stopRun(process); });

    QString diagnosis;
    const QByteArray answer = askControlPlane(process, controlPort, QByteArray("GET /status HTTP/1.0\r\n\r\n"), &diagnosis);
    QVERIFY2(answer.startsWith(QByteArray("HTTP/1.0 200 ")), qPrintable(diagnosis));

    QTcpSocket silent;
    silent.connectToHost(QHostAddress::LocalHost, controlPort);

    // The connect is read against the port it landed on, not against its own success, for the
    // reason askControlPlane spells out: Qt reports a connection to a port with no listener as
    // one that was made, with no peer.
    QVERIFY(silent.waitForConnected(5000));
    QCOMPARE(silent.peerPort(), controlPort);
    QCOMPARE(silent.state(), QAbstractSocket::ConnectedState);

    // Nothing is written from here on. The wait is well past the run's own deadline, so a
    // connection still open at the end of it is one the run never let go of.
    QVERIFY2(silent.waitForDisconnected(20000), "the control plane held a connection that never asked anything");
    QVERIFY(silent.state() != QAbstractSocket::ConnectedState);

    // And nothing was said to the peer either: what closed the connection is the deadline, not an
    // answer to a request that was never sent.
    QCOMPARE(silent.bytesAvailable(), qint64(0));

    // What ended was the connection, not the run: a run that had gone away would close the socket
    // just as well, so the plane is asked again and has to answer. This is also what says the
    // deadline is per connection rather than a shutdown of the plane.
    QCOMPARE(process.state(), QProcess::Running);
    const QByteArray again = askControlPlane(process, controlPort, QByteArray("GET /status HTTP/1.0\r\n\r\n"), &diagnosis);
    QVERIFY2(again.startsWith(QByteArray("HTTP/1.0 200 ")), qPrintable(diagnosis));
}

// The plane binds the loopback address rather than every address, so the machine's own number is
// not a way in. That claim had only ever been checked by opening the plane and reading a socket
// table by hand -- a check nobody repeats. Here the run is asked over the address the machine has
// besides loopback, and the answer has to be silence.
//
// The address is read off the interface list rather than written down, so what is asked is the
// machine's own number. A host whose only address is loopback (a container without a network, a
// machine with its interfaces down) has nothing to ask, and the case skips rather than pass on a
// check it did not make.
void tst_QMdmmServer::controlPlaneIsOnlyReachableOnLoopback()
{
    QHostAddress elsewhere;
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.isLoopback() || address.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        elsewhere = address;
        break;
    }

    if (elsewhere.isNull())
        QSKIP("this host has no address besides loopback, so there is no second way in to ask about");

    const quint16 controlPort = freePort();

    QProcess process;
    process.start(QString::fromLatin1(QMDMMSERVER_EXECUTABLE), {u"-t"_s, u"off"_s, u"-l"_s, u"off"_s, u"-w"_s, u"off"_s, u"--control-port"_s, QString::number(controlPort)});
    if (!process.waitForStarted(60000))
        qFatal("the executable under test did not start");

    const auto stopProcess [[maybe_unused]] = qScopeGuard([&process] { stopRun(process); });

    // The plane is known to be serving before the other address is tried: a run that had not
    // reached its listener yet would be silent on every address, which is the reading this case
    // has to tell apart from a plane that is bound to loopback only.
    QString diagnosis;
    const QByteArray answer = askControlPlane(process, controlPort, QByteArray("GET /status HTTP/1.0\r\n\r\n"), &diagnosis);
    QVERIFY2(answer.startsWith(QByteArray("HTTP/1.0 200 ")), qPrintable(diagnosis));

    QTcpSocket other;
    other.connectToHost(elsewhere, controlPort);

    // Judged on the port the connection landed on, not on whether the connect call reported
    // success: Qt reports a connection to a port with no listener as one that was made, with no
    // peer behind it (see the note on askControlPlane).
    const bool reached = other.waitForConnected(2000) && other.peerPort() == controlPort;
    QVERIFY2(!reached, qPrintable(u"the control plane answered on %1, which is not the loopback address"_s.arg(elsewhere.toString())));
}

QTEST_GUILESS_MAIN(tst_QMdmmServer)
#include "tst_qmdmmserver.moc"

// NOLINTEND

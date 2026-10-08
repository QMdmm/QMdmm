// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTest>

#include "gameclient.h"
#include "mainwindow.h"

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace Qt::StringLiterals;

namespace {

// Everything a run of the executable under test left behind.
struct RunResult
{
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
};

// Runs the executable under test with @p arguments and collects what it printed.
//
// The run made here -- the usage -- prints and exits on its own, so a run that
// outlives the timeout means the program stopped exiting where it is expected to:
// it read the command line and went on to its window, which never returns. The
// helper kills it and fails rather than reporting a result of its own.
//
// The platform plugin is named rather than inherited. QApplication needs one before
// main() reaches the parser, and the cases have to run both on a machine with no
// display (the CI runners) and on one with a display (a developer's machine, where
// the inherited value is the real one and a window would come up).
RunResult runGui(const QStringList &arguments, int timeoutMs = 60000)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QString::fromLatin1(QMDMMGUI_EXECUTABLE), arguments);

    if (!process.waitForStarted(timeoutMs))
        qFatal("the executable under test did not start");

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        qFatal("the executable under test did not exit; it probably went on to its window");
    }

    RunResult result;
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.standardError = QString::fromLocal8Bit(process.readAllStandardError());
    return result;
}

} // namespace

// The command line and the window are the two things the smoke test next to this file
// cannot reach: that one drives the QML scenes, while main() and MainWindow's
// constructor only run in the program itself. MainWindow is compiled into this target
// the way QMdmmGameClient is (both live in the executable, not in a library, so there
// is nothing to link against); main() is covered by running the built executable and
// reading what a user can observe from the outside -- the exit code and the two
// streams -- the way the command-line cases of QMdmmBot and QMdmmServer do.
//
// The window is built here rather than run: a run of this program -- bundles included --
// gets as far as its event loop and stays there, so a case that started one and then
// killed it would observe the window only up to the kill, and a killed process reports
// no coverage. Building the window directly is what reaches the constructor on a run
// that ends by itself.
//
// The QML the window puts in its view is not in this target: the module's resources
// belong to the executable, so the view comes up with nothing to load. What the case
// pins is the window the constructor builds -- its title, its content widget and the two
// paths it hands to the client -- while what the scenes paint is the smoke test's own
// subject.
class tst_QMdmmGuiApp : public QObject
{
    Q_OBJECT

private slots:
    void help_printsTheUsageAndExitsZero();
    void theWindow_handsTheGivenProgramPathsToTheClient();
};

void tst_QMdmmGuiApp::help_printsTheUsageAndExitsZero()
{
    const RunResult result = runGui({u"-h"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    QVERIFY(result.standardOutput.contains(u"QMdmm client"_s));
    // Both paths a local game needs are options of this program and of no other: they
    // are what the client starts the server and the bots of such a game from.
    QVERIFY(result.standardOutput.contains(u"--server"_s));
    QVERIFY(result.standardOutput.contains(u"--bot"_s));
}

// The two values from the command line reach the client in the constructor and nowhere
// else, and the client hands them on -- unexamined -- to the server and the bots of the
// first local game asked for. Locating them (and reporting one that leads nowhere when
// that game does come) is the client's own case, so what this one pins is the handover:
// a path that leads nowhere is passed through as the path the user asked for.
void tst_QMdmmGuiApp::theWindow_handsTheGivenProgramPathsToTheClient()
{
    const QString serverProgram = u"/a/server/that/does/not/exist"_s;
    const QString botProgram = u"/a/bot/that/does/not/exist"_s;

    MainWindow window(serverProgram, botProgram);

    QCOMPARE(window.windowTitle(), u"QMdmm"_s);

    // The scene is the window's content -- without it there is nothing to show.
    QVERIFY(window.centralWidget() != nullptr);

    const QMdmmGameClient *game = window.findChild<QMdmmGameClient *>();
    QVERIFY(game != nullptr);
    QCOMPARE(game->serverProgram(), serverProgram);
    QCOMPARE(game->botProgram(), botProgram);
}

QTEST_MAIN(tst_QMdmmGuiApp)

#include "tst_qmdmmguiapp.moc"

// NOLINTEND

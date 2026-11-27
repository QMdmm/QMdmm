// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSize>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <QMdmmSettings>

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
// The runs made here end on their own: the usage prints and exits, and a probe run leaves
// once the window is up (see main). So a run that outlives the timeout means the program
// stopped ending where it is expected to -- it read the command line and stayed in its
// window, which never returns on its own. The helper kills it and fails rather than
// reporting a result of its own.
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
// subject. What the startup has to say about the view is a reading of its own, and it is
// taken from a run of the program: the probe switch ends such a run once the window is up
// (see main), and the log it leaves behind is what
// theStartup_reachesTheViewWithoutWarningsFromTheBindings reads.
class tst_QMdmmGuiApp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void help_printsTheUsageAndExitsZero();
    void theWindow_handsTheGivenProgramPathsToTheClient();
    void theWindow_opensAtTheSizeTheLastRunWasClosedAt();
    void theStartup_reachesTheViewWithoutWarningsFromTheBindings();

private:
    // The home this process resolves, which is where a window that is closed leaves its size.
    // See initTestCase.
    QTemporaryDir home;
};

// Settings resolves the home directory once, when the first settings object of this process is
// built, and the per-user file it writes is fixed there. A closed window keeps its size in that
// file, so the home moves before any case builds a window; nothing here touches settings before
// this hook has returned. The runs of the executable started from the other cases inherit the
// environment and write into the same temporary home.
//
// Whether the home can be moved is the platform's answer rather than this file's: Windows takes
// the profile directory out of the process token before it looks at the environment, so the
// temporary home never becomes the one settings resolves there -- which is why the case that
// reads the file back names the keys it changes and puts them back rather than relying on an
// empty file.
void tst_QMdmmGuiApp::initTestCase()
{
    QVERIFY(home.isValid());
    qputenv("HOME", home.path().toLocal8Bit());

#ifndef Q_OS_WIN
    QCOMPARE(QDir::homePath(), home.path());
#endif
}

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

// A window opens at the size the last one was closed at, and that size outlives the run in the
// per-user configuration. What that is easy to get wrong does not show on the screen: the
// settings setter fills an in-memory instance while the save call is what reaches the file, and
// a window with nothing stored has to open at the default rather than at whatever size the
// widget happens to take.
void tst_QMdmmGuiApp::theWindow_opensAtTheSizeTheLastRunWasClosedAt()
{
    // Where the per-user file lives is fixed by the first settings object this process builds;
    // the plain QSettings below has to name the same file the window writes, so one is built
    // here to fix it.
    {
        const QMdmmCore::Settings settings;
    }

    QSettings stored(QSettings::IniFormat, QSettings::UserScope, u"Fsu0413.me"_s, u"QMdmm"_s);

    // Whatever size the client was last closed at on this machine belongs to whoever ran it
    // here, and this case is about to replace it; the values are put back at the end.
    const QVariant widthBefore = stored.value(u"gui/window-width"_s);
    const QVariant heightBefore = stored.value(u"gui/window-height"_s);

    // From nothing stored, which is where a run that has never been closed starts. The keys are
    // removed rather than the file, since a platform whose home does not follow the environment
    // resolves a real one (see initTestCase) and that file is not this case's to discard.
    stored.remove(u"gui/window-width"_s);
    stored.remove(u"gui/window-height"_s);
    stored.sync();

    {
        MainWindow window(u""_s, u""_s);
        QCOMPARE(window.size(), QSize(1280, 800));

        window.resize(1440, 900);
        QVERIFY(window.close());
    }

    // Read back through this instance rather than the settings object the window wrote
    // through: what is being asserted is what reached the file, and a setter whose save call is
    // missing leaves it without a size while a run of the client would still open at the one
    // that was in memory.
    stored.sync();
    QCOMPARE(stored.value(u"gui/window-width"_s).toInt(), 1440);
    QCOMPARE(stored.value(u"gui/window-height"_s).toInt(), 900);

    // The next run picks the size up where the last one left it.
    MainWindow window(u""_s, u""_s);
    QCOMPARE(window.size(), QSize(1440, 900));

    // The floor the window may not be dragged below.
    window.resize(200, 100);
    QCOMPARE(window.size(), QSize(1024, 700));

    if (widthBefore.isValid())
        stored.setValue(u"gui/window-width"_s, widthBefore);
    else
        stored.remove(u"gui/window-width"_s);

    if (heightBefore.isValid())
        stored.setValue(u"gui/window-height"_s, heightBefore);
    else
        stored.remove(u"gui/window-height"_s);

    stored.sync();
}

// The view is handed its context properties before its source, and MainWindow's own comment
// carries the reason: the scene binds to them while it builds, so a property set afterwards is
// one the compiled bindings never saw. What that ordering is worth shows up on the log of a
// run and nowhere else -- out of order, the scene says "game is not defined" once per read and
// a line per handler in a Connections element whose target never matched a signal. The window
// still comes up either way, so a run that reads the screen alone sees nothing wrong.
//
// The probe run's log has a second half: the program leaves the event loop and the window
// comes down, which is where the same bindings meet the same objects for the last time. Both
// halves are read here -- the case is about what a whole run says, not about one end of it.
void tst_QMdmmGuiApp::theStartup_reachesTheViewWithoutWarningsFromTheBindings()
{
    const RunResult result = runGui({u"--probe"_s});

    QVERIFY(result.exitStatus == QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);

    // The run has to have got as far as the view. Its source is one of this program's own
    // resources, so a URL that does not resolve is a log line of its own -- and a log without
    // warnings then only says that nothing was built.
    QVERIFY2(!result.standardError.contains(u"No such file or directory"_s), qPrintable(result.standardError));

    QVERIFY2(!result.standardError.contains(u"ReferenceError"_s), qPrintable(result.standardError));
    QVERIFY2(!result.standardError.contains(u"no signal of the target matches"_s), qPrintable(result.standardError));

    // The other end of the same log. The scene reads "game" through a context property and
    // the client behind it is a child of the window, so the two are taken down together at
    // the end of the run; children go in the order they were added, and out of order the
    // scene outlived that object and reported every binding it still held as a read of
    // null. MainWindow's destructor carries the reason for the order it uses. Kept here
    // because the order is not readable off the code -- it is one line, and what it does
    // shows up on the log of a run like this one.
    QVERIFY2(!result.standardError.contains(u"TypeError"_s), qPrintable(result.standardError));
}

QTEST_MAIN(tst_QMdmmGuiApp)

#include "tst_qmdmmguiapp.moc"

// NOLINTEND

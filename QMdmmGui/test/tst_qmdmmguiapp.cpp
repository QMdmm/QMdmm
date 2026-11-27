// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
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

// The places in @p path where an element that draws text opens without naming the size it draws
// at, as "path:line"; an unreadable file is reported in the same list, because a file the scan
// could not read would otherwise pass for one with nothing to report.
//
// The block an opening line starts is read to its matching closing brace by indentation: a line
// that is exactly a closing brace at the opening line's own depth ends it. A text opened and
// closed on one line is not read that way, so the openings the pattern finds are counted against
// the opening braces the file has and a difference is reported rather than passed over.
QStringList textsWithoutASize(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {path + u": could not be read"_s};

    const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');

    const QRegularExpression opening(uR"(^(\s*)(?:Text|TextInput)\s*\{\s*$)"_s);
    const QRegularExpression anyOpening(uR"(\b(?:Text|TextInput)\s*\{)"_s);

    QStringList problems;
    int readOpenings = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch match = opening.match(lines.at(i));
        if (!match.hasMatch())
            continue;

        ++readOpenings;
        const int depth = match.captured(1).size();
        bool sized = false;
        for (int j = i + 1; j < lines.size(); ++j) {
            const QString &line = lines.at(j);
            if (line.trimmed() == u"}"_s && line.size() - line.trimmed().size() <= depth)
                break;
            if (line.contains(u"font.pixelSize"_s)) {
                sized = true;
                break;
            }
        }

        if (!sized)
            problems.append(path + u':' + QString::number(i + 1));
    }

    int openings = 0;
    QRegularExpressionMatchIterator it = anyOpening.globalMatch(lines.join(u'\n'));
    while (it.hasNext()) {
        it.next();
        ++openings;
    }
    if (openings != readOpenings)
        problems.append(path + u": an element that draws text is opened other than on a line of its own"_s);

    return problems;
}

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
    void theScene_givesEveryTextItsSize();

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

// A text that names no size of its own is drawn at whatever the application's font says, and the
// startup used to name one there. The texts that left their size out -- a button's label, the
// lines that say what a field is for, the paragraph of the about popup -- came out at that one
// size, larger than the text around them and larger than the boxes they sat in, and nothing on
// screen said which texts those were. The sizes are the bridge's now (see
// QMdmmGameClient::fontSizeBody and fontSizeButton), so what is left to hold is that no element
// goes back to a size nobody named. The scene is read as text here because the shape being
// guarded is a property of the source; the elements themselves only show up on a screen with
// somebody looking at it.
void tst_QMdmmGuiApp::theScene_givesEveryTextItsSize()
{
    const QDir scene(QString::fromLatin1(QMDMMGUI_QML_DIR));
    const QFileInfoList files = scene.entryInfoList({u"*.qml"_s}, QDir::Files, QDir::Name);

    // The directory comes from the build system rather than from this file, and a scan of nothing
    // reads as a scan that found nothing wrong.
    QVERIFY2(!files.isEmpty(), qPrintable(scene.absolutePath()));

    QStringList problems;
    for (const QFileInfo &file : files)
        problems += textsWithoutASize(file.absoluteFilePath());

    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u'\n')));
}

QTEST_MAIN(tst_QMdmmGuiApp)

#include "tst_qmdmmguiapp.moc"

// NOLINTEND

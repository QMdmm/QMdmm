// SPDX-License-Identifier: AGPL-3.0-or-later

#include "mainwindow.h"

#include "gameclient.h"

#include <QMdmmPlayer>
#include <QMdmmSettings>

#include <QCloseEvent>
#include <QQuickWidget>
#include <QUrl>
#include <QtQml>

using namespace Qt::StringLiterals;

namespace {

// The size a run opens at when no size has been stored yet. It is not a constraint the scene
// was designed against: main.qml draws into a fixed logical height of 1024 and scales it, so
// the window size only decides how large that drawing comes out and how much of the width is
// left as margin beside it.
constexpr int defaultWindowWidth = 1280;
constexpr int defaultWindowHeight = 800;

// What the window may be dragged down to -- the size the scene's layout is known to hold at.
// The scene is drawn into a fixed logical height and scaled to the window, so going below this
// shrinks that drawing rather than rearranging it.
constexpr int minimumWindowWidth = 1024;
constexpr int minimumWindowHeight = 700;

} // namespace

MainWindow::MainWindow(const QString &serverProgram, const QString &botProgram, QWidget *parent)
    : QMainWindow(parent)
    , m_view(nullptr)
{
    setWindowTitle(QObject::tr("QMdmm"));

    // The size the last run was closed at, from the per-user instance, where closeEvent puts
    // it. resize() clamps to the minimum set first, so a size stored by a window that was
    // allowed to be smaller cannot open one below it.
    setMinimumSize(minimumWindowWidth, minimumWindowHeight);

    QMdmmCore::Settings settings;
    settings.beginGroup(u"gui"_s);
    const int width = settings.value(u"window-width"_s, defaultWindowWidth).toInt();
    const int height = settings.value(u"window-height"_s, defaultWindowHeight).toInt();
    settings.endGroup();
    resize(width, height);

    // Register the core data enums and the Player type so the QML layer can
    // read properties and use action / upgrade constants.
    //
    // Registered is not the same as safe to read, though: the QML compiler
    // folds the value it finds into the compiled unit, and the on-disk cache is
    // keyed by the QML source alone -- renumber the C++ enum and nothing
    // recompiles, so the stale value stays in place and nothing fails. A
    // constant that has to cross into QML belongs on a live object instead,
    // read at run time, the way QMdmmGameClient hands out the state masks.
    qmlRegisterUncreatableMetaObject(QMdmmCore::Data::staticMetaObject, "QMdmm.Core", 1, 0, "Data", u"Access to enums only"_s);
    qmlRegisterUncreatableType<QMdmmCore::Player>("QMdmm.Core", 1, 0, "Player", u"Player is created by the engine"_s);

    QMdmmGameClient *game = new QMdmmGameClient(this);

    // Where the local game starts its server and its bots from, as given on the
    // command line; the client resolves the rest and holds on to both paths until
    // the local game asks for them.
    game->setProgramPaths(serverProgram, botProgram);

    // The source comes last. Handing it to the constructor loads the scene on the
    // spot, and everything the scene binds to has to be in place by then: the view
    // reads "game" and "MainWindowInstance" while it builds, so a property set
    // afterwards is one the compiled bindings never saw -- the startup used to print
    // a ReferenceError for every one of those reads, and a Connections element whose
    // target is still undefined reports every handler in it as one no signal matches.
    QQuickWidget *qw = new QQuickWidget(this);
    m_view = qw;

    qw->setResizeMode(QQuickWidget::SizeViewToRootObject);
    qw->rootContext()->setContextProperty(u"game"_s, static_cast<QObject *>(game));
    qw->rootContext()->setContextProperty(u"MainWindowInstance"_s, static_cast<QObject *>(this));

    qw->setSource(QUrl(u"qrc:///qt/qml/QMdmm/Gui/qml/main.qml"_s));

    setCentralWidget(qw);
}

// Where the size goes for the next run to open at. setValue() fills the specified instance
// only -- it writes no file -- so the save call below is what puts the size in the per-user
// one; without it nothing is reported and the next run opens at the default again.
void MainWindow::closeEvent(QCloseEvent *event)
{
    QMdmmCore::Settings settings;
    settings.beginGroup(u"gui"_s);
    settings.setValue(u"window-width"_s, width());
    settings.setValue(u"window-height"_s, height());
    settings.endGroup();
    settings.saveConfig(QMdmmCore::Settings::PerUser);

    QMainWindow::closeEvent(event);
}

// The scene reads "game" through the context property, and the game client is a child of
// this window, so the two are taken down together at the end of a run. Children go in the
// order they were added and the client is the older one: left alone, the object every
// binding in the scene reads is destroyed while the bindings are still there, each of them
// re-evaluates against a null context property, and a run that otherwise ended quietly
// printed a TypeError for every binding the scene still held. Taking the view down first
// destroys those bindings while the object they read is still in place, so the teardown says
// nothing.
MainWindow::~MainWindow()
{
    delete m_view;
    m_view = nullptr;
}

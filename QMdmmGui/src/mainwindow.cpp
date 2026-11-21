// SPDX-License-Identifier: AGPL-3.0-or-later

#include "mainwindow.h"

#include "gameclient.h"

#include <QMdmmPlayer>

#include <QQuickWidget>
#include <QUrl>
#include <QtQml>

using namespace Qt::StringLiterals;

MainWindow::MainWindow(const QString &serverProgram, const QString &botProgram, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QObject::tr("QMdmm"));

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

    qw->setResizeMode(QQuickWidget::SizeViewToRootObject);
    qw->rootContext()->setContextProperty(u"game"_s, static_cast<QObject *>(game));
    qw->rootContext()->setContextProperty(u"MainWindowInstance"_s, static_cast<QObject *>(this));

    qw->setSource(QUrl(u"qrc:///qt/qml/QMdmm/Gui/qml/main.qml"_s));

    setCentralWidget(qw);
}

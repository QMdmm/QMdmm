// SPDX-License-Identifier: AGPL-3.0-or-later

#include "mainwindow.h"

#include "gameclient.h"

#include <QMdmmPlayer>

#include <QQuickWidget>
#include <QtQml>

using namespace Qt::StringLiterals;

MainWindow::MainWindow(const QString &serverProgram, const QString &botProgram, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QObject::tr("QMdmm"));

    // Register the core data enums and the Player type so the QML layer can
    // read properties and use action / upgrade constants.
    qmlRegisterUncreatableMetaObject(QMdmmCore::Data::staticMetaObject, "QMdmm.Core", 1, 0, "Data", u"Access to enums only"_s);
    qmlRegisterUncreatableType<QMdmmCore::Player>("QMdmm.Core", 1, 0, "Player", u"Player is created by the engine"_s);

    QMdmmGameClient *game = new QMdmmGameClient(this);

    // Where the local game starts its server and its bots from, as given on the
    // command line; the client resolves the rest and holds on to both paths until
    // the local game asks for them.
    game->setProgramPaths(serverProgram, botProgram);

    QQuickWidget *qw = new QQuickWidget(u"qrc:///qt/qml/QMdmm/Gui/qml/main.qml"_s, this);

    qw->setResizeMode(QQuickWidget::SizeViewToRootObject);
    qw->rootContext()->setContextProperty(u"game"_s, static_cast<QObject *>(game));
    qw->rootContext()->setContextProperty(u"MainWindowInstance"_s, static_cast<QObject *>(this));

    setCentralWidget(qw);
}

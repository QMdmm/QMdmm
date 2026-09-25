// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QtQuickTest/quicktest.h>

#include <QtQml>
#include <QQmlContext>

#include <QMdmmData>
#include <QMdmmPlayer>

#include "gameclient.h"

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

// Registers the GUI bridge type and the core data types with the QML engine so
// the QML test cases can instantiate QMdmmGameClient and inspect the Player
// objects / enum values it exposes. Mirrors the registration done by MainWindow
// (which is not linked into this test).
class QMdmmGuiTestSetup : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    QMdmmGuiTestSetup()
        : m_game(new QMdmmGameClient(this))
    {
        qmlRegisterUncreatableMetaObject(QMdmmCore::Data::staticMetaObject, "QMdmm.Core", 1, 0, "Data", u"Access to enums only"_s);
        qmlRegisterUncreatableType<QMdmmCore::Player>("QMdmm.Core", 1, 0, "Player", u"Player is created by the engine"_s);
        qmlRegisterType<QMdmmGameClient>("QMdmm.Gui", 1, 0, "GameClient");
    }

    // The scenes reach the client through the `game` context property, exactly
    // as MainWindow installs it. The QML cases need the same view of the world,
    // so they get an idle client (nothing happens until it is started) to drive
    // by emitting its signals. Qt looks this up on the setup object's meta
    // object, so it has to be a slot.
public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        // A local game runs on the QMdmmServer program, and a bridge is told where the two
        // programs it needs are (MainWindow does it from the paths the command line carries).
        // This fixture has no command line, so it hands over the same empty pair: both are then
        // looked up next to this test, which is where the build tree keeps them.
        //
        // It happens here rather than in the constructor because the setup object is built
        // before the application is: this is the first point at which "next to this program"
        // means anything.
        m_game->setProgramPaths({}, {});

        engine->rootContext()->setContextProperty(u"game"_s, m_game);
    }

private:
    QMdmmGameClient *m_game = nullptr;
};

QUICK_TEST_MAIN_WITH_SETUP(qmdmmgui, QMdmmGuiTestSetup)

#include "tst_qmdmmgui.moc"

// NOLINTEND

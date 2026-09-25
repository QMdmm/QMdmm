// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QtQuickTest/quicktest.h>

#include <QQmlContext>
#include <QtQml>

#include <QMdmmAgent>
#include <QMdmmData>
#include <QMdmmPlayer>

#include "gameclient.h"

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

// Every request and every notification the agent can raise has to end up on the screen -- that
// is what the 0.0.2 version is accepted on -- and nothing in the build enforces it. A new
// notification on QMdmmNetworking::Agent compiles, keeps every other test green, and simply
// never shows up in the GUI; and which signals were *meant* to be left out was written down
// nowhere -- the one exemption this table carries lived in a review note.
//
// The table below closes it at the only place that can see the whole surface: the meta object
// of the class that declares the signals. Each request and notification carries a row -- either
// the bridge consumes it, or the row says why the GUI may leave it alone. A signal with no row
// fails the test, a row whose signal is gone fails it too (a rename is as visible as an
// addition), and so does an exemption that does not say why.
//
// Scope: the two families the criterion names, i.e. the signals whose names end in `Notified`
// or `Requested` -- every NotifyId and every RequestId of the protocol lands on one of those.
//
// What it does not check: that the bridge connects what its rows claim. Qt does not expose a
// QObject's incoming connections, so the rows are declarations rather than proof -- but a
// declaration is precisely what used to be missing.
class QMdmmAgentInventory : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // One line per problem with the table; empty is the expected reading. The lines name the
    // signal and what is wrong with it, so the test message says which signal to look at.
    Q_INVOKABLE QStringList problems() const;
};

namespace {

// How the GUI stands with respect to one of the signals.
enum class Disposition
{
    // The bridge consumes it -- see QMdmmGameClient::wireClient().
    Consumed,
    // The GUI may leave it alone, for the reason spelled out next to it.
    Exempt,
};

struct AgentSignalDisposition
{
    const char *name;
    Disposition disposition;
    // Why the GUI may leave this signal alone; empty unless the disposition is Exempt.
    const char *exemptBecause;
};

constexpr AgentSignalDisposition kAgentSignalDispositions[] = {
    {"actionNotified", Disposition::Consumed, ""},
    {"actionOrderNotified", Disposition::Consumed, ""},
    {"actionOrderRequested", Disposition::Consumed, ""},
    {"actionRequested", Disposition::Consumed, ""},
    {"agentStateChangeNotified", Disposition::Consumed, ""},
    {"gameOverNotified", Disposition::Consumed, ""},
    {"gameStartNotified", Disposition::Consumed, ""},
    {"logicConfigurationNotified", Disposition::Consumed, ""},
    {"playerAddNotified", Disposition::Consumed, ""},
    {"playerRemoveNotified", Disposition::Consumed, ""},
    {"rockPaperScissorsNotified", Disposition::Consumed, ""},
    {"rockPaperScissorsRequested", Disposition::Consumed, ""},
    {"roundOverNotified", Disposition::Consumed, ""},
    {"roundStartNotified", Disposition::Consumed, ""},
    {"speakNotified", Disposition::Consumed, ""},
    {"upgradeNotified", Disposition::Consumed, ""},
    {"upgradeRequested", Disposition::Consumed, ""},

    // The one exemption: the payload is the protocol's unfinished OB functionality, still a
    // `@todo` (see Protocol::NotifyOperate), so there is no content to put on the screen yet.
    {"operateNotified", Disposition::Exempt, "the payload is the protocol's unfinished OB functionality"},
};

// Reads the names off the meta object rather than off a list, so the table cannot quietly
// become the only thing being compared with itself.
QStringList declaredRequestAndNotificationSignals()
{
    const QMetaObject *metaObject = &QMdmmNetworking::Agent::staticMetaObject;

    QStringList names;
    for (int i = 0; i < metaObject->methodCount(); ++i) {
        const QMetaMethod method = metaObject->method(i);
        if (method.methodType() != QMetaMethod::Signal)
            continue;

        const QString name = QString::fromLatin1(method.name());
        if (!name.endsWith(u"Notified"_s) && !name.endsWith(u"Requested"_s))
            continue;
        names.append(name);
    }
    return names;
}

bool hasDispositionRow(const QString &name)
{
    for (const AgentSignalDisposition &disposition : kAgentSignalDispositions) {
        if (name == QLatin1String(disposition.name))
            return true;
    }
    return false;
}

} // namespace

QStringList QMdmmAgentInventory::problems() const
{
    const QStringList declared = declaredRequestAndNotificationSignals();

    QStringList result;
    for (const QString &name : declared) {
        if (!hasDispositionRow(name))
            result.append(u"no row: %1"_s.arg(name));
    }
    for (const AgentSignalDisposition &disposition : kAgentSignalDispositions) {
        const QString name = QString::fromLatin1(disposition.name);
        if (!declared.contains(name)) {
            result.append(u"no such signal: %1"_s.arg(name));
            continue;
        }

        // A row that only says "not shown" is the silence this table exists to break, and a
        // reason left behind on a signal that is consumed now is a lie in the file.
        const bool hasReason = *disposition.exemptBecause != '\0';
        if (disposition.disposition == Disposition::Exempt && !hasReason)
            result.append(u"no reason: %1"_s.arg(name));
        if (disposition.disposition == Disposition::Consumed && hasReason)
            result.append(u"reason on a consumed signal: %1"_s.arg(name));
    }

    result.sort();
    return result;
}

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
        , m_agentInventory(new QMdmmAgentInventory(this))
    {
        qmlRegisterUncreatableMetaObject(QMdmmCore::Data::staticMetaObject, "QMdmm.Core", 1, 0, "Data", u"Access to enums only"_s);
        qmlRegisterUncreatableType<QMdmmCore::Player>("QMdmm.Core", 1, 0, "Player", u"Player is created by the engine"_s);
        qmlRegisterUncreatableType<QMdmmAgentInventory>("QMdmm.Gui", 1, 0, "AgentInventory", u"Used by the test fixture only"_s);
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

        // The one case that reads the agent's signal surface instead of driving the client --
        // see QMdmmAgentInventory above for what it checks and why it lives here rather than in
        // a build step.
        engine->rootContext()->setContextProperty(u"agentInventory"_s, m_agentInventory);
    }

private:
    QMdmmGameClient *m_game = nullptr;
    QMdmmAgentInventory *m_agentInventory = nullptr;
};

QUICK_TEST_MAIN_WITH_SETUP(qmdmmgui, QMdmmGuiTestSetup)

#include "tst_qmdmmgui.moc"

// NOLINTEND

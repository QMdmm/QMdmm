// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QtQuickTest/quicktest.h>

#include <QMetaEnum>
#include <QQmlContext>
#include <QSignalSpy>
#include <QStringList>
#include <QVariantMap>
#include <QtQml>

#include <QMdmmAgent>
#include <QMdmmClient>
#include <QMdmmData>
#include <QMdmmPlayer>
#include <QMdmmServer>

#include "gameclient.h"

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

// Every request and every notification the agent can raise has to end up on the screen -- that
// is what the 0.1.0 version is accepted on -- and nothing in the build enforces it. A new
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
    // Not consumed any more, and not a silent gap either: the bridge reads agent states through
    // the client's own signal, which reports the mirrors' states and its own alike (see
    // QMdmmGameClient::wireClient).
    {"agentStateChangeNotified", Disposition::Exempt, "the bridge reads agent states through the client's own signal"},
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

    // The one signal exempt for want of content rather than for want of a reader: its payload is the
    // protocol's unfinished OB functionality, still a `@todo` (see Protocol::NotifyOperate), so
    // there is no content to put on the screen yet.
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

// Drives a bridge from the far end of its wiring, for the three places where an agent call turns
// into something the screen acts on: the request that asks a player to spend an upgrade point, the
// notice that the match is over, and the reply that carries the choice back. The cases go through
// this object rather than reaching the agent themselves, because the bridge keeps its client
// private -- the client is a child of the bridge all the same (see QMdmmGameClient::connectOnline),
// and the agent is the end those three points are wired to (see QMdmmGameClient::wireClient).
//
// Every call names the bridge it drives: each case builds its own, as it builds its own GameClient.
// No server stands behind any of this -- the cases point the bridge at a socket name with nothing
// at it, which is refused at once everywhere, and then raise the agent's own entry points.
class QMdmmBridgeDriver : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // The server asking this player which upgrades to spend the point on. False when the bridge has
    // no agent to raise it on, which is what a bridge that was never started looks like.
    Q_INVOKABLE bool askForUpgradeChoices(QObject *bridge, int remainingTimes) const;

    // The match is over and these players won.
    Q_INVOKABLE bool endTheMatch(QObject *bridge, const QStringList &winners) const;

    // Hand a set of choices back the way the upgrade overlay does, and read out what the agent was
    // given: the items as numbers, or an empty list when nothing reached it. The reply this sends is
    // never the empty one, so an empty reading means the choices did not leave the bridge.
    Q_INVOKABLE QVariantList replyToUpgradeChoices(QObject *bridge, const QVariantList &items) const;

private:
    [[nodiscard]] static QMdmmNetworking::Agent *agentOf(const QObject *bridge);
};

QMdmmNetworking::Agent *QMdmmBridgeDriver::agentOf(const QObject *bridge)
{
    const auto *game = qobject_cast<const QMdmmGameClient *>(bridge);
    if (game == nullptr)
        return nullptr;

    // The one reading of the bridge's client available from outside: it is a child of the bridge.
    auto *client = game->findChild<QMdmmNetworking::Client *>();
    return client == nullptr ? nullptr : client->agent();
}

bool QMdmmBridgeDriver::askForUpgradeChoices(QObject *bridge, int remainingTimes) const
{
    QMdmmNetworking::Agent *agent = agentOf(bridge);
    if (agent == nullptr)
        return false;

    agent->requestUpgrade(remainingTimes);
    return true;
}

bool QMdmmBridgeDriver::endTheMatch(QObject *bridge, const QStringList &winners) const
{
    QMdmmNetworking::Agent *agent = agentOf(bridge);
    if (agent == nullptr)
        return false;

    agent->notifyGameOver(winners);
    return true;
}

QVariantList QMdmmBridgeDriver::replyToUpgradeChoices(QObject *bridge, const QVariantList &items) const
{
    auto *game = qobject_cast<QMdmmGameClient *>(bridge);
    QMdmmNetworking::Agent *agent = agentOf(bridge);
    if (game == nullptr || agent == nullptr)
        return {};

    // What the agent was handed, read off the signal the bridge's reply ends on. The choices travel
    // to the agent as an Agent::upgrade() call and are read back here as the items it carried,
    // rather than as the numbers the case typed in -- the conversion between the two is the whole of
    // what replyUpgrade does.
    //
    // Named by signature rather than by pointer to member: every signal on Agent carries the private
    // signal marker for a last parameter, so the pointer form is not open to a caller outside the
    // class, while the meta object lists the marker as no parameter of its own -- the signature
    // below is the one it holds.
    QSignalSpy replies(agent, SIGNAL(replyUpgrade(QList<QMdmmCore::Data::UpgradeItem>)));

    game->replyUpgrade(items);

    QVariantList sent;
    for (const QList<QVariant> &reply : replies) {
        const auto given = qvariant_cast<QList<QMdmmCore::Data::UpgradeItem>>(reply.value(0));
        for (QMdmmCore::Data::UpgradeItem item : given)
            sent.append(static_cast<int>(item));
    }
    return sent;
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
        , m_bridgeDriver(new QMdmmBridgeDriver(this))
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

        // What drives a bridge from the far end -- see QMdmmBridgeDriver above. The cases that
        // reach the upgrade request, the end of the match and the upgrade reply need it, because
        // the client those are wired to is private to the bridge and the agent is not otherwise
        // reachable from QML.
        engine->rootContext()->setContextProperty(u"bridgeDriver"_s, m_bridgeDriver);

        // The two names a local game's ends have to agree on, for the case in tst_gameclient.qml
        // that compares them: the socket the bridge hands its own client and the bots it starts,
        // and the socket the server it starts listens on -- which is the server configuration's
        // own default, because the server is handed no name at all. Nothing in the build ties the
        // two together, and a game played here only finds out that they parted by a room that
        // never fills (see that case for why it is worth saying earlier).
        engine->rootContext()->setContextProperty(u"bridgeSocketName"_s, QMdmmGameClient::serverSocketName());
        engine->rootContext()->setContextProperty(u"serverDefaultSocketName"_s, QMdmmNetworking::ServerConfiguration::defaults().localSocketName());

        // The stand-in bot the seat-watch case hands the bridge: a program that leaves with code 0
        // at once, which is what a bot's own exit looks like once the match it played is over (see
        // botstandin.cpp). It arrives resolved from the build system rather than looked up here, so
        // the case says nothing about where the build keeps its programs.
        engine->rootContext()->setContextProperty(u"standInBotProgram"_s, QString::fromUtf8(QMDMM_STANDIN_BOT));

        // The values behind the two enums the scene names in its own branches -- the punish
        // rounding its rules strip spells out, and the rock-paper-scissors pick its match log
        // spells out. It is handed both as plain numbers, so the branches only hold while their
        // numbering agrees with the core enum's, and nothing in the build says a word if it stops
        // doing so. The case in tst_gamescene.qml is the other end of this: it feeds these in and
        // asks the scene for the name.
        //
        // Read here rather than written into the .qml file, because a value spelled out in QML is
        // folded into the compiled unit and the disk cache that holds it is keyed on the QML
        // source alone -- a renumbering underneath would go on being answered with the old number,
        // and the case would pass on a scene that had stopped following the enum.
        QVariantMap coreEnumValues;
        coreEnumValues.insert(u"rpsRock"_s, static_cast<int>(QMdmmCore::Data::Rock));
        coreEnumValues.insert(u"rpsScissors"_s, static_cast<int>(QMdmmCore::Data::Scissors));
        coreEnumValues.insert(u"rpsPaper"_s, static_cast<int>(QMdmmCore::Data::Paper));
        coreEnumValues.insert(u"punishRoundDown"_s, static_cast<int>(QMdmmCore::LogicConfiguration::RoundDown));
        coreEnumValues.insert(u"punishRoundToNearest45"_s, static_cast<int>(QMdmmCore::LogicConfiguration::RoundToNearest45));
        coreEnumValues.insert(u"punishRoundUp"_s, static_cast<int>(QMdmmCore::LogicConfiguration::RoundUp));
        coreEnumValues.insert(u"punishPlusOne"_s, static_cast<int>(QMdmmCore::LogicConfiguration::PlusOne));
        engine->rootContext()->setContextProperty(u"coreEnumValues"_s, coreEnumValues);

        // The names of the bridge's own state enum, read off its meta object here for the case in
        // tst_gameclient.qml. The enum is reachable at all because gameclient.h registers it
        // (Q_ENUM), and that registration is what QML would go through to name one of its states;
        // nothing else reads it, so a registration that went away would take no other reading with
        // it. Read here rather than spelled out in the .qml file for the reason right above.
        QStringList gameStateNames;
        const QMetaEnum gameState = QMetaEnum::fromType<QMdmmGameClient::GameState>();
        for (int i = 0; i < gameState.keyCount(); ++i)
            gameStateNames.append(QString::fromLatin1(gameState.key(i)));
        engine->rootContext()->setContextProperty(u"gameStateNames"_s, gameStateNames);
    }

private:
    QMdmmGameClient *m_game = nullptr;
    QMdmmAgentInventory *m_agentInventory = nullptr;
    QMdmmBridgeDriver *m_bridgeDriver = nullptr;
};

// The setup object is a fixture, not a test. Qt looks up two hooks on it by name -- applicationAvailable() and
// qmlEngineAvailable(QQmlEngine *) (the one above) -- and nothing else here runs: a case written into the setup
// class's own private slots is never run at all, and it fails silently. The totals stay where they were and its
// name never reaches the output, so such a run is indistinguishable from one that passed it. Cases live in the
// .qml files, and the readings they assert on arrive from here as context properties. What tells a case that ran
// from one that did not is its name in the output and the totals going up by one -- never the exit code.
QUICK_TEST_MAIN_WITH_SETUP(qmdmmgui, QMdmmGuiTestSetup)

#include "tst_qmdmmgui.moc"

// NOLINTEND

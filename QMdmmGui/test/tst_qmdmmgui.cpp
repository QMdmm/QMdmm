// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QtQuickTest/quicktest.h>

#include <QMetaEnum>
#include <QMutex>
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

// What the program wrote to the log while a case was watching, for QMdmmLogWatch below. The lines
// arrive from whichever thread raised them, so they are kept under a lock; the handler itself is
// called on the thread that logged.
QMutex g_logLinesMutex;
QStringList g_logLines;
QtMessageHandler g_replacedLogHandler = nullptr;

void keepLogLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    {
        QMutexLocker locker(&g_logLinesMutex);
        g_logLines.append(message);
    }

    // Handed on rather than swallowed. Whatever handler was in place writes the run's own report of
    // what each case printed, and this one is only here to keep a copy.
    if (g_replacedLogHandler != nullptr)
        g_replacedLogHandler(type, context, message);
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

// Counts what the program wrote to the log, for the rule that the log and the screen say the same
// thing about a connection that was lost (see QMdmmGameClient::wireClient). The screen's half of
// that rule is a signal a case can spy on; the log's half is a line nobody sees, so without this it
// is only a run by hand and a look at the terminal -- and moving the line back above the check the
// local game relies on leaves the whole suite green.
//
// A fragment rather than a whole line: the line carries the transport's own reason after the colon,
// which is English out of somebody else's catalogue and differs per platform.
class QMdmmLogWatch : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Start a fresh reading. Installed afresh for every case rather than once for the run: the test
    // runner puts a handler of its own in place per case, so one kept from an earlier case would be
    // handing lines to a handler that is no longer listening.
    Q_INVOKABLE void watch()
    {
        {
            QMutexLocker locker(&g_logLinesMutex);
            g_logLines.clear();
        }

        const QtMessageHandler replaced = qInstallMessageHandler(&keepLogLine);
        if (replaced != &keepLogLine)
            g_replacedLogHandler = replaced;
    }

    Q_INVOKABLE int countContaining(const QString &fragment) const
    {
        QMutexLocker locker(&g_logLinesMutex);

        int seen = 0;
        for (const QString &line : g_logLines) {
            if (line.contains(fragment))
                ++seen;
        }
        return seen;
    }
};

// Drives a bridge from the far end of its wiring, for the places where an agent call turns into
// something the screen acts on: the request that asks a player to spend an upgrade point, the
// notice that the match is over, and the reply that carries the choice back. The cases go through
// this object rather than reaching the agent themselves, because the bridge keeps its client
// private -- the client is a child of the bridge all the same (see QMdmmGameClient::connectOnline),
// and the agent is the end those are wired to (see QMdmmGameClient::wireClient).
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

    // Fill the bridge's room mirror with a room of `seats` seats, as a server would have it by the
    // time it asks this side to act: one player per seat, each in the city its seat names, and this
    // side standing in the Village -- where every city is a place it may move to, so the list a case
    // reads is one entry per city in the room. False when the bridge has no room to fill.
    //
    // The cases need rooms of sizes the bridge never asked for: the count it hands to a local server
    // says how many players that server is to start with, and a joined room has a size of its own.
    // Reaching such a room through a server takes playing a match into it, so the state is written
    // straight into the mirror here, and the live path stays where it is tested (the case in
    // tst_gameclient.qml that reads the list off a room it filled through a server).
    Q_INVOKABLE bool fillRoom(QObject *bridge, int seats) const;

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

bool QMdmmBridgeDriver::fillRoom(QObject *bridge, int seats) const
{
    const auto *game = qobject_cast<const QMdmmGameClient *>(bridge);
    if (game == nullptr || seats < 1)
        return false;

    auto *client = game->findChild<QMdmmNetworking::Client *>();
    if (client == nullptr)
        return false;

    QMdmmCore::Room *room = client->room();
    if (room == nullptr)
        return false;

    for (int seat = 1; seat <= seats; ++seat) {
        // This side takes the first seat; the rest are named for this fixture only, since no server
        // is behind them to have given a name.
        const bool isThisSide = seat == 1;
        const QString name = isThisSide ? game->localName() : u"Seat %1"_s.arg(seat);
        QMdmmCore::Player *player = room->addPlayer(name);
        if (player == nullptr)
            return false;

        // Every seat is a city, and the seat a player holds is where it starts a round -- written
        // down for every player, so the mirror looks the same to a reading that takes the seats as
        // the players and to one that takes them as the highest seat number.
        player->setInitialPlace(seat);
        player->setPlace(isThisSide ? QMdmmCore::Data::Village : seat);
    }

    return true;
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
        , m_logWatch(new QMdmmLogWatch(this))
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

        // The case in tst_agentsignals.qml reads the agent's signal surface instead of driving
        // the client -- see QMdmmAgentInventory above for what it checks and why it lives here
        // rather than in a build step.
        engine->rootContext()->setContextProperty(u"agentInventory"_s, m_agentInventory);

        // What drives a bridge from the far end -- see QMdmmBridgeDriver above. The cases that
        // reach the upgrade request, the end of the match and the upgrade reply need it, because
        // the client those are wired to is private to the bridge and the agent is not otherwise
        // reachable from QML.
        engine->rootContext()->setContextProperty(u"bridgeDriver"_s, m_bridgeDriver);

        // What the program wrote to the log, for the cases in tst_gameclient.qml that hold the log to
        // the same rule as the screen about a connection that was lost: the one where a local game
        // comes up (nothing written there) and the one where a connection cannot be made (the loss
        // written once at least). That half of the rule had no reader in a test before this.
        engine->rootContext()->setContextProperty(u"logWatch"_s, m_logWatch);

        // The names a local game's ends have to agree on, for the case in tst_gameclient.qml
        // that compares them: the socket the bridge hands its own client and the bots it starts,
        // and the socket the server it starts listens on -- which is the server configuration's
        // own default, because the server is handed no name at all. No build step compares
        // them; that case is what holds them together, and a game played here finds out that they
        // parted by a room that never fills (see it for why it is worth saying earlier).
        engine->rootContext()->setContextProperty(u"bridgeSocketName"_s, QMdmmGameClient::serverSocketName());
        engine->rootContext()->setContextProperty(u"serverDefaultSocketName"_s, QMdmmNetworking::ServerConfiguration::defaults().localSocketName());

        // The stand-in bot the seat-watch case hands the bridge: a program that leaves with code 0
        // at once, which is what a bot's own exit looks like once the match it played is over (see
        // botstandin.cpp). It arrives resolved from the build system rather than looked up here, so
        // the case says nothing about where the build keeps its programs.
        engine->rootContext()->setContextProperty(u"standInBotProgram"_s, QString::fromUtf8(QMDMM_STANDIN_BOT));

        // The values behind the core enums the QML side names or compares: the punish rounding the
        // scene's rules strip spells out, the rock-paper-scissors pick its match log spells out,
        // and the kind of action that is a move -- the entry a case in tst_gameclient.qml picks out
        // of the action list to read which cities the room it is in has. The scene is handed the
        // punish rounding and the pick as plain numbers, so its branches only hold while their
        // numbering agrees with the core enum's; no build-time check holds the scene's numbering
        // against it. The case in tst_gamescene.qml is the other end of this: it feeds these in
        // and asks the scene for the name.
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
        coreEnumValues.insert(u"actionMove"_s, static_cast<int>(QMdmmCore::Data::Move));
        engine->rootContext()->setContextProperty(u"coreEnumValues"_s, coreEnumValues);

        // The names of the bridge's own state enum, read off its meta object here for the case in
        // tst_gameclient.qml. The enum is reachable at all because gameclient.h registers it
        // (Q_ENUM), and that registration is what QML would go through to name one of its states;
        // what reads those names is that case, which holds them against the names it writes down
        // itself -- a registration that went away would take that comparison down with it. Read
        // here rather than spelled out in the .qml file for the reason right above.
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
    QMdmmLogWatch *m_logWatch = nullptr;
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

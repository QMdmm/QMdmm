// SPDX-License-Identifier: AGPL-3.0-or-later

#include "gameclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QVariantMap>

#include <QMdmmAgent>
#include <QMdmmLogicConfiguration>

using namespace Qt::StringLiterals;

// Bridge between the QML GUI and the networking / core engine: owns the human
// Client, optionally an in-process Server plus a few auto-replying bot Clients (so
// a single user can fill a room and actually play a full match), and exposes a
// QML-friendly view of the synchronized Room model.

namespace {
constexpr char LOCAL_HOST[] = "qmdmm://localhost:6366";

// The two programs a local game runs as child processes, named the way the build tree and the
// packages lay them out. The number in each name is the Qt major version, as in the target
// names; the executable it points at may carry a version suffix of its own.
constexpr char SERVER_PROGRAM[] = "QMdmmServer6";
constexpr char BOT_PROGRAM[] = "QMdmmBot6";
} // namespace

QMdmmGameClient::QMdmmGameClient(QObject *parent)
    : QObject(parent)
{
}

QMdmmGameClient::~QMdmmGameClient()
{
    reset();
}

void QMdmmGameClient::reset()
{
    qDeleteAll(m_bots);
    m_bots.clear();

    delete m_human;
    m_human = nullptr;

    delete m_server;
    m_server = nullptr;

    m_room = nullptr;
    m_localName.clear();
    m_localScreen.clear();
    m_screenNames.clear();
    m_agentStates.clear();
    m_chat.clear();
    m_logicConfiguration.clear();

    emit agentStatesChanged();
    emit chatLogChanged();
    emit logicConfigurationChanged();
    emit playersChanged();
}

QVariantList QMdmmGameClient::players() const
{
    QVariantList ret;
    if (m_room == nullptr)
        return ret;
    const QList<QMdmmCore::Player *> ps = m_room->players();
    ret.reserve(ps.size());
    for (QMdmmCore::Player *p : ps)
        ret.append(QVariant::fromValue(static_cast<QObject *>(p)));
    return ret;
}

QString QMdmmGameClient::gameState() const
{
    switch (m_state) {
    case GameState::Start:
        return u"start"_s;
    case GameState::Lobby:
        return u"lobby"_s;
    case GameState::Playing:
        return u"playing"_s;
    case GameState::GameOver:
        return u"gameover"_s;
    }
    return u"start"_s;
}

QString QMdmmGameClient::localName() const
{
    return m_localName;
}

QVariantList QMdmmGameClient::chatLog() const
{
    return m_chat;
}

QVariantMap QMdmmGameClient::agentStates() const
{
    QVariantMap ret;
    for (QHash<QString, QMdmmCore::Data::AgentState>::const_iterator it = m_agentStates.constBegin(); it != m_agentStates.constEnd(); ++it)
        ret.insert(it.key(), static_cast<int>(it.value()));
    return ret;
}

QVariantMap QMdmmGameClient::logicConfiguration() const
{
    return m_logicConfiguration;
}

QString QMdmmGameClient::statusMessage() const
{
    return m_status;
}

int QMdmmGameClient::playerCount() const
{
    return m_playerCount;
}

void QMdmmGameClient::setPlayerCount(int n)
{
    n = qBound(1, n, 6);
    if (n == m_playerCount)
        return;
    m_playerCount = n;
    emit playerCountChanged();
}

QString QMdmmGameClient::serverProgram() const
{
    return m_serverProgram;
}

QString QMdmmGameClient::botProgram() const
{
    return m_botProgram;
}

void QMdmmGameClient::setProgramPaths(const QString &serverProgram, const QString &botProgram)
{
    const QString server = locateProgram(QString::fromLatin1(SERVER_PROGRAM), serverProgram);
    const QString bot = locateProgram(QString::fromLatin1(BOT_PROGRAM), botProgram);
    if (server == m_serverProgram && bot == m_botProgram)
        return;
    m_serverProgram = server;
    m_botProgram = bot;
    emit programPathsChanged();
}

QString QMdmmGameClient::locateProgram(const QString &programName, const QString &explicitPath)
{
    // A path given on the command line is the one the user asked for, whether or not there is
    // anything at it: reporting a path that leads nowhere is the job of whatever starts the
    // program.
    if (!explicitPath.isEmpty())
        return explicitPath;

    // Otherwise look next to this program. That is where both installed layouts keep the
    // three of them: the bundle carries the command line programs next to this one, and the
    // plain shape puts all three in one bin/. A build tree keeps them in one bin/ too, but on
    // macOS the bundle puts this program three levels down inside it, so the second candidate
    // covers that shape. The lookup is left to QStandardPaths rather than done by hand, so
    // that each name is tried with the suffix the platform runs and taken only if it is
    // executable.
    const QDir dir(QCoreApplication::applicationDirPath());
    const QDir above(dir.filePath(u"../../../"_s));
    const QStringList candidates {
        dir.absolutePath(),
        above.absolutePath(),
    };
    for (const QString &candidate : candidates) {
        const QString found = QStandardPaths::findExecutable(programName, {candidate});
        if (found.isEmpty())
            continue;
        // TODO: a program found this way, unlike one named on the command line, has not been
        // vouched for by the user; verify its signature before starting it.
        return found;
    }

    return {};
}

void QMdmmGameClient::setGameState(GameState s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit gameStateChanged();
}

void QMdmmGameClient::setStatusMessage(const QString &msg)
{
    if (msg == m_status)
        return;
    m_status = msg;
    emit statusMessageChanged(msg);
}

void QMdmmGameClient::setLogicConfiguration(const QMdmmCore::LogicConfiguration &conf)
{
    // Read the rules through the getters rather than off the stored object: a rule the server
    // left out is answered from LogicConfiguration::defaults(), which is the same answer the
    // engine plays by -- so the view can never disagree with the match.
    QVariantMap rules;
    rules.insert(u"initialKnifeDamage"_s, conf.initialKnifeDamage());
    rules.insert(u"maximumKnifeDamage"_s, conf.maximumKnifeDamage());
    rules.insert(u"initialHorseDamage"_s, conf.initialHorseDamage());
    rules.insert(u"maximumHorseDamage"_s, conf.maximumHorseDamage());
    rules.insert(u"initialMaxHp"_s, conf.initialMaxHp());
    rules.insert(u"maximumMaxHp"_s, conf.maximumMaxHp());
    rules.insert(u"punishHpModifier"_s, conf.punishHpModifier());
    rules.insert(u"punishHpRoundStrategy"_s, static_cast<int>(conf.punishHpRoundStrategy()));
    rules.insert(u"zeroHpAsDead"_s, conf.zeroHpAsDead());
    rules.insert(u"enableLetMove"_s, conf.enableLetMove());
    rules.insert(u"canBuyOnlyInInitialCity"_s, conf.canBuyOnlyInInitialCity());

    if (rules == m_logicConfiguration)
        return;

    m_logicConfiguration = rules;
    emit logicConfigurationChanged();
}

QMdmmCore::Player *QMdmmGameClient::localPlayer() const
{
    if (m_room == nullptr || m_localName.isEmpty())
        return nullptr;
    return m_room->player(m_localName);
}

QString QMdmmGameClient::screenName(const QString &playerName) const
{
    return m_screenNames.value(playerName, playerName);
}

bool QMdmmGameClient::isYou(const QString &playerName) const
{
    return playerName == m_localName;
}

QString QMdmmGameClient::placeName(int place) const
{
    if (place == QMdmmCore::Data::Village)
        return tr("Village");
    return tr("City %1").arg(place);
}

void QMdmmGameClient::wireClient(QMdmmNetworking::Client *client)
{
    // The client's own agent is the controller the operation side drives: incoming requests
    // arrive on it as xxxRequested signals, notifications as xxxNotified signals, and replies /
    // speech are sent back through the same agent.
    QMdmmNetworking::Agent *agent = client->agent();

    // request signals -> re-emit for QML, unless the player is managed and the request is given up
    // on its behalf instead (see requestIsForTheHuman)
    connect(agent, &QMdmmNetworking::Agent::rockPaperScissorsRequested, this, [this](const QStringList &playerNames, int strivedOrder) {
        if (requestIsForTheHuman())
            emit requestRockPaperScissors(playerNames, strivedOrder);
    });
    connect(agent, &QMdmmNetworking::Agent::actionOrderRequested, this, [this](const QList<int> &remainedOrders, int maximumOrder, int selectionNum) {
        if (requestIsForTheHuman())
            emit requestActionOrder(remainedOrders, maximumOrder, selectionNum);
    });
    connect(agent, &QMdmmNetworking::Agent::actionRequested, this, [this](int currentOrder) {
        if (requestIsForTheHuman())
            emit requestAction(currentOrder);
    });
    connect(agent, &QMdmmNetworking::Agent::upgradeRequested, this, [this](int remainingTimes) {
        if (requestIsForTheHuman())
            emit requestUpgrade(remainingTimes);
    });

    // notify signals -> re-emit (and keep the local view in sync)
    connect(agent, &QMdmmNetworking::Agent::logicConfigurationNotified, this, [this]() {
        if (m_room != nullptr)
            setLogicConfiguration(m_room->logicConfiguration());
    });
    // An agent's state changes on the wire: the managed toggle, a drop, a reconnect. The
    // player cards are the only place it can be read, so the map they read has to follow.
    connect(agent, &QMdmmNetworking::Agent::agentStateChangeNotified, this, [this](const QString &playerName, const QMdmmCore::Data::AgentState &agentState) {
        m_agentStates.insert(playerName, agentState);
        emit agentStatesChanged();
    });
    connect(agent, &QMdmmNetworking::Agent::playerAddNotified, this, [this](const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState) {
        m_screenNames.insert(playerName, screenName);
        m_agentStates.insert(playerName, agentState);
        emit agentStatesChanged();
        emit playerAdded(playerName, screenName, static_cast<int>(agentState));
        emit playersChanged();
    });
    connect(agent, &QMdmmNetworking::Agent::playerRemoveNotified, this, [this](const QString &playerName) {
        m_screenNames.remove(playerName);
        m_agentStates.remove(playerName);
        emit agentStatesChanged();
        emit playerRemoved(playerName);
        emit playersChanged();
    });
    connect(agent, &QMdmmNetworking::Agent::gameStartNotified, this, [this]() {
        setGameState(GameState::Playing);
        emit gameStart();
    });
    connect(agent, &QMdmmNetworking::Agent::roundStartNotified, this, [this]() { emit roundStart(); });
    connect(agent, &QMdmmNetworking::Agent::roundOverNotified, this, [this]() { emit roundOver(); });
    connect(agent, &QMdmmNetworking::Agent::rockPaperScissorsNotified, this, [this](const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies) {
        QVariantMap m;
        for (QHash<QString, QMdmmCore::Data::RockPaperScissors>::const_iterator it = replies.constBegin(); it != replies.constEnd(); ++it)
            m.insert(it.key(), static_cast<int>(it.value()));
        emit rpsResult(m);
    });
    connect(agent, &QMdmmNetworking::Agent::actionOrderNotified, this, [this](const QStringList &result) {
        QVariantMap m;
        for (int i = 0; i < result.size(); ++i)
            m.insert(QString::number(i + 1), result.at(i));
        emit actionOrderResult(m);
    });
    connect(agent, &QMdmmNetworking::Agent::actionNotified, this, [this](const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace) {
        emit actionResult(playerName, static_cast<int>(action), toPlayer, toPlace);
    });
    connect(agent, &QMdmmNetworking::Agent::upgradeNotified, this, [this](const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades) {
        QVariantMap m;
        for (QHash<QString, QList<QMdmmCore::Data::UpgradeItem>>::const_iterator it = upgrades.constBegin(); it != upgrades.constEnd(); ++it) {
            QVariantList l;
            l.reserve(it.value().size());
            for (QMdmmCore::Data::UpgradeItem u : it.value())
                l.append(static_cast<int>(u));
            m.insert(it.key(), l);
        }
        emit upgradeResult(m);
    });
    connect(agent, &QMdmmNetworking::Agent::gameOverNotified, this, [this](const QStringList &winners) {
        setGameState(GameState::GameOver);
        emit gameOver(winners);
    });
    connect(agent, &QMdmmNetworking::Agent::speakNotified, this, [this](const QString &playerName, const QString &content) {
        QVariantMap entry;
        entry.insert(u"name"_s, playerName);
        entry.insert(u"screen"_s, screenName(playerName));
        entry.insert(u"content"_s, content);
        m_chat.append(entry);
        emit chatLogChanged();
    });
    connect(client, &QMdmmNetworking::Client::socketErrorDisconnected, this, [this](const QString &errorString) {
        setStatusMessage(errorString);
        emit errorOccurred(errorString);
    });
}

void QMdmmGameClient::addBot(const QString &name)
{
    QMdmmNetworking::ClientConfiguration cfg;
    cfg.setScreenName(name);
    QMdmmNetworking::Client *bot = new QMdmmNetworking::Client(cfg, this);

    // Auto-reply: mirror the server's default-reply behavior so the room fills
    // and the match progresses without a human driving the bot. The bot's own
    // agent is the controller: requests arrive on its xxxRequested signals, and
    // replies are sent back through its bare-verb methods.
    QMdmmNetworking::Agent *botAgent = bot->agent();
    connect(botAgent, &QMdmmNetworking::Agent::rockPaperScissorsRequested, bot,
            [botAgent]() { botAgent->rockPaperScissors(static_cast<QMdmmCore::Data::RockPaperScissors>(QRandomGenerator::global()->generate() % 3)); });
    connect(botAgent, &QMdmmNetworking::Agent::actionOrderRequested, bot, [botAgent](const QList<int> &remainedOrders, int, int selectionNum) {
        QList<int> ao;
        ao.reserve(selectionNum);
        for (int i = 0; i < selectionNum && i < remainedOrders.size(); ++i)
            ao.append(remainedOrders.at(i));
        botAgent->actionOrder(ao);
    });
    connect(botAgent, &QMdmmNetworking::Agent::actionRequested, bot, [botAgent]() { botAgent->action(QMdmmCore::Data::DoNothing, {}, 0); });
    connect(botAgent, &QMdmmNetworking::Agent::upgradeRequested, bot, [botAgent](int remainingTimes) {
        QList<QMdmmCore::Data::UpgradeItem> ups;
        ups.reserve(remainingTimes);
        for (int i = 0; i < remainingTimes; ++i)
            ups.append(QMdmmCore::Data::UpgradeMaxHp);
        botAgent->upgrade(ups);
    });

    bot->connectToHost(QString::fromLatin1(LOCAL_HOST), QMdmmCore::Data::StateOnlineBot);
    m_bots.append(bot);
}

void QMdmmGameClient::startLocalGame(const QString &playerName)
{
    reset();

    // In-process server so a single user can actually play a full match.
    QMdmmNetworking::ServerConfiguration serverConf = QMdmmNetworking::ServerConfiguration::defaults();
    serverConf.setPlayerNumPerRoom(m_playerCount);
    m_server = new QMdmmNetworking::Server(serverConf, QMdmmCore::LogicConfiguration::defaults(), this);
    if (!m_server->listen()) {
        setStatusMessage(tr("Failed to start local server"));
        delete m_server;
        m_server = nullptr;
        return;
    }

    QMdmmNetworking::ClientConfiguration hc;
    hc.setScreenName(playerName.isEmpty() ? u"You"_s : playerName);
    m_human = new QMdmmNetworking::Client(hc, this);
    m_localName = m_human->objectName();
    m_localScreen = hc.screenName();
    m_room = m_human->room();
    wireClient(m_human);
    m_human->connectToHost(QString::fromLatin1(LOCAL_HOST), QMdmmCore::Data::StateOnline);

    for (int i = 1; i < m_playerCount; ++i)
        addBot(u"Bot %1"_s.arg(i));

    emit localNameChanged();
    setGameState(GameState::Lobby);
    setStatusMessage(tr("Connected to local server, waiting for other players..."));
}

void QMdmmGameClient::connectOnline(const QString &host, const QString &playerName)
{
    reset();

    QMdmmNetworking::ClientConfiguration hc;
    hc.setScreenName(playerName.isEmpty() ? u"You"_s : playerName);
    m_human = new QMdmmNetworking::Client(hc, this);
    m_localName = m_human->objectName();
    m_localScreen = hc.screenName();
    m_room = m_human->room();
    wireClient(m_human);

    // The address is handed to the client as it is: which transport it names is the networking
    // layer's call (SocketP::typeByConnectAddr), and an address with no scheme names a local
    // socket. Writing a scheme in here would be a second, competing reading of the same string --
    // and the one that turns a local socket name into a hostname to look up.
    m_human->connectToHost(host.trimmed(), QMdmmCore::Data::StateOnline);

    emit localNameChanged();
    setGameState(GameState::Lobby);
    setStatusMessage(tr("Connecting to server..."));
}

void QMdmmGameClient::disconnectAll()
{
    reset();
    setGameState(GameState::Start);
    setStatusMessage(tr("Disconnected"));
}

bool QMdmmGameClient::requestIsForTheHuman()
{
    // A managed (entrusted) player does not answer for itself: the request is given up on its
    // behalf, and the server answers it with the same default reply a timeout gets, so the match
    // keeps moving with nobody at this keyboard. The player still stays connected, and what comes
    // back -- the throw, the action, the upgrades the default reply picked -- is broadcast and
    // shown like anyone else's. Only the asking stops.
    //
    // Taken from the agent's own flag rather than waiting for the server's broadcast of it: the
    // declaration is what the player asked for, and a request that arrives before the broadcast
    // comes back is just as much one to give up on. See setManaged for the request that is
    // already in flight when the flag goes on.
    if (m_human != nullptr && m_human->agent()->managed()) {
        m_human->agent()->giveUpRequest();
        return false;
    }
    return true;
}

void QMdmmGameClient::replyRps(int rps)
{
    if (m_human != nullptr)
        m_human->agent()->rockPaperScissors(static_cast<QMdmmCore::Data::RockPaperScissors>(rps));
}

void QMdmmGameClient::replyActionOrder(const QVariantList &orders)
{
    if (m_human == nullptr)
        return;
    QList<int> ao;
    ao.reserve(orders.size());
    for (const QVariant &v : orders)
        ao.append(v.toInt());
    m_human->agent()->actionOrder(ao);
}

void QMdmmGameClient::yieldActionOrder(int selectionNum)
{
    // Yield the whole action-order negotiation: reply with a 0 sentinel for every
    // selection requested, telling the server to auto-assign whatever orders are
    // left. The reply must carry exactly `selectionNum` entries (one per selection),
    // and a 0 means "accept the leftover order and stop competing for it".
    if (m_human != nullptr && selectionNum > 0)
        m_human->agent()->actionOrder(QList<int>(selectionNum, 0));
}

void QMdmmGameClient::replyAction(int action, const QString &toPlayer, int toPlace)
{
    if (m_human != nullptr)
        m_human->agent()->action(static_cast<QMdmmCore::Data::Action>(action), toPlayer, toPlace);
}

void QMdmmGameClient::replyUpgrade(const QVariantList &items)
{
    if (m_human == nullptr)
        return;
    QList<QMdmmCore::Data::UpgradeItem> ups;
    ups.reserve(items.size());
    for (const QVariant &v : items)
        ups.append(static_cast<QMdmmCore::Data::UpgradeItem>(v.toInt()));
    m_human->agent()->upgrade(ups);
}

void QMdmmGameClient::speak(const QString &text)
{
    if (m_human != nullptr && !text.isEmpty())
        m_human->agent()->speak(text);
}

void QMdmmGameClient::setManaged(bool managed)
{
    // Declared, not flipped: the server owns the agent state, applies the flag and broadcasts the
    // result back, and that broadcast is what the player cards redraw from (see
    // Agent::setManaged). Nothing to declare while there is no client.
    if (m_human == nullptr)
        return;

    m_human->agent()->setManaged(managed);

    // Handing the player over takes in the request that is already in flight, not only the ones
    // after it: give up on it now, so turning the flag on never leaves the match waiting for a
    // decision that this side has stopped making. With nothing in flight the give-up is a no-op
    // (see ClientP::sendRequestGivenUp), and the view is told to take down whatever it shows --
    // an overlay still asking for a request that has just been answered would be asking for a
    // decision that is no longer open.
    if (managed) {
        m_human->agent()->giveUpRequest();
        emit requestWithdrawn();
    }
}

QVariantList QMdmmGameClient::actionListFor(const QMdmmCore::Player *from) const
{
    QVariantList ret;
    if (from == nullptr || m_room == nullptr)
        return ret;

    const auto make = [](QMdmmCore::Data::Action a, const QString &label, const QString &target, int place) {
        QVariantMap m;
        m.insert(u"action"_s, static_cast<int>(a));
        m.insert(u"label"_s, label);
        m.insert(u"target"_s, target);
        m.insert(u"place"_s, place);
        return m;
    };

    if (from->alive())
        ret.append(make(QMdmmCore::Data::DoNothing, tr("Do nothing / rest"), QString(), -1));
    if (from->canBuyKnife())
        ret.append(make(QMdmmCore::Data::BuyKnife, tr("Buy knife"), QString(), -1));
    if (from->canBuyHorse())
        ret.append(make(QMdmmCore::Data::BuyHorse, tr("Buy horse"), QString(), -1));

    const int here = from->place();
    // Move to any adjacent place (Village <-> one city).
    for (int to = 0; to <= m_playerCount; ++to) {
        if (to == here)
            continue;
        if (QMdmmCore::Data::isPlaceAdjacent(here, to) && from->canMove(to))
            ret.append(make(QMdmmCore::Data::Move, tr("Move to %1").arg(placeName(to)), QString(), to));
    }

    for (const QMdmmCore::Player *other : m_room->players()) {
        if (other == from || !other->alive())
            continue;
        const QString screen = screenName(other->objectName());
        if (from->canSlash(other))
            ret.append(make(QMdmmCore::Data::Slash, tr("Slash %1").arg(screen), other->objectName(), -1));
        if (from->canKick(other))
            ret.append(make(QMdmmCore::Data::Kick, tr("Kick %1").arg(screen), other->objectName(), -1));
        for (int to = 0; to <= m_playerCount; ++to) {
            if (to == other->place())
                continue;
            if (QMdmmCore::Data::isPlaceAdjacent(other->place(), to) && from->canLetMove(other, to))
                ret.append(make(QMdmmCore::Data::LetMove, tr("Move %1 to %2").arg(screen, placeName(to)), other->objectName(), to));
        }
    }
    return ret;
}

QVariantList QMdmmGameClient::getActionOptions() const
{
    return actionListFor(localPlayer());
}

QVariantList QMdmmGameClient::getUpgradeOptions() const
{
    QVariantList ret;
    const QMdmmCore::Player *p = localPlayer();
    if (p == nullptr)
        return ret;

    auto add = [&](QMdmmCore::Data::UpgradeItem item, const QString &label) {
        QVariantMap m;
        m.insert(u"item"_s, static_cast<int>(item));
        m.insert(u"label"_s, label);
        ret.append(m);
    };
    if (p->canUpgradeKnife())
        add(QMdmmCore::Data::UpgradeKnife, tr("Upgrade knife damage"));
    if (p->canUpgradeHorse())
        add(QMdmmCore::Data::UpgradeHorse, tr("Upgrade horse damage"));
    if (p->canUpgradeMaxHp())
        add(QMdmmCore::Data::UpgradeMaxHp, tr("Upgrade max HP"));
    return ret;
}

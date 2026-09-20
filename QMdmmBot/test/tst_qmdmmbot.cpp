// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QMdmmAgent>
#include <QMdmmClient>
#include <QMdmmData>
#include <QMdmmPlayer>
#include <QMdmmRoom>
#include <QMdmmServer>

#include <QEventLoop>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>

#include <memory>
#include <vector>

#include "bot.h"

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

// The state under test (the revenge memory, the threat assessment, the target
// selection built on them) lives in the Bot base class, but its read accessors
// are protected and the four request handlers are pure virtual (a style
// subclass must implement its strategy there). This subclass only lifts the
// read accessors into public scope and supplies the minimal concrete request
// handlers; it deliberately leaves the notification side untouched, so the base
// class implementations are the ones under test here. The notifications are
// delivered through the public Agent API, which also pins the signal
// connections Bot makes in its constructor.
class ProbeBot final : public Bot
{
public:
    explicit ProbeBot(QMdmmNetworking::Client *parent)
        : Bot(parent)
    {
    }

    using Bot::canSlashSafely;
    using Bot::logicConfiguration;
    using Bot::revengeScore;
    using Bot::selectTarget;
    using Bot::targetScore;
    using Bot::threatScore;

protected:
    void handleRockPaperScissorsRequest(const QStringList &playerNames, int strivedOrder) override
    {
        Q_UNUSED(playerNames);
        Q_UNUSED(strivedOrder);
    }

    void handleActionOrderRequest(const QList<int> &remainedOrders, int maximumOrder, int selectionNum) override
    {
        Q_UNUSED(remainedOrders);
        Q_UNUSED(maximumOrder);
        Q_UNUSED(selectionNum);
    }

    void handleActionRequest(int currentOrder) override
    {
        Q_UNUSED(currentOrder);
    }

    void handleUpgradeRequest(int remainingTimes) override
    {
        Q_UNUSED(remainingTimes);
    }
};

// The other half of the notification contract: the entry points the Agent
// reaches are not virtual, so a style subclass tracks the match by overriding
// the onXxxNotified() hooks instead, and the state every bot shares is kept for
// it. This subclass is exactly that shape -- it records what it is told, which
// also pins that the hooks are the half a subclass gets.
class HookProbeBot final : public Bot
{
public:
    explicit HookProbeBot(QMdmmNetworking::Client *parent)
        : Bot(parent)
    {
    }

    using Bot::revengeScore;

    int logicConfigurationSeen = 0;
    int roundStartSeen = 0;
    int actionsSeen = 0;
    int upgradesSeen = 0;
    int roundsSeen = 0;
    int gameOverSeen = 0;

    QString lastActionPlayer;
    QMdmmCore::Data::Action lastAction = QMdmmCore::Data::DoNothing;

protected:
    void handleRockPaperScissorsRequest(const QStringList &playerNames, int strivedOrder) override
    {
        Q_UNUSED(playerNames);
        Q_UNUSED(strivedOrder);
    }

    void handleActionOrderRequest(const QList<int> &remainedOrders, int maximumOrder, int selectionNum) override
    {
        Q_UNUSED(remainedOrders);
        Q_UNUSED(maximumOrder);
        Q_UNUSED(selectionNum);
    }

    void handleActionRequest(int currentOrder) override
    {
        Q_UNUSED(currentOrder);
    }

    void handleUpgradeRequest(int remainingTimes) override
    {
        Q_UNUSED(remainingTimes);
    }

    void onLogicConfigurationNotified() override
    {
        ++logicConfigurationSeen;
    }

    void onRoundStartNotified() override
    {
        ++roundStartSeen;
    }

    void onActionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace) override
    {
        ++actionsSeen;
        lastActionPlayer = playerName;
        lastAction = action;
        Q_UNUSED(toPlayer);
        Q_UNUSED(toPlace);
    }

    void onUpgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades) override
    {
        ++upgradesSeen;
        Q_UNUSED(upgrades);
    }

    void onRoundOverNotified() override
    {
        ++roundsSeen;
    }

    void onGameOverNotified(const QStringList &playerNames) override
    {
        ++gameOverSeen;
        Q_UNUSED(playerNames);
    }
};

namespace {

// What a bot answered to one action request.
struct ActionReply
{
    int count = 0;
    QMdmmCore::Data::Action action = QMdmmCore::Data::DoNothing;
    QString toPlayer;
    int toPlace = 0;
};

// Asks a bot for an action the way the match does: the server's request reaches
// the client, which hands it to the agent, which raises it as a signal -- the
// same signal the bot answered above. What comes back out is the reply signal,
// recorded here together with the player and place it names.
ActionReply askForAction(QMdmmNetworking::Client &client, int currentOrder)
{
    ActionReply reply;
    const auto record = [&reply](QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace) {
        ++reply.count;
        reply.action = action;
        reply.toPlayer = toPlayer;
        reply.toPlace = toPlace;
    };
    const QMetaObject::Connection connection = QObject::connect(client.agent(), &QMdmmNetworking::Agent::replyAction, &client, record);
    client.agent()->requestAction(currentOrder);
    QObject::disconnect(connection);
    return reply;
}

// What a bot asked for in one upgrade request.
struct UpgradeReply
{
    int count = 0;
    QList<QMdmmCore::Data::UpgradeItem> items;
};

// Asks a bot for an upgrade the way the match does (see askForAction()).
UpgradeReply askForUpgrade(QMdmmNetworking::Client &client, int remainingTimes)
{
    UpgradeReply reply;
    const auto record = [&reply](const QList<QMdmmCore::Data::UpgradeItem> &items) {
        ++reply.count;
        reply.items = items;
    };
    const QMetaObject::Connection connection = QObject::connect(client.agent(), &QMdmmNetworking::Agent::replyUpgrade, &client, record);
    client.agent()->requestUpgrade(remainingTimes);
    QObject::disconnect(connection);
    return reply;
}

// Whether a bot gave up on one upgrade request rather than answering it. The
// reply-or-giveUp contract allows either, but never neither.
bool upgradeWasGivenUp(QMdmmNetworking::Client &client, int remainingTimes)
{
    bool givenUp = false;
    const QMetaObject::Connection connection = QObject::connect(client.agent(), &QMdmmNetworking::Agent::requestGivenUp, &client, [&givenUp] { givenUp = true; });
    client.agent()->requestUpgrade(remainingTimes);
    QObject::disconnect(connection);
    return givenUp;
}

// What a bot answered to one Rock-Paper-Scissors request.
struct ThrowReply
{
    int count = 0;
    QMdmmCore::Data::RockPaperScissors answer = QMdmmCore::Data::Rock;
};

// Asks a bot for a throw the way the match does (see askForAction()). The two
// arguments are what the request carries -- the peers still in the running for
// the order, and the order this bot would like -- and neither is anything a
// fallback answer reads.
ThrowReply askForThrow(QMdmmNetworking::Client &client)
{
    ThrowReply reply;
    const auto record = [&reply](QMdmmCore::Data::RockPaperScissors rps) {
        ++reply.count;
        reply.answer = rps;
    };
    const QMetaObject::Connection connection = QObject::connect(client.agent(), &QMdmmNetworking::Agent::replyRockPaperScissors, &client, record);
    client.agent()->requestRockPaperScissors(QStringList(), 0);
    QObject::disconnect(connection);
    return reply;
}

// What a bot answered to one action-order request.
struct ActionOrderReply
{
    int count = 0;
    QList<int> order;
};

// Asks a bot to pick from the orders still up for grabbing, the way the match
// does (see askForAction()). The arguments are what the request carries: the
// orders on offer and how many of them the server wants chosen.
ActionOrderReply askForActionOrder(QMdmmNetworking::Client &client, const QList<int> &remainedOrders, int selectionNum)
{
    ActionOrderReply reply;
    const auto record = [&reply](const QList<int> &order) {
        ++reply.count;
        reply.order = order;
    };
    const QMetaObject::Connection connection = QObject::connect(client.agent(), &QMdmmNetworking::Agent::replyActionOrder, &client, record);
    client.agent()->requestActionOrder(remainedOrders, int(remainedOrders.size()), selectionNum);
    QObject::disconnect(connection);
    return reply;
}

// Lays one player out the way the action-order cases need it: standing in a place
// of its own, at full HP, and with no weapon in hand. Places are told apart by
// comparing them, so any three distinct values stand for two cities and the
// Village.
void placePlayer(QMdmmCore::Player *player, int place, int hp)
{
    player->setPlace(place);
    player->setMaxHp(hp);
    player->setHp(hp);
}

// The same, holding a knife of a stated damage.
void placeArmedPlayer(QMdmmCore::Player *player, int place, int hp, int knifeDamage)
{
    placePlayer(player, place, hp);
    player->setKnifeDamage(knifeDamage);
    player->setHasKnife(true);
}

// The same, in the saddle instead: a horse of a stated damage and no knife. The
// two are laid out apart because the blows are counted apart -- a slash and a kick
// are two different ways to finish somebody, and a case that means to exercise one
// of them must not leave the other lying around.
void placeMountedPlayer(QMdmmCore::Player *player, int place, int hp, int horseDamage)
{
    placePlayer(player, place, hp);
    player->setHorseDamage(horseDamage);
    player->setHasHorse(true);
}

// What one bot's requests came to over a whole match: how many were put to it,
// and how many it answered -- a reply or a give-up, the two the contract allows
// and the only two that keep the server from timing the bot out.
struct ReplyTally
{
    int requests = 0;
    int replies = 0;
    int giveUps = 0;

    [[nodiscard]] int answers() const
    {
        return replies + giveUps;
    }
};

// Counts every request that reaches a bot and every answer it sends back, over
// all four request kinds. Both are the wire-level Agent signals, so what is
// counted is what the server sees: the requests it put to this bot, and the
// answers this bot sent it.
void tallyReplies(QMdmmNetworking::Client &client, ReplyTally &tally)
{
    QMdmmNetworking::Agent *agent = client.agent();
    const auto request = [&tally] { ++tally.requests; };
    const auto reply = [&tally] { ++tally.replies; };
    const auto giveUp = [&tally] { ++tally.giveUps; };

    QObject::connect(agent, &QMdmmNetworking::Agent::rockPaperScissorsRequested, &client, request);
    QObject::connect(agent, &QMdmmNetworking::Agent::actionOrderRequested, &client, request);
    QObject::connect(agent, &QMdmmNetworking::Agent::actionRequested, &client, request);
    QObject::connect(agent, &QMdmmNetworking::Agent::upgradeRequested, &client, request);

    QObject::connect(agent, &QMdmmNetworking::Agent::replyRockPaperScissors, &client, reply);
    QObject::connect(agent, &QMdmmNetworking::Agent::replyActionOrder, &client, reply);
    QObject::connect(agent, &QMdmmNetworking::Agent::replyAction, &client, reply);
    QObject::connect(agent, &QMdmmNetworking::Agent::replyUpgrade, &client, reply);

    QObject::connect(agent, &QMdmmNetworking::Agent::requestGivenUp, &client, giveUp);
}

// One seat in the whole-match case: a client playing the match, the style Bot
// attached to it -- the same pairing the shipped QMdmmBot program builds, a Bot
// whose parent is its client -- and what its bot was asked and answered over the
// match.
struct Seat
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = nullptr;
    ReplyTally tally;
};

// The loopback endpoints the whole-match case's server listens on, one per
// transport. They are fixed because the server offers no way to read back a port
// it picked itself, and they are deliberately not the ones the smoke test's own
// server binds: that server takes the configuration defaults (TCP 6366, a
// websocket on 6367 and a local socket called "QMdmm"), and both servers run on
// the same machine during a test run.
constexpr quint16 MATCH_PORT = 6368;
constexpr quint16 MATCH_WEBSOCKET_PORT = 6369;
const QString MATCH_LOCAL_SOCKET_NAME = u"QMdmmBotTest"_s;

// The whole-match case's deadline, well past what the match below takes in
// practice: it is there so a match that stalls fails the case instead of hanging
// the test run, not to bound a healthy one.
constexpr int MATCH_TIMEOUT_MS = 30000;

} // namespace

class tst_QMdmmBot : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmBot() = default;

private slots:
    // A grudge is earned by a hostile action aimed at this bot, and only then.
    void revenge_recordsHostileActionsOnly();

    // A grudge fades every finished round but outlives the round it was earned
    // in, and keeps accumulating across rounds.
    void revenge_decaysEveryRound();

    // Once a grudge has faded to nothing the entry is dropped, so the table
    // stays bounded over a long match.
    void revenge_dropsNegligibleEntries();

    // A style subclass tracks the match through the notification hooks, and
    // overriding one cannot cost it the state every bot keeps: the Agent reaches
    // a non-virtual entry point, which updates that state and then calls the
    // hook. The hooks are told about every broadcast, not only the ones the
    // shared state has an opinion on.
    void notify_overridingAHookKeepsTheSharedState();

    // The threat one opponent poses is the damage of its weapons, discounted by
    // how far away it stands.
    void threat_sumsWeaponsDiscountedByDistance();

    // A peer that cannot hurt this bot -- because it is dead, or because it is
    // not in the room at all -- is no threat, and neither is a dead bot itself.
    void threat_ignoresDeadPlayersAndStrangers();

    // How attractive a peer is as a target is its grudge plus the threat it
    // poses; death zeroes the threat but not the grudge.
    void target_combinesRevengeAndThreat();

    // The target is the opponent with the highest score, on equal weight for the
    // two dimensions, and a tie goes to room order.
    void target_picksTheHighestScoringOpponent();

    // Nobody scores above zero -- or nobody alive is left to act against -- means
    // no target at all.
    void target_returnsEmptyWhenNobodyIsWorthAimingAt();

    // Where a slash happens decides what it costs: a city charges the slasher its
    // own HP, the Village charges nothing, and the rules decide whether there is a
    // punish at all.
    void punish_isChargedInCitiesAndNotInTheVillage();

    // A slash is skipped when its punish would finish the slasher off, and taken
    // when it would not.
    void slash_isSkippedWhenItsPunishWouldBeFatal();

    // The styles answer an action request with that rule in force: they pass up a
    // co-located attack that a city would punish them to death for.
    void action_skipsASlashThatTheCityPunishWouldMakeFatal();

    // The knife style spends its points on the knife first, on max HP second,
    // and leaves the horse for last (issue #6 Q3).
    void upgrade_spendsOnKnifeThenMaxHpThenHorse();

    // The knife style stops buying horses once two slashes finish every peer
    // off: what it is short of past that point is staying power, not reach.
    void action_buysAHorseOnlyWhileItIsStillShortOfReach();

    // The knife style pays for a slash with its life only in the one case Q3
    // names: a blow that finishes a peer off, thrown from the stronger side,
    // with no third peer left to profit from the round it dies in.
    void action_paysForASlashWithItsLifeOnlyWhenTheTradeIsWorthIt();

    // The blow goes to the co-located peer the score rates highest -- the score
    // ranks targets, it does not forbid hitting them. Both styles ask this
    // through the same base-class rule, so both are asked here.
    void action_aimsAtTheBestScoringPeerStandingHere();

    // The horse style spends its points on the horse first, on the knife second,
    // and leaves max HP for last (issue #6 Q4).
    void upgrade_spendsOnHorseThenKnifeThenMaxHp();

    // Bare at the start of a round, the horse style buys its horse before its
    // knife -- and buys the knife too, only later (issue #6 Q4).
    void action_buysTheHorseBeforeTheKnife();

    // A kick costs nothing where a city slash costs HP, so the horse style kicks
    // a peer standing with it in a city; where a kick is forbidden -- the Village
    // -- it slashes it instead (issue #6 C3).
    void action_kicksWhatStandsWithItForFree();

    // The pull half of the pull-kick loop: a peer the score rates, standing in
    // the Village, is dragged into the city this bot stands in -- unless the
    // rules forbid dragging, or nobody rates the peer, and then the round goes to
    // closing in on it instead (issue #6 C3, and Q4 for the rules).
    void action_pullsARatedPeerIntoItsCity();

    // Neither implemented style answers the same throw every time. Answering one
    // fixed throw is what makes two bots tie on every Rock-Paper-Scissors and
    // never get past the first action time of a round (the D1 backlog item), so
    // both styles are asked here, and each one's answers are collected until they
    // vary.
    void rps_doesNotAlwaysAnswerTheSameThrow();

    // Both styles answer an action-order request with the orders it was offered,
    // and take no more of them than the request asked for -- whatever the request
    // says, nothing is read past the end of the offered list.
    void actionOrder_takesOnlyTheOrdersItWasOffered();

    // The pick itself, laid out on three peers in three places: a round in which
    // no blow can land on anybody is the one where waiting costs nothing, so the
    // latest order on offer is the one both styles ask for.
    void actionOrder_holdsBackWhenNoBlowCanBeFatal();

    // And the rounds in which waiting does cost something, so the earliest order
    // is the one worth asking for: a peer standing here whose knife would finish
    // this bot off, a peer standing here that this bot's own knife would finish,
    // and a pair of peers able to finish each other while this bot stands aside.
    void actionOrder_grabsTheEarliestOrderWhenItsOwnLifeIsOnTheLine();
    void actionOrder_grabsTheEarliestOrderWhenAKillIsOnTheTable();
    void actionOrder_grabsTheEarliestOrderWhenAPeerCouldBeFinished();

    // The same three rounds once more, with the horse as the only weapon on the
    // field. A kick is the second way a blow lands -- a different weapon, armed on
    // its own -- so the field has to be read off both of them; read off the knife
    // alone, a round whose only lethal blow is a kick looks harmless and the bot
    // waits in it.
    void actionOrder_grabsTheEarliestOrderWhenAPeersKickWouldFinishSelf();
    void actionOrder_grabsTheEarliestOrderWhenItsOwnKickWouldFinishAPeer();
    void actionOrder_grabsTheEarliestOrderWhenAPeersKickCouldFinishSomebody();

    // Where the death threshold lies is a rule of the match, so the same layout
    // answers differently under the two rules: one blow that leaves a peer on
    // exactly zero HP is fatal where zero counts as dead, and is not where it does
    // not.
    void actionOrder_readsTheDeathThresholdOffTheMatchRules();

    // The whole match, played by the real styles against the real server, in
    // this process: the seats are drawn from the two implemented styles, and the
    // case is that the match runs its own loop to the end without a bot stalling
    // or the server dropping one. It is run once per transport, so the same
    // match also covers the three ways a player can reach the server.
    void fullGame_theTwoStylesPlayAWholeMatchToTheEnd();
    void fullGame_theTwoStylesPlayAWholeMatchToTheEnd_data();
};

void tst_QMdmmBot::revenge_recordsHostileActionsOnly()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    // The client's objectName is its playerName, i.e. the name this bot is
    // known by (see Bot::selfPlayer()).
    const QString self = client.objectName();
    const QString attacker = u"attacker"_s;
    const QString bystander = u"bystander"_s;

    // Slash and Kick aimed at us are hostile, and every hit piles up.
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.revengeScore(attacker), 1.0);
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Kick, self, 0);
    QCOMPARE(bot.revengeScore(attacker), 2.0);

    // Every attacker gets its own entry.
    client.agent()->notifyAction(bystander, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.revengeScore(bystander), 1.0);
    QCOMPARE(bot.revengeScore(attacker), 2.0);

    // LetMove is not hostile (it moves a player but deals no damage), and a
    // hostile action aimed at somebody else is none of our business either.
    client.agent()->notifyAction(attacker, QMdmmCore::Data::LetMove, self, 0);
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, bystander, 0);
    client.agent()->notifyAction(attacker, QMdmmCore::Data::DoNothing, QString(), 0);
    QCOMPARE(bot.revengeScore(attacker), 2.0);

    // A peer that never attacked this bot holds no grudge at all.
    QCOMPARE(bot.revengeScore(u"innocent"_s), 0.0);
}

void tst_QMdmmBot::revenge_decaysEveryRound()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    const QString self = client.objectName();
    const QString attacker = u"attacker"_s;

    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, self, 0);
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.revengeScore(attacker), 2.0);

    // Every finished round multiplies the grudge by the decay factor, and one
    // round is never enough to forget an attacker.
    const double earned = bot.revengeScore(attacker);
    client.agent()->notifyRoundOver();
    QVERIFY(qFuzzyCompare(bot.revengeScore(attacker), earned * 0.8));
    QVERIFY(bot.revengeScore(attacker) > 0.0);

    const double afterFirstRound = bot.revengeScore(attacker);
    client.agent()->notifyRoundOver();
    QVERIFY(qFuzzyCompare(bot.revengeScore(attacker), afterFirstRound * 0.8));

    // A grudge outlives its round: a new hit adds to what is left over.
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, self, 0);
    QVERIFY(qFuzzyCompare(bot.revengeScore(attacker), afterFirstRound * 0.8 + 1.0));

    // Rounds passing do not conjure up entries for peers that never attacked.
    client.agent()->notifyRoundOver();
    QCOMPARE(bot.revengeScore(u"innocent"_s), 0.0);
}

void tst_QMdmmBot::revenge_dropsNegligibleEntries()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    const QString self = client.objectName();
    const QString attacker = u"attacker"_s;

    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, self, 0);

    // 20 rounds leave 0.8^20 = 0.0115..., still above the drop threshold, so
    // the grudge is expected to survive them.
    for (int round = 0; round < 20; ++round)
        client.agent()->notifyRoundOver();
    QVERIFY(bot.revengeScore(attacker) > 0.0);

    // One more round pushes it to 0.8^21 = 0.0092..., at or below the
    // threshold, and the entry is dropped.
    client.agent()->notifyRoundOver();
    QCOMPARE(bot.revengeScore(attacker), 0.0);

    // A dropped entry starts over rather than leaving a residue behind.
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.revengeScore(attacker), 1.0);
}

void tst_QMdmmBot::notify_overridingAHookKeepsTheSharedState()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    HookProbeBot bot {&client};

    const QString self = client.objectName();
    const QString attacker = u"attacker"_s;

    // Every broadcast reaches its hook -- that is the half of the notification
    // path a style subclass overrides to track the match.
    client.agent()->notifyLogicConfiguration();
    client.agent()->notifyRoundStart();
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, self, 0);
    client.agent()->notifyUpgrade({});
    client.agent()->notifyRoundOver();
    client.agent()->notifyGameOver({attacker});

    QCOMPARE(bot.logicConfigurationSeen, 1);
    QCOMPARE(bot.roundStartSeen, 1);
    QCOMPARE(bot.actionsSeen, 1);
    QCOMPARE(bot.upgradesSeen, 1);
    QCOMPARE(bot.roundsSeen, 1);
    QCOMPARE(bot.gameOverSeen, 1);
    QCOMPARE(bot.lastActionPlayer, attacker);
    QCOMPARE(bot.lastAction, QMdmmCore::Data::Slash);

    // The hook is told and the shared state is kept in the same delivery: the
    // grudge was recorded before the hook ran, and the round that then finished
    // faded it. Overriding a hook loses neither.
    QVERIFY(qFuzzyCompare(bot.revengeScore(attacker), 0.8));

    // The hooks are told about the broadcasts the shared state has no opinion
    // on, too -- the entry points filter their own bookkeeping, not what a style
    // sees: a hit aimed at somebody else still reaches the hook, and still
    // earns no grudge.
    client.agent()->notifyAction(attacker, QMdmmCore::Data::Slash, u"somebodyElse"_s, 0);
    QCOMPARE(bot.actionsSeen, 2);
    QVERIFY(qFuzzyCompare(bot.revengeScore(attacker), 0.8));
}

void tst_QMdmmBot::threat_sumsWeaponsDiscountedByDistance()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    // The threat is read off the room mirror, so the room has to hold both this
    // bot (under the client's objectName, see Bot::selfPlayer()) and the peer.
    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    enemy->setKnifeDamage(3);
    enemy->setHorseDamage(2);

    // Unarmed, an opponent is harmless however close it stands.
    self->setPlace(1);
    enemy->setPlace(1);
    QCOMPARE(bot.threatScore(u"enemy"_s), 0.0);

    // Sharing this bot's place, an opponent brings both weapons to bear...
    enemy->setHasKnife(true);
    QCOMPARE(bot.threatScore(u"enemy"_s), 3.0);
    enemy->setHasHorse(true);
    QCOMPARE(bot.threatScore(u"enemy"_s), 5.0);

    // ...except inside the Village, where a horse cannot be used.
    self->setPlace(QMdmmCore::Data::Village);
    enemy->setPlace(QMdmmCore::Data::Village);
    QCOMPARE(bot.threatScore(u"enemy"_s), 3.0);

    // A merely adjacent opponent has to move in first, so its hit lands a round
    // later and counts for half. It lands where this bot stands: stepping into a
    // City it can still kick,
    self->setPlace(1);
    enemy->setPlace(QMdmmCore::Data::Village);
    QCOMPARE(bot.threatScore(u"enemy"_s), 2.5);

    // but stepping into the Village it cannot.
    self->setPlace(QMdmmCore::Data::Village);
    enemy->setPlace(1);
    QCOMPARE(bot.threatScore(u"enemy"_s), 1.5);

    // Two Cities are not adjacent, so an opponent standing in one of them
    // cannot reach this bot within a round.
    self->setPlace(1);
    enemy->setPlace(2);
    QCOMPARE(bot.threatScore(u"enemy"_s), 0.0);
}

void tst_QMdmmBot::threat_ignoresDeadPlayersAndStrangers()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    self->setPlace(1);
    enemy->setPlace(1);
    enemy->setHasKnife(true);
    enemy->setKnifeDamage(3);
    QCOMPARE(bot.threatScore(u"enemy"_s), 3.0);

    // A dead opponent is harmless even at arm's length.
    enemy->setHp(0);
    QCOMPARE(bot.threatScore(u"enemy"_s), 0.0);

    // So is a peer that is not in the room at all.
    QCOMPARE(bot.threatScore(u"stranger"_s), 0.0);

    // And a bot that is itself dead is threatened by nobody.
    enemy->setHp(10);
    self->setHp(0);
    QCOMPARE(bot.threatScore(u"enemy"_s), 0.0);
}

void tst_QMdmmBot::target_combinesRevengeAndThreat()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    const QString self = client.objectName();
    const QString enemy = u"enemy"_s;

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *selfPlayer = room->addPlayer(self);
    QMdmmCore::Player *enemyPlayer = room->addPlayer(enemy);
    QVERIFY(selfPlayer != nullptr);
    QVERIFY(enemyPlayer != nullptr);

    selfPlayer->setPlace(1);
    enemyPlayer->setPlace(1);

    // An unarmed peer that never attacked this bot is worth nothing as a target,
    // and so is a peer that is not in the room at all.
    QCOMPARE(bot.targetScore(enemy), 0.0);
    QCOMPARE(bot.targetScore(u"stranger"_s), 0.0);

    // The threat it poses is what it brings to bear...
    enemyPlayer->setKnifeDamage(3);
    enemyPlayer->setHasKnife(true);
    QCOMPARE(bot.targetScore(enemy), 3.0);

    // ...and a grudge adds on top of that.
    client.agent()->notifyAction(enemy, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.revengeScore(enemy), 1.0);
    QCOMPARE(bot.targetScore(enemy), 4.0);

    // Death zeroes the threat but not the grudge: the bot still remembers who
    // wronged it.
    enemyPlayer->setHp(0);
    QCOMPARE(bot.threatScore(enemy), 0.0);
    QCOMPARE(bot.targetScore(enemy), 1.0);
}

void tst_QMdmmBot::target_picksTheHighestScoringOpponent()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    const QString self = client.objectName();

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *selfPlayer = room->addPlayer(self);
    // Room order is name order, which is what decides a tie: "bbb" comes before
    // "ccc".
    QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
    QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
    QVERIFY(selfPlayer != nullptr);
    QVERIFY(bbb != nullptr);
    QVERIFY(ccc != nullptr);

    selfPlayer->setPlace(1);
    bbb->setPlace(1);
    ccc->setPlace(1);

    // Nobody has wronged this bot and nobody is armed, so there is nothing to
    // pick.
    QCOMPARE(bot.selectTarget(), QString());

    // "ccc" attacked us, which alone puts it ahead of the harmless "bbb".
    client.agent()->notifyAction(u"ccc"_s, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.selectTarget(), u"ccc"_s);

    // A knife, however, makes "bbb" the bigger threat and hands it the lead.
    bbb->setKnifeDamage(3);
    bbb->setHasKnife(true);
    QCOMPARE(bot.selectTarget(), u"bbb"_s);

    // Both dimensions are summed at equal weight, so two more hits put the
    // grudge against "ccc" at exactly the threat of "bbb" -- and a tie goes to
    // room order, i.e. to "bbb".
    client.agent()->notifyAction(u"ccc"_s, QMdmmCore::Data::Slash, self, 0);
    client.agent()->notifyAction(u"ccc"_s, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.targetScore(u"bbb"_s), 3.0);
    QCOMPARE(bot.targetScore(u"ccc"_s), 3.0);
    QCOMPARE(bot.selectTarget(), u"bbb"_s);

    // One more hit and "ccc" pulls ahead on its own.
    client.agent()->notifyAction(u"ccc"_s, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.targetScore(u"ccc"_s), 4.0);
    QCOMPARE(bot.selectTarget(), u"ccc"_s);
}

void tst_QMdmmBot::target_returnsEmptyWhenNobodyIsWorthAimingAt()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    const QString self = client.objectName();
    const QString enemy = u"enemy"_s;

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *selfPlayer = room->addPlayer(self);
    QMdmmCore::Player *enemyPlayer = room->addPlayer(enemy);
    QVERIFY(selfPlayer != nullptr);
    QVERIFY(enemyPlayer != nullptr);

    // A peer that is out of reach, unarmed and holds no grudge is not worth
    // aiming at.
    selfPlayer->setPlace(1);
    enemyPlayer->setPlace(2);
    QCOMPARE(bot.targetScore(enemy), 0.0);
    QCOMPARE(bot.selectTarget(), QString());

    // Once it has wronged this bot there is something to act on...
    client.agent()->notifyAction(enemy, QMdmmCore::Data::Slash, self, 0);
    QCOMPARE(bot.selectTarget(), enemy);

    // ...but a dead peer is out of the running even while the grudge survives:
    // only the living can be acted against.
    enemyPlayer->setHp(0);
    QCOMPARE(bot.targetScore(enemy), 1.0);
    QCOMPARE(bot.selectTarget(), QString());

    // A bot whose room holds nobody but itself has no target either.
    QMdmmNetworking::Client lonelyClient {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot lonelyBot {&lonelyClient};
    QMdmmCore::Room *lonelyRoom = lonelyClient.room();
    QVERIFY(lonelyRoom->addPlayer(lonelyClient.objectName()) != nullptr);
    QCOMPARE(lonelyBot.selectTarget(), QString());
}

void tst_QMdmmBot::punish_isChargedInCitiesAndNotInTheVillage()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    // The rules a strategy reads are the ones the room holds -- the same ones the
    // players are ruled by.
    QMdmmCore::LogicConfiguration configuration;
    configuration.setPunishHpModifier(2);
    configuration.setPunishHpRoundStrategy(QMdmmCore::LogicConfiguration::RoundDown);
    room->setLogicConfiguration(configuration);
    QCOMPARE(bot.logicConfiguration().punishHpModifier(), 2);

    self->setMaxHp(10);
    self->setHp(10);
    self->setHasKnife(true);
    self->setKnifeDamage(1);
    enemy->setMaxHp(10);
    enemy->setHp(10);

    // In a city a slash is punished with a share of the slasher's own max HP...
    self->setPlace(1);
    enemy->setPlace(1);
    QCOMPARE(self->slashPunishHp(), 5);

    // ...and the engine charges exactly that: the slash costs this player the
    // punish on top of the hit it lands.
    QVERIFY(self->slash(enemy));
    QCOMPARE(self->hp(), 5);
    QCOMPARE(enemy->hp(), 9);

    // Inside the Village the very same slash is free.
    self->setHp(10);
    self->setPlace(QMdmmCore::Data::Village);
    enemy->setPlace(QMdmmCore::Data::Village);
    QCOMPARE(self->slashPunishHp(), 0);
    QVERIFY(self->slash(enemy));
    QCOMPARE(self->hp(), 10);
    QCOMPARE(enemy->hp(), 8);

    // And with the punish rule called off, no city charges either.
    configuration.setPunishHpModifier(0);
    room->setLogicConfiguration(configuration);
    self->setPlace(1);
    enemy->setPlace(1);
    QCOMPARE(self->slashPunishHp(), 0);
}

void tst_QMdmmBot::slash_isSkippedWhenItsPunishWouldBeFatal()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot bot {&client};

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    QMdmmCore::LogicConfiguration configuration;
    configuration.setPunishHpModifier(2);
    configuration.setPunishHpRoundStrategy(QMdmmCore::LogicConfiguration::RoundDown);
    room->setLogicConfiguration(configuration);

    self->setMaxHp(10);
    self->setHp(6);
    self->setHasKnife(true);
    self->setPlace(1);
    enemy->setHp(10);
    enemy->setPlace(1);

    // A slash its owner walks away from is taken...
    QCOMPARE(self->slashPunishHp(), 5);
    QVERIFY(bot.canSlashSafely(enemy));

    // ...but one that would be paid for with this bot's life is not, however
    // healthy the peer happens to be.
    self->setHp(5);
    QVERIFY(!bot.canSlashSafely(enemy));

    // Where zero HP still counts as alive, zero is survivable after all.
    configuration.setZeroHpAsDead(false);
    room->setLogicConfiguration(configuration);
    QVERIFY(bot.canSlashSafely(enemy));
    self->setHp(4);
    QVERIFY(!bot.canSlashSafely(enemy));
    configuration.setZeroHpAsDead(true);
    room->setLogicConfiguration(configuration);

    // Without a knife there is nothing to slash in the first place.
    self->setHasKnife(false);
    QVERIFY(!bot.canSlashSafely(enemy));
    self->setHasKnife(true);

    // A slash inside the Village costs nothing, so even a bot on its last HP
    // takes it.
    self->setPlace(QMdmmCore::Data::Village);
    enemy->setPlace(QMdmmCore::Data::Village);
    self->setHp(1);
    QVERIFY(bot.canSlashSafely(enemy));

    // A peer that is not in the room is nothing to slash at.
    QVERIFY(!bot.canSlashSafely(nullptr));

    // And a bot that has not signed in yet has no self player to slash with.
    QMdmmNetworking::Client otherClient {QMdmmNetworking::ClientConfiguration::defaults()};
    ProbeBot otherBot {&otherClient};
    QVERIFY(!otherBot.canSlashSafely(enemy));
}

void tst_QMdmmBot::action_skipsASlashThatTheCityPunishWouldMakeFatal()
{
    // Both styles are asked: the place rules live in Bot::canSlashSafely(),
    // which every style strikes through, so a city's fatal punish holds both of
    // them back. (The knife style does have one trade that overrides it, but
    // that needs a blow which finishes the peer off -- not this setup.)
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
        QVERIFY(self != nullptr);
        QVERIFY(enemy != nullptr);

        // A city that charges half of the slasher's max HP for a slash.
        QMdmmCore::LogicConfiguration configuration;
        configuration.setPunishHpModifier(2);
        configuration.setPunishHpRoundStrategy(QMdmmCore::LogicConfiguration::RoundDown);
        room->setLogicConfiguration(configuration);

        self->setMaxHp(10);
        self->setHp(6);
        self->setHasKnife(true);
        self->setKnifeDamage(1);
        self->setPlace(1);
        enemy->setHp(10);
        enemy->setPlace(1);

        // HP to spare: the bot attacks the opponent standing next to it.
        const ActionReply attack = askForAction(client, 1);
        QCOMPARE(attack.count, 1);
        QCOMPARE(attack.action, QMdmmCore::Data::Slash);
        QCOMPARE(attack.toPlayer, enemy->objectName());

        // One HP less and that same slash would be punished with this bot's life:
        // the charge is 5 and it has exactly 5 HP left. It spends the round
        // elsewhere instead -- on the horse it does not have yet -- rather than
        // trade its life for the hit.
        self->setHp(5);
        const ActionReply skips = askForAction(client, 1);
        QCOMPARE(skips.count, 1);
        QVERIFY(skips.action != QMdmmCore::Data::Slash);
        QCOMPARE(skips.action, QMdmmCore::Data::BuyHorse);

        // The same last HP is no reason to hold back inside the Village, where a
        // slash is free.
        self->setHp(1);
        self->setPlace(QMdmmCore::Data::Village);
        enemy->setPlace(QMdmmCore::Data::Village);
        const ActionReply freeSlash = askForAction(client, 1);
        QCOMPARE(freeSlash.count, 1);
        QCOMPARE(freeSlash.action, QMdmmCore::Data::Slash);
        QCOMPARE(freeSlash.toPlayer, enemy->objectName());
    }
}

void tst_QMdmmBot::upgrade_spendsOnKnifeThenMaxHpThenHorse()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = Bot::createBot(u"knifePreferred"_s, &client);
    QVERIFY(bot != nullptr);

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QVERIFY(self != nullptr);

    // Under the default rules the knife runs 1..10, max HP 10..20 and the horse
    // 2..10, so what each reply spends the points on is what these show.
    QCOMPARE(self->upgradeKnifeRemainingTimes(), 9);

    // The knife comes first...
    const UpgradeReply knifeFirst = askForUpgrade(client, 2);
    QCOMPARE(knifeFirst.count, 1);
    QCOMPARE(knifeFirst.items, (QList<QMdmmCore::Data::UpgradeItem> {QMdmmCore::Data::UpgradeKnife, QMdmmCore::Data::UpgradeKnife}));

    // ...then max HP, ahead of a horse that still has room to grow (Q3)...
    self->setKnifeDamage(10);
    QCOMPARE(self->upgradeKnifeRemainingTimes(), 0);
    const UpgradeReply maxHpNext = askForUpgrade(client, 3);
    QCOMPARE(maxHpNext.count, 1);
    QCOMPARE(maxHpNext.items, (QList<QMdmmCore::Data::UpgradeItem> {QMdmmCore::Data::UpgradeMaxHp, QMdmmCore::Data::UpgradeMaxHp, QMdmmCore::Data::UpgradeMaxHp}));

    // ...and the horse takes what is left once both are maxed out.
    self->setMaxHp(20);
    QCOMPARE(self->upgradeMaxHpRemainingTimes(), 0);
    const UpgradeReply horseLast = askForUpgrade(client, 2);
    QCOMPARE(horseLast.count, 1);
    QCOMPARE(horseLast.items, (QList<QMdmmCore::Data::UpgradeItem> {QMdmmCore::Data::UpgradeHorse, QMdmmCore::Data::UpgradeHorse}));

    // With nothing left to upgrade there is no feasible list at all, and the bot
    // gives up rather than sending a short one.
    self->setHorseDamage(10);
    QCOMPARE(self->upgradeHorseRemainingTimes(), 0);
    QVERIFY(upgradeWasGivenUp(client, 1));
}

void tst_QMdmmBot::action_buysAHorseOnlyWhileItIsStillShortOfReach()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = Bot::createBot(u"knifePreferred"_s, &client);
    QVERIFY(bot != nullptr);

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    // Armed, in a city, with the only opponent a walk away.
    self->setHasKnife(true);
    self->setPlace(1);
    enemy->setPlace(2);

    // Two slashes of a 3-damage knife would not finish a 10-HP peer off, so
    // there is still ground to make up: the horse is worth buying.
    self->setKnifeDamage(3);
    const ActionReply shortOfReach = askForAction(client, 1);
    QCOMPARE(shortOfReach.count, 1);
    QCOMPARE(shortOfReach.action, QMdmmCore::Data::BuyHorse);

    // Exactly two slashes of a 5-damage knife would, so reach is no longer what
    // the bot is short of: it marches toward the peer instead (self stands in a
    // city, so the way there goes through the Village).
    self->setKnifeDamage(5);
    const ActionReply enoughReach = askForAction(client, 1);
    QCOMPARE(enoughReach.count, 1);
    QCOMPARE(enoughReach.action, QMdmmCore::Data::Move);
    QCOMPARE(enoughReach.toPlace, QMdmmCore::Data::Village);
}

void tst_QMdmmBot::action_paysForASlashWithItsLifeOnlyWhenTheTradeIsWorthIt()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = Bot::createBot(u"knifePreferred"_s, &client);
    QVERIFY(bot != nullptr);

    // A city that charges half of the slasher's max HP for a slash.
    QMdmmCore::LogicConfiguration configuration;
    configuration.setPunishHpModifier(2);
    configuration.setPunishHpRoundStrategy(QMdmmCore::LogicConfiguration::RoundDown);
    client.room()->setLogicConfiguration(configuration);

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    // Q3's example: the two stand in the same city, the bot has exactly the HP
    // the punish takes (5 of a 10-HP maximum), one slash of its 5-damage knife
    // finishes the peer off, and the peer's own knife is too weak to answer in
    // kind.
    self->setMaxHp(10);
    self->setHp(5);
    self->setHasKnife(true);
    self->setKnifeDamage(5);
    self->setPlace(1);
    enemy->setMaxHp(10);
    enemy->setHp(5);
    enemy->setHasKnife(true);
    enemy->setKnifeDamage(1);
    enemy->setPlace(1);

    // Alone with that peer, the trade is the one Q3 names as paying, so the bot
    // strikes knowing the punish takes it along.
    QCOMPARE(room->alivePlayersCount(), 2);
    const ActionReply traded = askForAction(client, 1);
    QCOMPARE(traded.count, 1);
    QCOMPARE(traded.action, QMdmmCore::Data::Slash);
    QCOMPARE(traded.toPlayer, enemy->objectName());

    // A peer this blow would not finish off is not worth dying for.
    enemy->setHp(6);
    const ActionReply survives = askForAction(client, 1);
    QCOMPARE(survives.count, 1);
    QVERIFY(survives.action != QMdmmCore::Data::Slash);

    // Neither is a peer that could cut the bot down in a single blow of its own:
    // that is no longer the stronger side taking a trade, it is a coin flip.
    enemy->setHp(5);
    enemy->setKnifeDamage(10);
    const ActionReply outmatched = askForAction(client, 1);
    QCOMPARE(outmatched.count, 1);
    QVERIFY(outmatched.action != QMdmmCore::Data::Slash);

    // Nor is one with a third peer still alive to profit from the round the bot
    // spends dying in -- Q3's example is a duel.
    enemy->setKnifeDamage(1);
    QMdmmCore::Player *third = room->addPlayer(u"third"_s);
    QVERIFY(third != nullptr);
    third->setPlace(2);
    QCOMPARE(room->alivePlayersCount(), 3);
    const ActionReply duelOnly = askForAction(client, 1);
    QCOMPARE(duelOnly.count, 1);
    QVERIFY(duelOnly.action != QMdmmCore::Data::Slash);
}

void tst_QMdmmBot::action_aimsAtTheBestScoringPeerStandingHere()
{
    // Both styles strike whatever stands with them -- the knife style with its
    // knife, the horse style with whichever of its weapons reaches -- and both
    // read that choice off the same rule in Bot, so both are asked here.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        const QString self = client.objectName();

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *selfPlayer = room->addPlayer(self);
        // Room order is name order, which is what decides a tie.
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(selfPlayer != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        selfPlayer->setHasKnife(true);
        selfPlayer->setKnifeDamage(1);
        selfPlayer->setPlace(1);
        bbb->setPlace(1);
        ccc->setPlace(1);

        // Two harmless peers that have never wronged this bot score nothing, so
        // the blow goes to the first of them in room order.
        QCOMPARE(askForAction(client, 1).toPlayer, u"bbb"_s);

        // A grudge against the other one puts it on top of the score, and the
        // blow follows the score.
        client.agent()->notifyAction(u"ccc"_s, QMdmmCore::Data::Slash, self, 0);
        const ActionReply rated = askForAction(client, 1);
        QCOMPARE(rated.count, 1);
        QCOMPARE(rated.action, QMdmmCore::Data::Slash);
        QCOMPARE(rated.toPlayer, u"ccc"_s);
    }
}

void tst_QMdmmBot::upgrade_spendsOnHorseThenKnifeThenMaxHp()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = Bot::createBot(u"horsePreferred"_s, &client);
    QVERIFY(bot != nullptr);

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QVERIFY(self != nullptr);

    // Under the default rules the horse runs 2..10, the knife 1..10 and max HP
    // 10..20, so what each reply spends the points on is what these show.
    QCOMPARE(self->upgradeHorseRemainingTimes(), 8);

    // The horse comes first...
    const UpgradeReply horseFirst = askForUpgrade(client, 2);
    QCOMPARE(horseFirst.count, 1);
    QCOMPARE(horseFirst.items, (QList<QMdmmCore::Data::UpgradeItem> {QMdmmCore::Data::UpgradeHorse, QMdmmCore::Data::UpgradeHorse}));

    // ...then the knife, ahead of max HP (Q4)...
    self->setHorseDamage(10);
    QCOMPARE(self->upgradeHorseRemainingTimes(), 0);
    const UpgradeReply knifeNext = askForUpgrade(client, 3);
    QCOMPARE(knifeNext.count, 1);
    QCOMPARE(knifeNext.items, (QList<QMdmmCore::Data::UpgradeItem> {QMdmmCore::Data::UpgradeKnife, QMdmmCore::Data::UpgradeKnife, QMdmmCore::Data::UpgradeKnife}));

    // ...and max HP takes what is left once both weapons are maxed out.
    self->setKnifeDamage(10);
    QCOMPARE(self->upgradeKnifeRemainingTimes(), 0);
    const UpgradeReply maxHpLast = askForUpgrade(client, 2);
    QCOMPARE(maxHpLast.count, 1);
    QCOMPARE(maxHpLast.items, (QList<QMdmmCore::Data::UpgradeItem> {QMdmmCore::Data::UpgradeMaxHp, QMdmmCore::Data::UpgradeMaxHp}));

    // With nothing left to upgrade there is no feasible list at all, and the bot
    // gives up rather than sending a short one.
    self->setMaxHp(20);
    QCOMPARE(self->upgradeMaxHpRemainingTimes(), 0);
    QVERIFY(upgradeWasGivenUp(client, 1));
}

void tst_QMdmmBot::action_buysTheHorseBeforeTheKnife()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = Bot::createBot(u"horsePreferred"_s, &client);
    QVERIFY(bot != nullptr);

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    // A round starts every player bare and at its own seat (see
    // Room::prepareForRoundStart()), so this is the opening the style has to
    // play from: no weapon at all, in its own city, with the peer a walk away.
    self->setInitialPlace(1);
    self->setPlace(1);
    enemy->setPlace(2);

    // Both weapons are missing and both are on sale where this bot stands, and
    // the horse is the one it buys first (Q4).
    const ActionReply horseFirst = askForAction(client, 1);
    QCOMPARE(horseFirst.count, 1);
    QCOMPARE(horseFirst.action, QMdmmCore::Data::BuyHorse);

    // The knife is bought too -- just not before the horse (Q4).
    self->setHasHorse(true);
    const ActionReply knifeSecond = askForAction(client, 1);
    QCOMPARE(knifeSecond.count, 1);
    QCOMPARE(knifeSecond.action, QMdmmCore::Data::BuyKnife);
}

void tst_QMdmmBot::action_kicksWhatStandsWithItForFree()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = Bot::createBot(u"horsePreferred"_s, &client);
    QVERIFY(bot != nullptr);

    // A city that charges half of the slasher's own max HP for a slash.
    QMdmmCore::LogicConfiguration configuration;
    configuration.setPunishHpModifier(2);
    configuration.setPunishHpRoundStrategy(QMdmmCore::LogicConfiguration::RoundDown);
    client.room()->setLogicConfiguration(configuration);

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *self = room->addPlayer(client.objectName());
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(self != nullptr);
    QVERIFY(enemy != nullptr);

    // Carrying both weapons and healthy, next to a peer in a city. The slash is
    // legal and survivable there -- it would cost 5 of this bot's 10 HP -- but a
    // kick costs nothing at all, so that is what this style throws.
    self->setHasHorse(true);
    self->setHorseDamage(2);
    self->setHasKnife(true);
    self->setPlace(1);
    enemy->setHp(10);
    enemy->setPlace(1);
    QVERIFY(self->canSlash(enemy));
    QCOMPARE(self->slashPunishHp(), 5);
    const ActionReply kicked = askForAction(client, 1);
    QCOMPARE(kicked.count, 1);
    QCOMPARE(kicked.action, QMdmmCore::Data::Kick);
    QCOMPARE(kicked.toPlayer, enemy->objectName());

    // Inside the Village a kick is forbidden, so the same peer is slashed -- and
    // that slash is free as well.
    self->setPlace(QMdmmCore::Data::Village);
    enemy->setPlace(QMdmmCore::Data::Village);
    QCOMPARE(self->slashPunishHp(), 0);
    const ActionReply slashed = askForAction(client, 1);
    QCOMPARE(slashed.count, 1);
    QCOMPARE(slashed.action, QMdmmCore::Data::Slash);
    QCOMPARE(slashed.toPlayer, enemy->objectName());
}

void tst_QMdmmBot::action_pullsARatedPeerIntoItsCity()
{
    QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
    Bot *bot = Bot::createBot(u"horsePreferred"_s, &client);
    QVERIFY(bot != nullptr);

    const QString self = client.objectName();

    QMdmmCore::Room *room = client.room();
    QMdmmCore::Player *selfPlayer = room->addPlayer(self);
    QMdmmCore::Player *enemy = room->addPlayer(u"enemy"_s);
    QVERIFY(selfPlayer != nullptr);
    QVERIFY(enemy != nullptr);

    // Armed with both weapons, in its own city, with the only peer standing in
    // the Village: one step away, and a kick does not reach that far.
    selfPlayer->setInitialPlace(1);
    selfPlayer->setPlace(1);
    selfPlayer->setHasHorse(true);
    selfPlayer->setHasKnife(true);
    enemy->setPlace(QMdmmCore::Data::Village);

    // A peer nobody rates is not worth a round, and the pull is what costs one:
    // the round goes to closing in on it instead. The walk is into the Village,
    // which is both where the peer stands and where a slash is free.
    const ActionReply unrated = askForAction(client, 1);
    QCOMPARE(unrated.count, 1);
    QCOMPARE(unrated.action, QMdmmCore::Data::Move);
    QCOMPARE(unrated.toPlace, QMdmmCore::Data::Village);

    // Once that peer has wronged this bot it is worth the round, and the round is
    // spent dragging it into the city this bot stands in -- the first half of the
    // pull-kick loop (issue #6 C3). The kick that throws it back into the Village
    // is what the next action time has to bring.
    client.agent()->notifyAction(u"enemy"_s, QMdmmCore::Data::Slash, self, 0);
    const ActionReply pulled = askForAction(client, 1);
    QCOMPARE(pulled.count, 1);
    QCOMPARE(pulled.action, QMdmmCore::Data::LetMove);
    QCOMPARE(pulled.toPlayer, u"enemy"_s);
    QCOMPARE(pulled.toPlace, selfPlayer->place());

    // With the rules refusing to have a peer dragged around, there is no loop to
    // play: the style plays it straight and walks in to slash it (Q4).
    QMdmmCore::LogicConfiguration configuration;
    configuration.setEnableLetMove(false);
    room->setLogicConfiguration(configuration);
    const ActionReply walkedIn = askForAction(client, 1);
    QCOMPARE(walkedIn.count, 1);
    QCOMPARE(walkedIn.action, QMdmmCore::Data::Move);
    QCOMPARE(walkedIn.toPlace, QMdmmCore::Data::Village);

    // From the Village there is nothing to drag, and nowhere to drag it to: the
    // only place a pull made there can put a peer is the Village itself, where
    // the kick it was dragged over for is forbidden and the peer's own slash
    // would be as free as this bot's. So that peer is walked towards instead --
    // into the city it stands in, where the kick does land.
    configuration.setEnableLetMove(true);
    room->setLogicConfiguration(configuration);
    selfPlayer->setPlace(QMdmmCore::Data::Village);
    enemy->setPlace(2);
    const ActionReply walkedOut = askForAction(client, 1);
    QCOMPARE(walkedOut.count, 1);
    QCOMPARE(walkedOut.action, QMdmmCore::Data::Move);
    QCOMPARE(walkedOut.toPlace, enemy->place());
}

void tst_QMdmmBot::rps_doesNotAlwaysAnswerTheSameThrow()
{
    // Every throw a bot may answer with. All three are legal answers, and which
    // one wins the request only decides the action order.
    const QList<QMdmmCore::Data::RockPaperScissors> legal {
        QMdmmCore::Data::Rock,
        QMdmmCore::Data::Paper,
        QMdmmCore::Data::Scissors,
    };

    // The two implemented styles answer through one shared fallback (the rl style
    // is a placeholder that terminates in its constructor, so it never gets as far
    // as answering).
    const QList<QString> styles {u"knifePreferred"_s, u"horsePreferred"_s};
    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QList<QMdmmCore::Data::RockPaperScissors> answered;
        for (int i = 0; i < 60; ++i) {
            const ThrowReply reply = askForThrow(client);
            QCOMPARE(reply.count, 1);
            QVERIFY(legal.contains(reply.answer));
            if (!answered.contains(reply.answer))
                answered << reply.answer;
        }

        // Never the same throw every time: answering one fixed throw is what makes
        // two of these bots tie on every request and stay stuck at the first action
        // time of a round (the D1 backlog item). Sixty draws all landing on the
        // same one of three is a 3^-59 coincidence, so a failure here means the
        // throw is constant rather than that one draw was unlucky.
        QVERIFY(answered.size() > 1);
    }
}

void tst_QMdmmBot::actionOrder_takesOnlyTheOrdersItWasOffered()
{
    // Both styles answer an action-order request the same way, so both are asked
    // here. Nothing has been laid out in the room, so no blow can land on anybody
    // and the later orders are the ones worth asking for (see
    // actionOrder_holdsBackWhenNoBlowCanBeFatal()).
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        const QList<int> offered {1, 2};

        // Asked for fewer selections than it was offered, it takes that many,
        // from the late end of the offer.
        const ActionOrderReply one = askForActionOrder(client, offered, 1);
        QCOMPARE(one.count, 1);
        QCOMPARE(one.order, (QList<int> {2}));

        // Asked for exactly the number of orders on offer, it takes all of them.
        const ActionOrderReply all = askForActionOrder(client, offered, 2);
        QCOMPARE(all.count, 1);
        QCOMPARE(all.order, offered);

        // Asked for more selections than there are orders -- which the server
        // promises never to do, but nothing on the wire holds it to -- it still
        // answers once, with the orders it actually has, instead of reading past
        // the end of the list.
        const ActionOrderReply over = askForActionOrder(client, offered, 5);
        QCOMPARE(over.count, 1);
        QCOMPARE(over.order, offered);

        // Asked for no selections at all, it answers with none.
        const ActionOrderReply none = askForActionOrder(client, offered, 0);
        QCOMPARE(none.count, 1);
        QVERIFY(none.order.isEmpty());
    }
}

void tst_QMdmmBot::actionOrder_holdsBackWhenNoBlowCanBeFatal()
{
    // Three peers in three places, none of them within reach of another: no blow
    // can land this round, so waiting costs nothing, and by the time the bot's
    // turn comes up every other commitment is out in the open. The latest order on
    // offer is the one worth asking for -- the opposite of what "take the first
    // orders" answered on this same layout.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        // Every knife here is fatal to whoever it is aimed at -- nobody is within
        // reach of anybody, which is what leaves the round with nothing that can
        // happen in it.
        placeArmedPlayer(self, 1, 4, 4);
        placeArmedPlayer(bbb, 2, 4, 4);
        placeArmedPlayer(ccc, QMdmmCore::Data::Village, 4, 4);

        // Asked for one of the two orders on offer, it asks for the later one.
        const ActionOrderReply late = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(late.count, 1);
        QCOMPARE(late.order, (QList<int> {2}));
        QVERIFY(!late.order.contains(0));

        // Asked for both, it asks for both rather than giving one back.
        const ActionOrderReply all = askForActionOrder(client, {1, 2}, 2);
        QCOMPARE(all.count, 1);
        QCOMPARE(all.order, (QList<int> {1, 2}));
        QVERIFY(!all.order.contains(0));
    }
}

void tst_QMdmmBot::actionOrder_grabsTheEarliestOrderWhenItsOwnLifeIsOnTheLine()
{
    // A peer standing where this bot stands with a knife that would finish it off:
    // the bot may not live to see the last order, and an action it has not taken
    // when it dies is skipped, so the earliest order is the one worth having.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        // The knife standing here is fatal to this bot; its own knife is not fatal
        // to that peer, and "ccc" is out of everyone's reach.
        placeArmedPlayer(self, 1, 4, 1);
        placeArmedPlayer(bbb, 1, 4, 4);
        placeArmedPlayer(ccc, 2, 4, 1);

        const ActionOrderReply early = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(early.count, 1);
        QCOMPARE(early.order, (QList<int> {1}));
        QVERIFY(!early.order.contains(0));
    }
}

void tst_QMdmmBot::actionOrder_grabsTheEarliestOrderWhenAKillIsOnTheTable()
{
    // A peer standing here that one blow of this bot's would finish: a kill is the
    // only way to an upgrade point, so it is worth an early order. Nothing here can
    // finish this bot off -- the knife facing it is too weak -- so it is the kill
    // and not the danger that turns the answer around.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        placeArmedPlayer(self, 1, 4, 4);
        placeArmedPlayer(bbb, 1, 4, 1);
        placeArmedPlayer(ccc, 2, 4, 1);

        const ActionOrderReply early = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(early.count, 1);
        QCOMPARE(early.order, (QList<int> {1}));
        QVERIFY(!early.order.contains(0));
    }
}

void tst_QMdmmBot::actionOrder_grabsTheEarliestOrderWhenAPeerCouldBeFinished()
{
    // A bot that neither is in danger nor has a kill of its own still waits at its
    // peril when somebody else can die: a death can end the round before a late
    // order runs, and the order is lost when that happens. So the earliest order is
    // taken as soon as any blow on the field would be fatal -- here between the two
    // peers, with this bot standing aside.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        placeArmedPlayer(self, 1, 4, 1);
        placeArmedPlayer(bbb, 2, 4, 4);
        placeArmedPlayer(ccc, 2, 4, 1);

        const ActionOrderReply early = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(early.count, 1);
        QCOMPARE(early.order, (QList<int> {1}));
        QVERIFY(!early.order.contains(0));
    }
}

void tst_QMdmmBot::actionOrder_grabsTheEarliestOrderWhenAPeersKickWouldFinishSelf()
{
    // The horse is the other way this bot's life can be on the line: a peer
    // standing where it stands, in the saddle, whose kick would finish it off.
    // Neither of them holds a knife, so the danger here is the horse and nothing
    // but the horse -- the round a knife-only reading of the field would have
    // called harmless, and answered with the latest order.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        // The horse standing here kicks for everything this bot has, and "ccc" is
        // out of everyone's reach.
        placePlayer(self, 1, 4);
        placeMountedPlayer(bbb, 1, 4, 4);
        placePlayer(ccc, 2, 4);

        const ActionOrderReply early = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(early.count, 1);
        QCOMPARE(early.order, (QList<int> {1}));
        QVERIFY(!early.order.contains(0));
    }
}

void tst_QMdmmBot::actionOrder_grabsTheEarliestOrderWhenItsOwnKickWouldFinishAPeer()
{
    // And the other way round: this bot is the one in the saddle, and the peer
    // standing here would go down to its kick. What that peer holds is too weak to
    // finish this bot off, so it is the kill -- the only way to an upgrade point --
    // and not the danger that turns the answer around.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        placeMountedPlayer(self, 1, 4, 4);
        placeArmedPlayer(bbb, 1, 4, 1);
        placeArmedPlayer(ccc, 2, 4, 1);

        const ActionOrderReply early = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(early.count, 1);
        QCOMPARE(early.order, (QList<int> {1}));
        QVERIFY(!early.order.contains(0));
    }
}

void tst_QMdmmBot::actionOrder_grabsTheEarliestOrderWhenAPeersKickCouldFinishSomebody()
{
    // The guard side, in the shape the knife case above already has: this bot is
    // neither in danger nor carrying a kill of its own, but a kick between two
    // peers would finish one of them, and a death can end the round before a late
    // order runs. The kick is the only fatal blow on the field here, and it is
    // enough on its own to give up on waiting.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QMdmmCore::Player *ccc = room->addPlayer(u"ccc"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);
        QVERIFY(ccc != nullptr);

        placePlayer(self, 1, 4);
        placeMountedPlayer(bbb, 2, 4, 4);
        placePlayer(ccc, 2, 4);

        const ActionOrderReply early = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(early.count, 1);
        QCOMPARE(early.order, (QList<int> {1}));
        QVERIFY(!early.order.contains(0));
    }
}

void tst_QMdmmBot::actionOrder_readsTheDeathThresholdOffTheMatchRules()
{
    // Where a player stops being alive is a rule of the match, so the same layout
    // answers differently under the two rules it allows: a blow leaving a peer on
    // exactly zero HP is fatal where zero counts as dead and is not where HP has to
    // go below zero. The bot asks the match rather than assuming either reading.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s};

    for (const QString &style : styles) {
        QMdmmNetworking::Client client {QMdmmNetworking::ClientConfiguration::defaults()};
        Bot *bot = Bot::createBot(style, &client);
        QVERIFY(bot != nullptr);

        QMdmmCore::Room *room = client.room();
        QMdmmCore::Player *self = room->addPlayer(client.objectName());
        QMdmmCore::Player *bbb = room->addPlayer(u"bbb"_s);
        QVERIFY(self != nullptr);
        QVERIFY(bbb != nullptr);

        // One blow from "bbb" takes this bot to exactly zero HP.
        placeArmedPlayer(self, 1, 4, 1);
        placeArmedPlayer(bbb, 1, 4, 4);

        // Under the default rules that is death, so the early order is taken.
        const ActionOrderReply underDefaultRules = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(underDefaultRules.count, 1);
        QCOMPARE(underDefaultRules.order, (QList<int> {1}));

        // Under the other reading the bot survives on zero HP, nothing on the field
        // can be fatal, and waiting is free again.
        QMdmmCore::LogicConfiguration rules = QMdmmCore::LogicConfiguration::defaults();
        rules.setZeroHpAsDead(false);
        room->setLogicConfiguration(rules);

        const ActionOrderReply underTheOtherReading = askForActionOrder(client, {1, 2}, 1);
        QCOMPARE(underTheOtherReading.count, 1);
        QCOMPARE(underTheOtherReading.order, (QList<int> {2}));
    }
}

// The three transports a client can be pointed at, one row each. The prefix
// whitelist in SocketP::typeByConnectAddr picks the transport from the connect
// address, and each of the three reaches the server through a different listener
// and a different socket implementation. Only the TCP one had ever been
// connected: the websocket and the local socket -- the client half of each, and
// the session the server builds once it accepts one -- went unexercised, which
// is the gap this case's rows close.
void tst_QMdmmBot::fullGame_theTwoStylesPlayAWholeMatchToTheEnd_data()
{
    QTest::addColumn<QString>("host");

    QTest::newRow("tcp") << u"qmdmm://127.0.0.1:%1"_s.arg(MATCH_PORT);
    QTest::newRow("websocket") << u"ws://127.0.0.1:%1"_s.arg(MATCH_WEBSOCKET_PORT);
    // A local socket is named rather than addressed, so its "address" is the
    // name the server listens on.
    QTest::newRow("local socket") << MATCH_LOCAL_SOCKET_NAME;
}

void tst_QMdmmBot::fullGame_theTwoStylesPlayAWholeMatchToTheEnd()
{
    QFETCH(QString, host);

    // The cases above drive one handler at a time against a room mirror laid out
    // by hand. This one lets the match run its own loop -- Rock-Paper-Scissors,
    // action order, actions, upgrades, round over, until the game ends -- with the
    // real styles sitting in it, which is the configuration the shipped QMdmmBot
    // plays in. What it guards is what none of the others can: that the replies
    // these strategies actually produce are accepted by the server, and that they
    // never leave a request unanswered. Both would show up in the field as a bot
    // that got disconnected mid-match, and neither shows up in a handler test.
    //
    // A match small enough to finish quickly: one blow kills, so rounds do end,
    // and each stat is only a few upgrades away from its maximum -- a player that
    // has maxed out all three is the winner, which is what ends the game.
    QMdmmCore::LogicConfiguration rules = QMdmmCore::LogicConfiguration::defaults();
    rules.setInitialMaxHp(1);
    rules.setMaximumMaxHp(8);
    rules.setInitialKnifeDamage(1);
    rules.setMaximumKnifeDamage(8);
    rules.setInitialHorseDamage(1);
    rules.setMaximumHorseDamage(8);
    // A slash in a city costs the slasher HP; with the punish called off, neither
    // style is held back by it on the way to the finish.
    rules.setPunishHpModifier(0);

    // The seats' styles. Both real styles are in the match, and one of them takes
    // a second seat: a two-player match never gets as far as the action-order
    // phase (with a single Rock-Paper-Scissors winner, startActionOrder() hands
    // every order out without negotiating), and the shared order pick is part of
    // the loop this case is here to run. Three players bring that phase on
    // whenever the Rock-Paper-Scissors leaves two winners.
    const QStringList styles = {u"knifePreferred"_s, u"horsePreferred"_s, u"knifePreferred"_s};

    QMdmmNetworking::ServerConfiguration serverConfiguration = QMdmmNetworking::ServerConfiguration::defaults();
    serverConfiguration.setTcpPort(MATCH_PORT);
    serverConfiguration.setWebsocketPort(MATCH_WEBSOCKET_PORT);
    serverConfiguration.setLocalSocketName(MATCH_LOCAL_SOCKET_NAME);
    serverConfiguration.setPlayerNumPerRoom(styles.size());
    // All three transports listen, on endpoints of this case's own (see the
    // constants above). The row says which one the seats come in over; the other
    // two listening at the same time is what shows the three do not step on each
    // other.

    // A bot has its answer ready as soon as it is asked, so a request still
    // unanswered a couple of seconds later is one that is never going to be
    // answered. The server would hold such a request open for its own grace
    // period before treating the timeout as a disconnect; what catches a stalled
    // bot here is the match deadline below, well before that.
    serverConfiguration.setRequestTimeout(2);

    QMdmmNetworking::Server server {serverConfiguration, rules};
    QVERIFY(server.listen());

    std::vector<std::unique_ptr<Seat>> seats;
    seats.reserve(styles.size());
    for (const QString &style : styles) {
        seats.push_back(std::make_unique<Seat>());
        Seat &seat = *seats.back();
        seat.bot = Bot::createBot(style, &seat.client);
        QVERIFY(seat.bot != nullptr);
        tallyReplies(seat.client, seat.tally);
    }

    // The match is watched through one seat's agent, which is told the same
    // things as the others are.
    QMdmmNetworking::Agent *const watcher = seats.front()->client.agent();
    int roundsStarted = 0;
    int roundsOver = 0;
    int gameOvers = 0;
    QStringList winners;
    QObject::connect(watcher, &QMdmmNetworking::Agent::roundStartNotified, watcher, [&roundsStarted] { ++roundsStarted; });
    QObject::connect(watcher, &QMdmmNetworking::Agent::roundOverNotified, watcher, [&roundsOver] { ++roundsOver; });
    QObject::connect(watcher, &QMdmmNetworking::Agent::gameOverNotified, watcher, [&gameOvers, &winners](const QStringList &playerNames) {
        ++gameOvers;
        winners = playerNames;
    });

    // A dropped connection fails this case rather than being played through:
    // nothing in this match interrupts a seat on purpose, so a drop means the
    // server closed the socket -- either because a bot stalled past its timeout
    // or because something it sent was refused.
    bool connectionLost = false;
    const auto noteLost = [&connectionLost](const QString &errorString) {
        connectionLost = true;
        qWarning() << "the whole-match case lost a connection:" << errorString;
    };
    for (const std::unique_ptr<Seat> &seat : seats) {
        QObject::connect(&seat->client, &QMdmmNetworking::Client::socketConnectionLost, &seat->client, noteLost);
        QObject::connect(&seat->client, &QMdmmNetworking::Client::socketErrorDisconnected, &seat->client, noteLost);
    }

    QEventLoop match;
    QObject::connect(watcher, &QMdmmNetworking::Agent::gameOverNotified, &match, &QEventLoop::quit);
    QTimer::singleShot(MATCH_TIMEOUT_MS, &match, &QEventLoop::quit);

    // A reply the server will not use is refused quietly: the logic warns and
    // carries on with its own default reply (see Logic::actionOrderReply,
    // Logic::actionReply and Logic::upgradeReply), so a strategy that answered
    // illegally would leave the match running and this case green. These three
    // warnings are therefore the illegal replies themselves, and the match is run
    // with them failing the case. Whatever else the match warns about -- a long
    // Rock-Paper-Scissors tie streak, for one, which is a normal thing for this
    // game to hit -- is left alone.
    QTest::failOnWarning(QRegularExpression(u"Logic::(actionOrderReply|actionReply|upgradeReply)"_s));

    for (const std::unique_ptr<Seat> &seat : seats)
        QVERIFY(seat->client.connectToHost(host, QMdmmCore::Data::StateOnlineBot));
    match.exec();

    // Playing the match to its end is the assertion this case exists for: a bot
    // that merely stayed alive is not evidence of anything, which is what the D1
    // item says about the "the bot survived N seconds" check this replaces.
    QCOMPARE(gameOvers, 1);
    QVERIFY(roundsStarted > 0);
    QVERIFY(roundsOver > 0);

    // One of the seats won it -- there was nobody else in the room.
    bool winnerIsASeat = false;
    for (const std::unique_ptr<Seat> &seat : seats)
        winnerIsASeat = winnerIsASeat || winners.contains(seat->client.objectName());
    QVERIFY(winnerIsASeat);

    // Every seat kept its connection for the whole match.
    QVERIFY(!connectionLost);

    // Every request that reached a bot got an answer out of it: the reply-or-giveUp
    // contract, which is what keeps the server from timing a bot out and dropping
    // it mid-match. Fewer answers than requests is a stall, more is a bot
    // answering the same request twice, so the counts have to come out equal.
    for (const std::unique_ptr<Seat> &seat : seats) {
        QVERIFY(seat->tally.requests > 0);
        QCOMPARE(seat->tally.answers(), seat->tally.requests);
    }
}

namespace {
RegisterTestObject<tst_QMdmmBot> _;
}
#include "tst_qmdmmbot.moc"

// NOLINTEND

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bot.h"

#include <QMdmmAgent>

#include <QRandomGenerator>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

Bot::Bot(QMdmmNetworking::Client *parent)
    : QObject(parent)
{
    QMdmmNetworking::Agent *agent = client()->agent();

    // Requests: the server asks this bot for a choice. Each handler is a
    // virtual slot so a style subclass overrides it to implement its strategy.
    connect(agent, &QMdmmNetworking::Agent::rockPaperScissorsRequested, this, &Bot::handleRockPaperScissorsRequest);
    connect(agent, &QMdmmNetworking::Agent::actionOrderRequested, this, &Bot::handleActionOrderRequest);
    connect(agent, &QMdmmNetworking::Agent::actionRequested, this, &Bot::handleActionRequest);
    connect(agent, &QMdmmNetworking::Agent::upgradeRequested, this, &Bot::handleUpgradeRequest);

    // Notifications: the server broadcasts game progress. These are connected to
    // fixed entry points rather than to virtual slots, so what a style subclass
    // overrides is only the half that is its own business (see the hooks below).
    connect(agent, &QMdmmNetworking::Agent::logicConfigurationNotified, this, &Bot::handleLogicConfigurationNotified);
    connect(agent, &QMdmmNetworking::Agent::roundStartNotified, this, &Bot::handleRoundStartNotified);
    connect(agent, &QMdmmNetworking::Agent::actionNotified, this, &Bot::handleActionNotified);
    connect(agent, &QMdmmNetworking::Agent::upgradeNotified, this, &Bot::handleUpgradeNotified);
    connect(agent, &QMdmmNetworking::Agent::roundOverNotified, this, &Bot::handleRoundOverNotified);
    connect(agent, &QMdmmNetworking::Agent::gameOverNotified, this, &Bot::handleGameOverNotified);
}

// Bot is abstract because the four request handlers are pure virtual (a style
// subclass must override all of them to become concrete). The destructor is
// also pure virtual and gets a defaulted definition here.
Bot::~Bot() = default;

// Notification entry points: each one keeps the state every bot shares up to
// date and then hands off to the matching hook (see the hooks below). Keeping
// the two apart is what makes a style's tracking additive -- the hook a subclass
// overrides has nothing to do with the state kept here, so it cannot lose it.

void Bot::handleLogicConfigurationNotified()
{
    onLogicConfigurationNotified();
}

void Bot::handleRoundStartNotified()
{
    onRoundStartNotified();
}

void Bot::handleActionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace)
{
    // Revenge memory: only Slash and Kick are hostile. LetMove moves a player
    // against their will but deals no damage, so it never earns a grudge. What
    // the action was aimed at decides whether it counts, and nothing here
    // filters what the hook is told -- a style tracks the whole match, not just
    // the hits taken.
    const bool hostile = action == QMdmmCore::Data::Slash || action == QMdmmCore::Data::Kick;
    if (hostile && toPlayer == client()->objectName())
        revenge_[playerName] += revengePerAttack;

    onActionNotified(playerName, action, toPlayer, toPlace);
}

void Bot::handleUpgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades)
{
    onUpgradeNotified(upgrades);
}

void Bot::handleRoundOverNotified()
{
    // Revenge memory: a round has passed, so every grudge fades a little. An
    // entry is only dropped once it has become negligible, which is what lets a
    // grudge outlive the round it was earned in.
    const QStringList attackers = revenge_.keys();
    for (const QString &attacker : attackers) {
        const double decayed = revenge_.value(attacker) * revengeDecayPerRound;
        if (decayed <= revengeEpsilon)
            revenge_.remove(attacker);
        else
            revenge_.insert(attacker, decayed);
    }

    onRoundOverNotified();
}

void Bot::handleGameOverNotified(const QStringList &playerNames)
{
    onGameOverNotified(playerNames);
}

// Notification hooks: the base implementations do nothing, and the parameters
// are unused for that reason -- a hook is a place for a style to put its own
// tracking, not a state the base class keeps here.

void Bot::onLogicConfigurationNotified()
{
}

void Bot::onRoundStartNotified()
{
}

void Bot::onActionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace)
{
    Q_UNUSED(playerName);
    Q_UNUSED(action);
    Q_UNUSED(toPlayer);
    Q_UNUSED(toPlace);
}

void Bot::onUpgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades)
{
    Q_UNUSED(upgrades);
}

void Bot::onRoundOverNotified()
{
}

void Bot::onGameOverNotified(const QStringList &playerNames)
{
    Q_UNUSED(playerNames);
}

QMdmmNetworking::Client *Bot::client()
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): Bot always has Client as parent; skip dynamic_cast cost
    return static_cast<QMdmmNetworking::Client *>(parent());
}

const QMdmmNetworking::Client *Bot::client() const
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): same as above
    return static_cast<const QMdmmNetworking::Client *>(parent());
}

QMdmmCore::Player *Bot::selfPlayer()
{
    return client()->room()->player(client()->objectName());
}

QList<QMdmmCore::Player *> Bot::opponents()
{
    QList<QMdmmCore::Player *> result;
    const QString selfName = client()->objectName();
    const QList<QMdmmCore::Player *> alive = client()->room()->alivePlayers();
    for (QMdmmCore::Player *player : alive) {
        if (player->objectName() != selfName)
            result << player;
    }
    return result;
}

double Bot::revengeScore(const QString &playerName) const
{
    // A peer that never attacked us is missing from the table, and QHash::value
    // hands back a default-constructed double for it -- exactly the 0 wanted.
    return revenge_.value(playerName);
}

double Bot::threatScore(const QString &playerName) const
{
    const QMdmmCore::Room *room = client()->room();
    const QMdmmCore::Player *self = room->player(client()->objectName());
    const QMdmmCore::Player *threat = room->player(playerName);

    // Before sign-in there is no self player yet, and a peer that is not in the
    // room (or is already dead) cannot hurt us.
    if (self == nullptr || threat == nullptr || self->dead() || threat->dead())
        return 0.0;

    // Reach: a peer in the same place can hit us right now, a peer that is
    // merely adjacent has to move in first and so lands a round later, and a
    // peer further away cannot reach us within one round at all.
    double reach = 0.0;
    if (threat->place() == self->place())
        reach = threatSamePlaceWeight;
    else if (QMdmmCore::Data::isPlaceAdjacent(threat->place(), self->place()))
        reach = threatAdjacentWeight;
    else
        return 0.0;

    // Offensive power: the weapons the peer holds. The hit lands wherever this
    // bot is standing -- its own place, or here after the move in -- and a horse
    // cannot be used inside the Village.
    double damage = 0.0;
    if (threat->hasKnife())
        damage += threat->knifeDamage();
    if (threat->hasHorse() && self->place() != QMdmmCore::Data::Village)
        damage += threat->horseDamage();

    return reach * damage;
}

double Bot::targetScore(const QString &playerName) const
{
    // Both dimensions are weighted and then added; the two terms are kept apart
    // so the weighting reads as one step and the sum as another.
    const double grudge = revengeWeight * revengeScore(playerName);
    const double threat = threatWeight * threatScore(playerName);

    return grudge + threat;
}

QString Bot::selectTarget() const
{
    const QMdmmCore::Room *room = client()->room();
    const QString selfName = client()->objectName();

    QString target;
    double best = 0.0;

    // Only alive peers are candidates, and a strict comparison from the zero
    // start means a peer that scores nothing is never picked and that the first
    // of several equally good peers wins.
    const QList<const QMdmmCore::Player *> alive = room->alivePlayers();
    for (const QMdmmCore::Player *player : alive) {
        if (player->objectName() == selfName)
            continue;
        const double score = targetScore(player->objectName());
        if (score > best) {
            best = score;
            target = player->objectName();
        }
    }

    return target;
}

QMdmmCore::Player *Bot::attackTarget()
{
    QMdmmCore::Player *self = selfPlayer();
    if (self == nullptr)
        return nullptr;

    QMdmmCore::Player *firstHere = nullptr;
    QMdmmCore::Player *bestScored = nullptr;
    double bestScore = 0.0;

    // A strict comparison from the zero start keeps the first of several equally
    // good peers (room order) and leaves a peer that scores nothing to the
    // fallback below.
    for (QMdmmCore::Player *to : opponents()) {
        if (to->place() != self->place())
            continue;
        if (firstHere == nullptr)
            firstHere = to;
        const double score = targetScore(to->objectName());
        if (score > bestScore) {
            bestScore = score;
            bestScored = to;
        }
    }

    return (bestScored != nullptr) ? bestScored : firstHere;
}

const QMdmmCore::LogicConfiguration &Bot::logicConfiguration() const
{
    // The mirror holds the rules the server broadcast. Before that broadcast, and
    // for every rule it left out, the getters answer from
    // LogicConfiguration::defaults() -- which is the same answer Player works the
    // punish out from, so the two can never disagree.
    return client()->room()->logicConfiguration();
}

bool Bot::canSlashSafely(const QMdmmCore::Player *to) const
{
    const QMdmmCore::Player *self = client()->room()->player(client()->objectName());

    // Before sign-in there is no self player, and a peer that is not in the room
    // is nothing to slash at.
    if (self == nullptr || to == nullptr)
        return false;

    if (!self->canSlash(to))
        return false;

    // A slash pays for itself in HP, so a bot leaves out the ones that would take
    // it to its own death threshold. Where that threshold lies is a rule of the
    // match, so it is asked of the player rather than assumed.
    const int hpLeft = self->hp() - self->slashPunishHp();
    return !self->deadAtHp(hpLeft);
}

QMdmmCore::Data::RockPaperScissors Bot::pickThrow()
{
    // The three throws, listed rather than counted through: their values are the
    // historical wire encoding (see QMdmmCore::Data::RockPaperScissors), so they
    // are not the plain 0..2 a counter would hand out.
    constexpr std::array<QMdmmCore::Data::RockPaperScissors, 3> throws {
        QMdmmCore::Data::Rock,
        QMdmmCore::Data::Paper,
        QMdmmCore::Data::Scissors,
    };

    const int index = QRandomGenerator::global()->bounded(static_cast<int>(throws.size()));
    return throws.at(index);
}

QList<int> Bot::desiredActionOrders(const QList<int> &remainedOrders, int selectionNum) const
{
    // An early order is worth paying for as soon as something can happen this
    // round that only an early turn can answer: a blow that would finish this bot
    // off, a blow of its own that would finish somebody (the only way to an
    // upgrade point), or a round that could be over before the last order runs.
    // With none of those on the table, the latest orders are the better ones:
    // nobody can act against this bot before its turn, and by the time the turn
    // comes up every other commitment is out in the open. Waiting is only safe
    // while no blow on the field can be fatal, though, so an early order is taken
    // as soon as one would be -- a late turn is lost outright to a round that ends
    // first. (The two conditions overlap: a round short enough to end before the
    // last order runs is one with fatal blows in it, which is the case the guard
    // below catches anyway.)
    const bool urgent = aPeerCouldFinishSelf() || selfCouldFinishAPeer() || roundCouldEndEarly();
    const bool noBlowCanBeFatal = finishingBlowsOnTheTable() == 0;
    const bool holdBack = !urgent && noBlowCanBeFatal;

    // The offer arrives ascending, but which end is taken from is read off the
    // values rather than off the order they arrived in. Either pick goes back
    // ascending: the reply is a set of orders, and that is how it is read.
    QList<int> offers = remainedOrders;
    std::ranges::sort(offers);

    const int count = qMin(selectionNum, static_cast<int>(offers.size()));
    if (count <= 0)
        return {};

    return holdBack ? offers.mid(offers.size() - count) : offers.mid(0, count);
}

bool Bot::blowWouldFinish(const QMdmmCore::Player *attacker, const QMdmmCore::Player *victim) const
{
    // Both blows that carry damage are modelled. Whether one of them could be
    // thrown at all is asked of the player -- Player::canSlash() for the knife,
    // Player::canKick() for the horse -- and each predicate already carries its
    // own half of the rule: the two standing in the same place, both of them
    // alive, and, for the kick, outside the Village. Restating those here would
    // only give them a second place to drift.
    //
    // Leaving the kick out reads a second, separately configured weapon as
    // harmless: the horse deals its own damage, and the safety condition this
    // feeds -- "waiting is only safe while no blow on the field can be fatal" --
    // is then answered off the weaker half of the field only.
    //
    // Whether what a blow leaves behind is fatal is a rule of the match (see
    // LogicConfiguration::zeroHpAsDead), so it is asked of the victim rather than
    // assumed -- the same predicate canSlashSafely() uses for this bot's own life.
    const auto wouldFinish = [victim](int damage) { return victim->deadAtHp(victim->hp() - damage); };

    if (attacker->canSlash(victim) && wouldFinish(attacker->knifeDamage()))
        return true;

    return attacker->canKick(victim) && wouldFinish(attacker->horseDamage());
}

bool Bot::aPeerCouldFinishSelf() const
{
    const QMdmmCore::Room *room = client()->room();
    const QMdmmCore::Player *self = room->player(client()->objectName());
    if (self == nullptr)
        return false;

    const auto finishesSelf = [this, self](const QMdmmCore::Player *peer) { return peer != self && blowWouldFinish(peer, self); };
    return std::ranges::any_of(room->alivePlayers(), finishesSelf);
}

bool Bot::selfCouldFinishAPeer() const
{
    const QMdmmCore::Room *room = client()->room();
    const QMdmmCore::Player *self = room->player(client()->objectName());
    if (self == nullptr)
        return false;

    const auto finishesPeer = [this, self](const QMdmmCore::Player *peer) { return peer != self && blowWouldFinish(self, peer); };
    return std::ranges::any_of(room->alivePlayers(), finishesPeer);
}

int Bot::finishingBlowsOnTheTable() const
{
    const QList<const QMdmmCore::Player *> alive = client()->room()->alivePlayers();

    int count = 0;
    for (const QMdmmCore::Player *attacker : alive) {
        for (const QMdmmCore::Player *victim : alive) {
            if (attacker != victim && blowWouldFinish(attacker, victim))
                ++count;
        }
    }

    return count;
}

bool Bot::roundCouldEndEarly() const
{
    const int alive = client()->room()->alivePlayersCount();

    // A round with nobody but this bot in it has no last player left to reach, so
    // there is nothing to cut short.
    if (alive <= 1)
        return false;

    return finishingBlowsOnTheTable() >= alive - 1;
}

Bot *Bot::createBot(const QString &style, QMdmmNetworking::Client *parent)
{
    if (style == u"knifePreferred"_s)
        return new KnifePreferredBot(parent);
    if (style == u"horsePreferred"_s)
        return new HorsePreferredBot(parent);
    if (style == u"rl"_s)
        return new RlBot(parent);
    return nullptr;
}

bool Bot::styleExist(const QString &style)
{
    return style == u"knifePreferred"_s || style == u"horsePreferred"_s || style == u"rl"_s;
}

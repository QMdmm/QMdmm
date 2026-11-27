// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMPLAYER_H
#define QMDMMPLAYER_H

#include "qmdmmcoreglobal.h"

#include <QObject>

QMDMM_EXPORT_NAME(QMdmmPlayer)

namespace QMdmmCore {

#ifndef DOXYGEN
namespace p {
struct PlayerP;
}
#endif

#ifndef DOXYGEN
namespace v0 {
#endif

class Room;

class QMDMMCORE_EXPORT Player final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool hasKnife READ hasKnife WRITE setHasKnife NOTIFY hasKnifeChanged DESIGNABLE false FINAL)
    Q_PROPERTY(bool hasHorse READ hasHorse WRITE setHasHorse NOTIFY hasHorseChanged DESIGNABLE false FINAL)
    Q_PROPERTY(int hp READ hp WRITE setHp NOTIFY hpChanged DESIGNABLE false FINAL)
    Q_PROPERTY(int place READ place WRITE setPlace NOTIFY placeChanged DESIGNABLE false FINAL)
    Q_PROPERTY(int initialPlace READ initialPlace WRITE setInitialPlace NOTIFY initialPlaceChanged DESIGNABLE false FINAL)
    Q_PROPERTY(int knifeDamage READ knifeDamage WRITE setKnifeDamage NOTIFY knifeDamageChanged DESIGNABLE false FINAL)
    Q_PROPERTY(int horseDamage READ horseDamage WRITE setHorseDamage NOTIFY horseDamageChanged DESIGNABLE false FINAL)
    Q_PROPERTY(int maxHp READ maxHp WRITE setMaxHp NOTIFY maxHpChanged DESIGNABLE false FINAL)
    Q_PROPERTY(int upgradePoint READ upgradePoint WRITE setUpgradePoint NOTIFY upgradePointChanged DESIGNABLE false FINAL)

    // Not-stored properties (i.e. calculated one)
    Q_PROPERTY(bool dead READ dead NOTIFY hpChanged STORED false DESIGNABLE false FINAL)
    Q_PROPERTY(bool alive READ alive STORED false DESIGNABLE false FINAL)

public:
    Q_DISABLE_COPY_MOVE(Player);

    explicit Player(const QString &name, Room *room /* or parent */);
    ~Player() override;

    // property setters/getters
    [[nodiscard]] Room *room();
    [[nodiscard]] const Room *room() const;

    // current property
    [[nodiscard]] bool hasKnife() const noexcept;
    void setHasKnife(bool k);
    [[nodiscard]] bool hasHorse() const noexcept;
    void setHasHorse(bool h);
    [[nodiscard]] int hp() const noexcept;
    void setHp(int h, bool *kills = nullptr);
    [[nodiscard]] int place() const noexcept;
    void setPlace(int toPlace);
    [[nodiscard]] int initialPlace() const noexcept;
    void setInitialPlace(int initialPlace);

    // upgradeable property
    [[nodiscard]] int knifeDamage() const noexcept;
    void setKnifeDamage(int k);
    [[nodiscard]] int horseDamage() const noexcept;
    void setHorseDamage(int h);
    [[nodiscard]] int maxHp() const noexcept;
    void setMaxHp(int m);
    [[nodiscard]] int upgradePoint() const noexcept;
    void setUpgradePoint(int u);

    // calculated property
    // Whether a given HP would leave this player dead. Where the death threshold
    // lies is a rule of the match rather than a fixed number (see
    // LogicConfiguration::zeroHpAsDead), so a caller holding an HP in hand asks
    // here instead of comparing on its own.
    [[nodiscard]] bool deadAtHp(int hp) const;
    [[nodiscard]] bool dead() const;
    [[nodiscard]] bool alive() const
    {
        return !dead();
    }

    // action checks
    [[nodiscard]] bool canBuyKnife() const;
    [[nodiscard]] bool canBuyHorse() const;
    [[nodiscard]] bool canSlash(const Player *to) const;
    // The HP this player is punished with for slashing from where it stands. A
    // slash is only punished outside the Village, and only when the rules ask
    // for it, so this is 0 where a slash is free. Which makes it the price a
    // slasher pays on top of whatever it hits, and the number a strategy has to
    // weigh before slashing.
    [[nodiscard]] int slashPunishHp() const;
    [[nodiscard]] bool canKick(const Player *to) const;
    [[nodiscard]] bool canMove(int toPlace) const;
    [[nodiscard]] bool canLetMove(const Player *to, int toPlace) const;

    // upgrade checks
    [[nodiscard]] int upgradeKnifeRemainingTimes() const;
    [[nodiscard]] int upgradeHorseRemainingTimes() const;
    [[nodiscard]] int upgradeMaxHpRemainingTimes() const;

    [[nodiscard]] bool canUpgradeKnife() const
    {
        return upgradeKnifeRemainingTimes() > 0;
    }
    [[nodiscard]] bool canUpgradeHorse() const
    {
        return upgradeHorseRemainingTimes() > 0;
    }
    [[nodiscard]] bool canUpgradeMaxHp() const
    {
        return upgradeMaxHpRemainingTimes() > 0;
    }

public slots: // NOLINT(readability-redundant-access-specifiers)
    // actions
    bool buyKnife();
    bool buyHorse();

    bool slash(Player *to);
    bool kick(Player *to);

    bool move(int toPlace);
    bool letMove(Player *to, int toPlace);

    bool doNothing();

    // upgrades
    bool upgradeKnife();
    bool upgradeHorse();
    bool upgradeMaxHp();

    void prepareForRoundStart(int seat);
    void resetUpgrades();

signals:
    void hasKnifeChanged(bool hasKnife, QPrivateSignal);
    void hasHorseChanged(bool hasHorse, QPrivateSignal);
    void hpChanged(int hp, QPrivateSignal);
    void placeChanged(int place, QPrivateSignal);
    void initialPlaceChanged(int initialPlace, QPrivateSignal);
    void knifeDamageChanged(int knifeDamage, QPrivateSignal);
    void horseDamageChanged(int horseDamage, QPrivateSignal);
    void maxHpChanged(int maxHp, QPrivateSignal);
    void upgradePointChanged(int upgradePoint, QPrivateSignal);

    void damaged(const Player *from, int damagePoint, Data::DamageReason reason, QPrivateSignal);
    void die(QPrivateSignal);

#ifndef DOXYGEN
private:
    friend struct p::PlayerP;
    const std::unique_ptr<p::PlayerP> d;
#endif
};

#ifndef DOXYGEN
} // namespace v0
inline namespace v1 {
using v0::Player;
}
#endif

} // namespace QMdmmCore

#endif // QMDMMPLAYER_H

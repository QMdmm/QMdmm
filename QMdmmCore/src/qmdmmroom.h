// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMROOM_H
#define QMDMMROOM_H

#include "qmdmmcoreglobal.h"

#include <QJsonObject>
#include <QList>
#include <QObject>

#include <cstdint>

QMDMM_EXPORT_NAME(QMdmmLogicConfiguration)
QMDMM_EXPORT_NAME(QMdmmRoom)

namespace QMdmmCore {

#ifndef DOXYGEN
namespace p {
struct RoomP;
}
#endif

#ifndef DOXYGEN
namespace v0 {
#endif

class Player;
class Logic;

class QMDMMCORE_EXPORT LogicConfiguration final : public QJsonObject
{
    Q_GADGET

    Q_PROPERTY(int initialKnifeDamage READ initialKnifeDamage WRITE setInitialKnifeDamage DESIGNABLE false FINAL)
    Q_PROPERTY(int maximumKnifeDamage READ maximumKnifeDamage WRITE setMaximumKnifeDamage DESIGNABLE false FINAL)
    Q_PROPERTY(int initialHorseDamage READ initialHorseDamage WRITE setInitialHorseDamage DESIGNABLE false FINAL)
    Q_PROPERTY(int maximumHorseDamage READ maximumHorseDamage WRITE setMaximumHorseDamage DESIGNABLE false FINAL)
    Q_PROPERTY(int initialMaxHp READ initialMaxHp WRITE setInitialMaxHp DESIGNABLE false FINAL)
    Q_PROPERTY(int maximumMaxHp READ maximumMaxHp WRITE setMaximumMaxHp DESIGNABLE false FINAL)
    Q_PROPERTY(int punishHpModifier READ punishHpModifier WRITE setPunishHpModifier DESIGNABLE false FINAL)
    Q_PROPERTY(LogicConfiguration::PunishHpRoundStrategy punishHpRoundStrategy READ punishHpRoundStrategy WRITE setPunishHpRoundStrategy DESIGNABLE false FINAL)
    Q_PROPERTY(bool zeroHpAsDead READ zeroHpAsDead WRITE setZeroHpAsDead DESIGNABLE false FINAL)
    Q_PROPERTY(bool enableLetMove READ enableLetMove WRITE setEnableLetMove DESIGNABLE false FINAL)
    Q_PROPERTY(bool canBuyOnlyInInitialCity READ canBuyOnlyInInitialCity WRITE setCanBuyOnlyInInitialCity DESIGNABLE false FINAL)

public:
    // The enclosing class carries QMDMMCORE_EXPORT, which is what exports these two; MSVC rejects the second marking (C2487).
    static const LogicConfiguration &defaults();
    static const LogicConfiguration &v1();

    enum PunishHpRoundStrategy : uint8_t
    {
        RoundDown,
        RoundToNearest45,
        RoundUp,
        PlusOne,
    };
    Q_ENUM(PunishHpRoundStrategy);

#ifdef Q_MOC_RUN
    Q_INVOKABLE LogicConfiguration();
    Q_INVOKABLE LogicConfiguration(const LogicConfiguration &);
#else
    using QJsonObject::QJsonObject;
#endif

    // standard configuration
    [[nodiscard]] int initialKnifeDamage() const;
    void setInitialKnifeDamage(int initialKnifeDamage);
    [[nodiscard]] int maximumKnifeDamage() const;
    void setMaximumKnifeDamage(int maximumKnifeDamage);
    [[nodiscard]] int initialHorseDamage() const;
    void setInitialHorseDamage(int initialHorseDamage);
    [[nodiscard]] int maximumHorseDamage() const;
    void setMaximumHorseDamage(int maximumHorseDamage);
    [[nodiscard]] int initialMaxHp() const;
    void setInitialMaxHp(int initialMaxHp);
    [[nodiscard]] int maximumMaxHp() const;
    void setMaximumMaxHp(int maximumMaxHp);

    // legacy experience
    [[nodiscard]] int punishHpModifier() const;
    void setPunishHpModifier(int punishHpModifier);
    [[nodiscard]] LogicConfiguration::PunishHpRoundStrategy punishHpRoundStrategy() const;
    void setPunishHpRoundStrategy(LogicConfiguration::PunishHpRoundStrategy punishHpRoundStrategy);
    [[nodiscard]] bool zeroHpAsDead() const;
    void setZeroHpAsDead(bool zeroHpAsDead);
    [[nodiscard]] bool enableLetMove() const;
    void setEnableLetMove(bool enableLetMove);
    [[nodiscard]] bool canBuyOnlyInInitialCity() const;
    void setCanBuyOnlyInInitialCity(bool canBuyOnlyInInitialCity);

    bool deserialize(const QJsonValue &value);

#ifndef DOXYGEN
private:
    using QJsonObject::operator=;
#endif
};

class QMDMMCORE_EXPORT Room final : public QObject
{
    Q_OBJECT

public:
    Q_DISABLE_COPY_MOVE(Room);

    explicit Room(LogicConfiguration logicConfiguration, QObject *parent = nullptr);
    ~Room() override;

    [[nodiscard]] const LogicConfiguration &logicConfiguration() const;
    void setLogicConfiguration(const LogicConfiguration &logicConfiguration);

    Player *addPlayer(const QString &playerName);
    bool removePlayer(const QString &playerName);

    [[nodiscard]] Player *player(const QString &playerName);
    [[nodiscard]] const Player *player(const QString &playerName) const;

    [[nodiscard]] QList<Player *> players();
    [[nodiscard]] QList<const Player *> players() const;
    [[nodiscard]] QStringList playerNames() const;

    [[nodiscard]] QList<Player *> alivePlayers();
    [[nodiscard]] QList<const Player *> alivePlayers() const;
    [[nodiscard]] QStringList alivePlayerNames() const;
    [[nodiscard]] int alivePlayersCount() const noexcept
    {
        return static_cast<int>(alivePlayers().size());
    }
    [[nodiscard]] bool isRoundOver() const noexcept
    {
        return alivePlayersCount() <= 1;
    }

    [[nodiscard]] bool isGameOver(QStringList *winnerPlayerNames = nullptr) const;

    void prepareForRoundStart();
    void resetUpgrades();

signals:
    void playerAdded(const QString &playerName, QPrivateSignal);
    void playerRemoved(const QString &playerName, QPrivateSignal);

#ifndef DOXYGEN
private:
    const std::unique_ptr<p::RoomP> d;
#endif
};

#ifndef DOXYGEN
} // namespace v0
inline namespace v1 {
using v0::LogicConfiguration;
using v0::Room;
} // namespace v1
#endif

} // namespace QMdmmCore

#endif // QMDMMROOM_H

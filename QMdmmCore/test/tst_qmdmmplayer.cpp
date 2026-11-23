#include "test.h"

#include <QMdmmCore/QMdmmPlayer>
#include <QMdmmCore/QMdmmRoom>

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmCore;

class tst_QMdmmPlayer : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmPlayer() = default;

    std::unique_ptr<Room> r;
    QPointer<Player> p1;
    QPointer<Player> p2;

private slots:
    // https://doc.qt.io/qt-5/qtest-overview.html#creating-a-test
    // called before each test case is run
    void init()
    {
        r.reset(new Room(LogicConfiguration::defaults(), this));
        p1 = r->addPlayer(u"test1"_s);
        p2 = r->addPlayer(u"test2"_s);
    }

    void QMdmmPlayerroom()
    {
        QCOMPARE(p1->room(), r.get());
        QCOMPARE(static_cast<const Player *>(p1)->room(), r.get());
    }

    void QMdmmPlayerhasKnife()
    {
        r->prepareForRoundStart();

        // case 1: initial state is has no knife
        {
            QVERIFY(!p1->hasKnife());
        }

        r->prepareForRoundStart();

        // case 2: set hasKnife = true -> emit hasKnifeChanged
        {
            QSignalSpy s(p1, &Player::hasKnifeChanged);
            p1->setHasKnife(true);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QVERIFY(s.first().first().toBool());
                QVERIFY(p1->hasKnife());
            }
        }

        r->prepareForRoundStart();

        // case 3: set hasKnife = false -> do not emit hasKnifeChanged
        {
            QSignalSpy s(p1, &Player::hasKnifeChanged);
            p1->setHasKnife(false);

            QCOMPARE(s.length(), 0);
            QVERIFY(!p1->hasKnife());
        }
    }

    void QMdmmPlayerhasHorse()
    {
        r->prepareForRoundStart();

        // case 1: initial state is has no horse
        {
            QVERIFY(!p1->hasHorse());
        }

        r->prepareForRoundStart();

        // case 2: set hasHorse = true -> emit hasHorseChanged
        {
            QSignalSpy s(p1, &Player::hasHorseChanged);
            p1->setHasHorse(true);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QVERIFY(s.first().first().toBool());
                QVERIFY(p1->hasHorse());
            }
        }

        r->prepareForRoundStart();

        // case 3: set hasHorse = false -> do not emit hasHorseChanged
        {
            QSignalSpy s(p1, &Player::hasHorseChanged);
            p1->setHasHorse(false);

            QCOMPARE(s.length(), 0);
            QVERIFY(!p1->hasHorse());
        }
    }

    // also the test function for dead() and alive(), and signal die()
    void QMdmmPlayerhp()
    {
        r->prepareForRoundStart();

        // case 1: initial state is hp == 10 (provided conf.initialMaxHp == 10, There is no upgrade for now!)
        {
            QCOMPARE(p1->hp(), 10);
            QVERIFY(p1->alive());
            QVERIFY(!p1->dead());
        }

        r->prepareForRoundStart();

        // case 2: set hp = 9 -> emit only hpChanged
        {
            QSignalSpy s(p1, &Player::hpChanged);
            QSignalSpy s2(p1, &Player::die);
            bool kills = false;
            p1->setHp(9, &kills);

            QCOMPARE(s.length(), 1);
            QCOMPARE(s2.length(), 0);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 9);
                QCOMPARE(p1->hp(), 9);
            }
            if (s2.length() == 0) {
                QVERIFY(!kills);
                QVERIFY(p1->alive());
                QVERIFY(!p1->dead());
            }
        }

        r->prepareForRoundStart();

        // case 3: set hp = 0 (provided conf.zeroHpAsDead == true) -> emit both hpChanged and die
        // damaging corpse do not trigger die
        {
            QSignalSpy s(p1, &Player::hpChanged);
            QSignalSpy s2(p1, &Player::die);
            bool kills = false;
            p1->setHp(0, &kills);

            QCOMPARE(s.length(), 1);
            QCOMPARE(s2.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 0);
                QCOMPARE(p1->hp(), 0);
            }
            if (s2.length() > 0) {
                QVERIFY(kills);
                QVERIFY(!p1->alive());
                QVERIFY(p1->dead());
            }

            bool kills2 = false;
            p1->setHp(-1, &kills2);

            QCOMPARE(s.length(), 2);
            QCOMPARE(s2.length(), 1);
            QVERIFY(!kills2);
            QVERIFY(!p1->alive());
            QVERIFY(p1->dead());
        }

        r->prepareForRoundStart();

        // case 4: set hp = 10 -> emit no signal
        {
            QSignalSpy s(p1, &Player::hpChanged);
            QSignalSpy s2(p1, &Player::die);
            bool kills = false;
            p1->setHp(10, &kills);

            QCOMPARE(s.length(), 0);
            QCOMPARE(s2.length(), 0);
            QCOMPARE(p1->hp(), 10);
            QVERIFY(!kills);
            QVERIFY(p1->alive());
            QVERIFY(!p1->dead());
        }

        LogicConfiguration conf = LogicConfiguration::defaults();
        conf.setZeroHpAsDead(false);

        r.reset(new Room(conf, this));

        p1 = r->addPlayer(u"test1"_s);
        r->prepareForRoundStart();

        // case 5: zeroHpAsDead = false
        {
            QSignalSpy s(p1, &Player::hpChanged);
            QSignalSpy s2(p1, &Player::die);
            bool kills = false;
            p1->setHp(0, &kills);

            QCOMPARE(s.length(), 1);
            QCOMPARE(s2.length(), 0);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 0);
                QCOMPARE(p1->hp(), 0);
            }
            if (s2.length() == 0) {
                QVERIFY(!kills);
                QVERIFY(p1->alive());
                QVERIFY(!p1->dead());
            }

            s.clear();
            s2.clear();

            bool kills2 = false;
            p1->setHp(-1, &kills2);

            QCOMPARE(s.length(), 1);
            QCOMPARE(s2.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), -1);
                QCOMPARE(p1->hp(), -1);
            }
            if (s2.length() > 0) {
                QVERIFY(kills2);
                QVERIFY(!p1->alive());
                QVERIFY(p1->dead());
            }

            s.clear();
            s2.clear();

            bool kills3 = false;
            p1->setHp(-2, &kills3);

            QCOMPARE(s.length(), 1);
            QCOMPARE(s2.length(), 0);
            QVERIFY(!kills3);
            QVERIFY(!p1->alive());
            QVERIFY(p1->dead());
        }
    }

    void QMdmmPlayerplace()
    {
        r->prepareForRoundStart();

        // case 1: initial state is at place 1
        {
            QCOMPARE(p1->place(), 1);
        }

        r->prepareForRoundStart();

        // case 2: set place = 0 -> emit placeChanged
        {
            QSignalSpy s(p1, &Player::placeChanged);
            p1->setPlace(Data::Village);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), Data::Village);
                QCOMPARE(p1->place(), Data::Village);
            }
        }

        r->prepareForRoundStart();

        // case 3: set place = 1 -> do not emit placeChanged
        {
            QSignalSpy s(p1, &Player::placeChanged);
            p1->setPlace(1);

            QCOMPARE(s.length(), 0);
            QCOMPARE(p1->place(), 1);
        }
    }

    void QMdmmPlayerinitialPlace()
    {
        r->prepareForRoundStart();

        // case 1: initial state is at initialPlace 1
        {
            QCOMPARE(p1->initialPlace(), 1);
        }

        r->prepareForRoundStart();

        // case 2: set initialPlace = 0 -> emit initialPlaceChanged
        {
            QSignalSpy s(p1, &Player::initialPlaceChanged);
            p1->setInitialPlace(Data::Village);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), Data::Village);
                QCOMPARE(p1->initialPlace(), Data::Village);
            }
        }

        r->prepareForRoundStart();

        // case 3: set initialPlace = 1 -> do not emit initialPlaceChanged
        {
            QSignalSpy s(p1, &Player::initialPlaceChanged);
            p1->setInitialPlace(1);

            QCOMPARE(s.length(), 0);
            QCOMPARE(p1->initialPlace(), 1);
        }
    }

    void QMdmmPlayerknifeDamage()
    {
        r->resetUpgrades();

        // case 1: initial state is at knifeDamage 1 (provided conf.initialKnifeDamage = 1, There is no upgrade for now!)
        {
            QCOMPARE(p1->knifeDamage(), 1);
        }

        r->resetUpgrades();

        // case 2: set knifeDamage = 2 -> emit knifeDamageChanged
        {
            QSignalSpy s(p1, &Player::knifeDamageChanged);
            p1->setKnifeDamage(2);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 2);
                QCOMPARE(p1->knifeDamage(), 2);
            }
        }

        r->resetUpgrades();

        // case 3: set knifeDamage = 1 -> do not emit knifeDamageChanged
        {
            QSignalSpy s(p1, &Player::knifeDamageChanged);
            p1->setKnifeDamage(1);

            QCOMPARE(s.length(), 0);
            QCOMPARE(p1->knifeDamage(), 1);
        }
    }

    void QMdmmPlayerhorseDamage()
    {
        r->resetUpgrades();

        // case 1: initial state is at horseDamage 2 (provided conf.initialHorseDamage = 2, There is no upgrade for now!)
        {
            QCOMPARE(p1->horseDamage(), 2);
        }

        r->resetUpgrades();

        // case 2: set horseDamage = 3 -> emit horseDamageChanged
        {
            QSignalSpy s(p1, &Player::horseDamageChanged);
            p1->setHorseDamage(3);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 3);
                QCOMPARE(p1->horseDamage(), 3);
            }
        }

        r->resetUpgrades();

        // case 3: set horseDamage = 2 -> do not emit horseDamageChanged
        {
            QSignalSpy s(p1, &Player::horseDamageChanged);
            p1->setHorseDamage(2);

            QCOMPARE(s.length(), 0);
            QCOMPARE(p1->horseDamage(), 2);
        }
    }

    void QMdmmPlayermaxHp()
    {
        r->resetUpgrades();

        // case 1: initial state is at maxHp 10 (provided conf.initialMaxHp = 10, There is no upgrade for now!)
        {
            QCOMPARE(p1->maxHp(), 10);
        }

        r->resetUpgrades();

        // case 2: set maxHp = 11 -> emit maxHpChanged
        {
            QSignalSpy s(p1, &Player::maxHpChanged);
            p1->setMaxHp(11);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 11);
                QCOMPARE(p1->maxHp(), 11);
            }
        }

        r->resetUpgrades();

        // case 3: set maxHp = 10 -> do not emit maxHpChanged
        {
            QSignalSpy s(p1, &Player::maxHpChanged);
            p1->setMaxHp(10);

            QCOMPARE(s.length(), 0);
            QCOMPARE(p1->maxHp(), 10);
        }
    }

    void QMdmmPlayerupgradePoint()
    {
        r->resetUpgrades();

        // case 1: initial state is at upgradePoint 0 (There is no upgrade for now!)
        {
            QCOMPARE(p1->maxHp(), 10);
        }

        r->resetUpgrades();

        // case 2: set upgradePoint = 1 -> emit upgradePointChanged
        {
            QSignalSpy s(p1, &Player::upgradePointChanged);
            p1->setUpgradePoint(1);

            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 1);
                QCOMPARE(p1->upgradePoint(), 1);
            }
        }

        r->resetUpgrades();

        // case 3: set upgradePoint = 0 -> do not emit upgradePointChanged
        {
            QSignalSpy s(p1, &Player::upgradePointChanged);
            p1->setUpgradePoint(0);

            QCOMPARE(s.length(), 0);
            QCOMPARE(p1->upgradePoint(), 0);
        }
    }

    void QMdmmPlayerkillCapsUpgradePoint()
    {
        r->prepareForRoundStart();

        // Max out every stat so no upgrade remains.
        p1->setKnifeDamage(r->logicConfiguration().maximumKnifeDamage());
        p1->setHorseDamage(r->logicConfiguration().maximumHorseDamage());
        p1->setMaxHp(r->logicConfiguration().maximumMaxHp());
        p1->setHasKnife(true);
        p1->setPlace(p2->place());
        p2->setHp(1);

        QSignalSpy s(p1, &Player::upgradePointChanged);

        QVERIFY(p1->slash(p2));
        // A kill grants nothing when the killer has no upgrades left to spend.
        QCOMPARE(s.length(), 0);
        QCOMPARE(p1->upgradePoint(), 0);
    }

    void QMdmmPlayercanBuyKnife()
    {
        r->prepareForRoundStart();

        // case 1: initial state - can buy knife in any city
        {
            QVERIFY(p1->canBuyKnife());
            p1->setPlace(p2->place());
            QVERIFY(p1->canBuyKnife());
        }

        r->prepareForRoundStart();

        // case 2: dead player - cannot buy knife
        {
            p1->setHp(-1);
            QVERIFY(!p1->canBuyKnife());
        }

        r->prepareForRoundStart();

        // case 3: has knife - cannot buy knife
        {
            p1->setHasKnife(true);
            QVERIFY(!p1->canBuyKnife());
        }

        r->prepareForRoundStart();

        // case 4: in the Village - cannot buy knife
        {
            p1->setPlace(Data::Village);
            QVERIFY(!p1->canBuyKnife());
        }

        LogicConfiguration conf = LogicConfiguration::defaults();
        conf.setCanBuyOnlyInInitialCity(true);

        r.reset(new Room(conf, this));

        p1 = r->addPlayer(u"test1"_s);
        p2 = r->addPlayer(u"test2"_s);
        r->prepareForRoundStart();

        // case 5: canBuyOnlyInInitialCity = true
        {
            p1->setPlace(p2->place());
            QVERIFY(!p1->canBuyKnife());
        }
    }

    void QMdmmPlayercanBuyHorse()
    {
        r->prepareForRoundStart();

        // case 1: initial state - can buy knife in any city
        {
            QVERIFY(p1->canBuyHorse());
            p1->setPlace(p2->place());
            QVERIFY(p1->canBuyHorse());
        }

        r->prepareForRoundStart();

        // case 2: dead player - cannot buy knife
        {
            p1->setHp(-1);
            QVERIFY(!p1->canBuyHorse());
        }

        r->prepareForRoundStart();

        // case 3: has knife - cannot buy knife
        {
            p1->setHasHorse(true);
            QVERIFY(!p1->canBuyHorse());
        }

        r->prepareForRoundStart();

        // case 4: in the Village - cannot buy knife
        {
            p1->setPlace(Data::Village);
            QVERIFY(!p1->canBuyHorse());
        }

        LogicConfiguration conf = LogicConfiguration::defaults();
        conf.setCanBuyOnlyInInitialCity(true);

        r.reset(new Room(conf, this));

        p1 = r->addPlayer(u"test1"_s);
        p2 = r->addPlayer(u"test2"_s);
        r->prepareForRoundStart();

        // case 5: canBuyOnlyInInitialCity = true
        {
            p1->setPlace(p2->place());
            QVERIFY(!p1->canBuyHorse());
        }
    }

    void QMdmmPlayercanSlash()
    {
        r->prepareForRoundStart();

        // case 1: can't slash if place is different
        {
            p1->setHasKnife(true);

            QVERIFY(!p1->canSlash(p2));
        }

        r->prepareForRoundStart();

        // case 2: can't slash without knife
        {
            p1->setPlace(p2->place());

            QVERIFY(!p1->canSlash(p2));
        }

        r->prepareForRoundStart();

        // case 3: dead player can't slash, can't slash dead player
        {
            p1->setHasKnife(true);
            p1->setPlace(p2->place());
            p2->setHp(-1);

            QVERIFY(!p1->canSlash(p2));
            QVERIFY(!p2->canSlash(p1));
        }

        r->prepareForRoundStart();

        // case 4: can't slash himself / herself
        {
            p1->setHasKnife(true);

            QVERIFY(!p1->canSlash(p1));
        }

        r->prepareForRoundStart();

        // case 5: can slash with knife, both healthy state, and same place
        {
            p1->setHasKnife(true);
            p1->setPlace(p2->place());

            QVERIFY(p1->canSlash(p2));
        }
    }

    void QMdmmPlayercanKick()
    {
        r->prepareForRoundStart();

        // case 1: can't kick if place is different
        {
            p1->setHasHorse(true);

            QVERIFY(!p1->canKick(p2));
        }

        r->prepareForRoundStart();

        // case 2: can't kick without horse
        {
            p1->setPlace(p2->place());

            QVERIFY(!p1->canKick(p2));
        }

        r->prepareForRoundStart();

        // case 3: dead player can't kick, can't kick dead player
        {
            p1->setHasHorse(true);
            p1->setPlace(p2->place());
            p2->setHp(-1);

            QVERIFY(!p1->canKick(p2));
            QVERIFY(!p2->canKick(p1));
        }

        r->prepareForRoundStart();

        // case 4: can't kick himself / herself
        {
            p1->setHasHorse(true);

            QVERIFY(!p1->canKick(p1));
        }

        r->prepareForRoundStart();

        // case 5: can kick with horse, both healthy state, and same place in city
        {
            p1->setHasHorse(true);
            p1->setPlace(p2->place());

            QVERIFY(p1->canKick(p2));
        }

        r->prepareForRoundStart();

        // case 6: can't kick in the Village
        {
            p1->setHasHorse(true);
            p1->setPlace(Data::Village);
            p2->setPlace(Data::Village);

            QVERIFY(!p1->canKick(p2));
        }
    }

    void QMdmmPlayercanMove()
    {
        r->prepareForRoundStart();

        // case 1: can move to the Village when in city
        {
            QVERIFY(p1->canMove(Data::Village));
        }

        r->prepareForRoundStart();

        // case 2: can't move to another city when in a city
        {
            QVERIFY(!p1->canMove(p2->place()));
        }

        r->prepareForRoundStart();

        // case 3: can move to any city when in the Village
        {
            p1->setPlace(Data::Village);

            QVERIFY(p1->canMove(p1->initialPlace()));
            QVERIFY(p1->canMove(p2->place()));
        }

        r->prepareForRoundStart();

        // case 4: dead player can't move
        {
            p1->setHp(-1);
            QVERIFY(!p1->canMove(Data::Village));
        }

        r->prepareForRoundStart();

        // case 5: can't move to current place
        {
            QVERIFY(!p1->canMove(p1->place()));
        }
    }

    void QMdmmPlayercanLetMove()
    {
        r->prepareForRoundStart();

        // coverage for this == to
        {
            QVERIFY(p1->canLetMove(p1, Data::Village));
        }

        r->prepareForRoundStart();

        // case 1: dead players can't be push / pull to / from
        {
            p1->setPlace(p2->place());
            p2->setHp(-1);

            QVERIFY(!p1->canLetMove(p2, Data::Village));
            QVERIFY(!p2->canLetMove(p1, Data::Village));
        }

        r->prepareForRoundStart();

        // case 2: push stuff - if p1 and p2 place is same
        {
            p1->setPlace(p2->place());

            QVERIFY(p1->canLetMove(p2, Data::Village));
            QVERIFY(!p1->canLetMove(p2, p2->initialPlace()));
            QVERIFY(!p1->canLetMove(p2, p1->initialPlace()));
        }

        r->prepareForRoundStart();

        // case 3: pull stuff - if p1 and p2 place is adjacent
        {
            p2->setPlace(Data::Village);
            QVERIFY(p1->canLetMove(p2, p1->place()));
            QVERIFY(!p1->canLetMove(p2, Data::Village));
            QVERIFY(!p1->canLetMove(p2, p2->initialPlace()));
        }

        r->prepareForRoundStart();

        // case 4: ??? - if p1 and p2 place is not same nor adjacent
        {
            QVERIFY(!p1->canLetMove(p2, p1->place()));
            QVERIFY(!p1->canLetMove(p2, Data::Village));
            QVERIFY(!p1->canLetMove(p2, p2->initialPlace()));
        }

        LogicConfiguration conf = LogicConfiguration::defaults();
        conf.setEnableLetMove(false);

        r.reset(new Room(conf, this));

        p1 = r->addPlayer(u"test1"_s);
        p2 = r->addPlayer(u"test2"_s);
        r->prepareForRoundStart();

        // case 5: disabled let move
        {
            p1->setPlace(p2->place());

            QVERIFY(!p1->canLetMove(p2, Data::Village));
        }
    }

    // A place is one the room has: the Village, and a city within the seats the room fills. The
    // Village counts as adjacent to every city number, so the last city has to be read off the
    // seats rather than off where the player stands.
    void QMdmmPlayeraPlaceMustBeOneTheRoomHas()
    {
        r->prepareForRoundStart();

        const int lastCity = std::max(p1->initialPlace(), p2->initialPlace());

        p1->setPlace(Data::Village);
        p2->setPlace(Data::Village);

        // the last city is reachable from here, so the refusal of a move to a city past it, and
        // of a move that takes a room-mate there, answer to the place and not to the player
        // standing in the Village
        QVERIFY(p1->canMove(lastCity));
        QVERIFY(!p1->canMove(lastCity + 1));

        QVERIFY(p1->canLetMove(p2, lastCity));
        QVERIFY(!p1->canLetMove(p2, lastCity + 1));
    }

    void QMdmmPlayerupgradeKnifeRemainingTimes()
    {
        r->resetUpgrades();

        // case 1
        {
            QCOMPARE(p1->upgradeKnifeRemainingTimes(), 9);
            QVERIFY(p1->canUpgradeKnife());
        }

        r->resetUpgrades();

        // case 2
        {
            p1->setKnifeDamage(10);

            QCOMPARE(p1->upgradeKnifeRemainingTimes(), 0);
            QVERIFY(!p1->canUpgradeKnife());
        }
    }

    void QMdmmPlayerupgradeHorseRemainingTimes()
    {
        r->resetUpgrades();

        // case 1
        {
            QCOMPARE(p1->upgradeHorseRemainingTimes(), 8);
            QVERIFY(p1->canUpgradeHorse());
        }

        r->resetUpgrades();

        // case 2
        {
            p1->setHorseDamage(10);

            QCOMPARE(p1->upgradeHorseRemainingTimes(), 0);
            QVERIFY(!p1->canUpgradeHorse());
        }
    }

    void QMdmmPlayerupgradeMaxHpRemainingTimes()
    {
        r->resetUpgrades();

        // case 1
        {
            QCOMPARE(p1->upgradeMaxHpRemainingTimes(), 10);
            QVERIFY(p1->canUpgradeMaxHp());
        }

        r->resetUpgrades();

        // case 2
        {
            p1->setMaxHp(20);

            QCOMPARE(p1->upgradeMaxHpRemainingTimes(), 0);
            QVERIFY(!p1->canUpgradeMaxHp());
        }
    }

    void QMdmmPlayerbuyKnife()
    {
        r->prepareForRoundStart();

        // case 1
        {
            QSignalSpy s(p1, &Player::hasKnifeChanged);

            QVERIFY(p1->buyKnife());
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QVERIFY(s.first().first().toBool());
                QVERIFY(p1->hasKnife());
            }
        }

        r->prepareForRoundStart();

        // case 2
        {
            p1->setHasKnife(true);

            QSignalSpy s(p1, &Player::hasKnifeChanged);

            QVERIFY(!p1->buyKnife());
            QCOMPARE(s.length(), 0);
        }
    }

    void QMdmmPlayerbuyHorse()
    {
        r->prepareForRoundStart();

        // case 1
        {
            QSignalSpy s(p1, &Player::hasHorseChanged);

            QVERIFY(p1->buyHorse());
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QVERIFY(s.first().first().toBool());
                QVERIFY(p1->hasHorse());
            }
        }

        r->prepareForRoundStart();

        // case 2
        {
            p1->setHasHorse(true);

            QSignalSpy s(p1, &Player::hasHorseChanged);

            QVERIFY(!p1->buyHorse());
            QCOMPARE(s.length(), 0);
        }
    }

    void QMdmmPlayerslash()
    {
        r->prepareForRoundStart();

        // case 1
        {
            p1->setHasKnife(true);

            QSignalSpy s(p2, &Player::hpChanged);

            QVERIFY(!p1->slash(p2));
            QCOMPARE(s.length(), 0);
        }

        r->prepareForRoundStart();

        // case 2
        {
            p1->setHasKnife(true);
            p2->setPlace(Data::Village);
            p1->setPlace(p2->place());

            QSignalSpy s(p2, &Player::hpChanged);
            QSignalSpy s2(p1, &Player::hpChanged);

            QVERIFY(p1->slash(p2));
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 9);
                QCOMPARE(p2->hp(), 9);
            }
            QCOMPARE(s2.length(), 0);
        }

        r->prepareForRoundStart();

        // case 3: slash kills
        {
            p1->setHasKnife(true);
            p2->setPlace(Data::Village);
            p1->setPlace(p2->place());
            p2->setHp(1);

            QSignalSpy s(p2, &Player::hpChanged);
            QSignalSpy s2(p2, &Player::die);
            QSignalSpy s3(p1, &Player::upgradePointChanged);
            QSignalSpy s4(p1, &Player::die);

            QVERIFY(p1->slash(p2));
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 0);
                QCOMPARE(p2->hp(), 0);
            }
            QCOMPARE(s2.length(), 1);
            QCOMPARE(s3.length(), 1);
            if (s3.length() > 0) {
                QCOMPARE(s3.first().first().toInt(), 1);
                QCOMPARE(p1->upgradePoint(), 1);
            }
            QCOMPARE(s4.length(), 0);
        }

        r->prepareForRoundStart();

        // case 4: punish HP kills
        {
            p1->setHasKnife(true);
            p1->setPlace(p2->place());
            p1->setHp(1);

            QSignalSpy s(p2, &Player::hpChanged);
            QSignalSpy s2(p1, &Player::die);
            QSignalSpy s3(p2, &Player::upgradePointChanged);
            QSignalSpy s4(p1, &Player::hpChanged);
            QSignalSpy s5(p2, &Player::die);

            QVERIFY(p1->slash(p2));
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 9);
                QCOMPARE(p2->hp(), 9);
            }
            QCOMPARE(s4.length(), 1);
            if (s4.length() > 0) {
                QCOMPARE(s4.first().first().toInt(), -4);
                QCOMPARE(p1->hp(), -4);
            }
            QCOMPARE(s2.length(), 1);
            QCOMPARE(s3.length(), 1);
            if (s3.length() > 0) {
                QCOMPARE(s3.first().first().toInt(), 1);
                QCOMPARE(p2->upgradePoint(), 1);
            }
            QCOMPARE(s5.length(), 0);
        }

        // Details of punish HP is tested in QMdmmPlayerslash2
    }
    void QMdmmPlayerslash2_data()
    {
        QTest::addColumn<int>("punishHpModifier");
        QTest::addColumn<LogicConfiguration::PunishHpRoundStrategy>("punishHpRoundStrategy");
        QTest::addColumn<int>("maxHp");
        QTest::addColumn<int>("punishedHp");

        QTest::newRow("disable") << 0 << LogicConfiguration::RoundDown << 10 << 10;

        // some typical numbers
        QTest::newRow("roundDown-0") << 3 << LogicConfiguration::RoundDown << 9 << 6;
        QTest::newRow("roundDown-1") << 3 << LogicConfiguration::RoundDown << 10 << 7;
        QTest::newRow("roundDown-2") << 3 << LogicConfiguration::RoundDown << 11 << 8;
        QTest::newRow("roundDown-3") << 3 << LogicConfiguration::RoundDown << 12 << 8;
        QTest::newRow("roundToNearest45-0") << 3 << LogicConfiguration::RoundToNearest45 << 9 << 6;
        QTest::newRow("roundToNearest45-1") << 3 << LogicConfiguration::RoundToNearest45 << 10 << 7;
        QTest::newRow("roundToNearest45-2") << 3 << LogicConfiguration::RoundToNearest45 << 11 << 7;
        QTest::newRow("roundToNearest45-3") << 3 << LogicConfiguration::RoundToNearest45 << 12 << 8;
        QTest::newRow("roundUp-0") << 3 << LogicConfiguration::RoundUp << 9 << 6;
        QTest::newRow("roundUp-1") << 3 << LogicConfiguration::RoundUp << 10 << 6;
        QTest::newRow("roundUp-2") << 3 << LogicConfiguration::RoundUp << 11 << 7;
        QTest::newRow("roundUp-3") << 3 << LogicConfiguration::RoundUp << 12 << 8;
        QTest::newRow("plusOne-0") << 3 << LogicConfiguration::PlusOne << 9 << 5;
        QTest::newRow("plusOne-1") << 3 << LogicConfiguration::PlusOne << 10 << 6;
        QTest::newRow("plusOne-2") << 3 << LogicConfiguration::PlusOne << 11 << 7;
        QTest::newRow("plusOne-3") << 3 << LogicConfiguration::PlusOne << 12 << 7;

        // some big numbers
        QTest::newRow("roundDown-4") << 15 << LogicConfiguration::RoundDown << 105 << 98;
        QTest::newRow("roundDown-5") << 15 << LogicConfiguration::RoundDown << 106 << 99;
        QTest::newRow("roundDown-6") << 15 << LogicConfiguration::RoundDown << 112 << 105;
        QTest::newRow("roundDown-7") << 15 << LogicConfiguration::RoundDown << 113 << 106;
        QTest::newRow("roundDown-8") << 15 << LogicConfiguration::RoundDown << 119 << 112;
        QTest::newRow("roundDown-9") << 15 << LogicConfiguration::RoundDown << 120 << 112;
        QTest::newRow("roundToNearest45-4") << 15 << LogicConfiguration::RoundToNearest45 << 105 << 98;
        QTest::newRow("roundToNearest45-5") << 15 << LogicConfiguration::RoundToNearest45 << 106 << 99;
        QTest::newRow("roundToNearest45-6") << 15 << LogicConfiguration::RoundToNearest45 << 112 << 105;
        QTest::newRow("roundToNearest45-7") << 15 << LogicConfiguration::RoundToNearest45 << 113 << 105;
        QTest::newRow("roundToNearest45-8") << 15 << LogicConfiguration::RoundToNearest45 << 119 << 111;
        QTest::newRow("roundToNearest45-9") << 15 << LogicConfiguration::RoundToNearest45 << 120 << 112;
        QTest::newRow("roundUp-4") << 15 << LogicConfiguration::RoundUp << 105 << 98;
        QTest::newRow("roundUp-5") << 15 << LogicConfiguration::RoundUp << 106 << 98;
        QTest::newRow("roundUp-6") << 15 << LogicConfiguration::RoundUp << 112 << 104;
        QTest::newRow("roundUp-7") << 15 << LogicConfiguration::RoundUp << 113 << 105;
        QTest::newRow("roundUp-8") << 15 << LogicConfiguration::RoundUp << 119 << 111;
        QTest::newRow("roundUp-9") << 15 << LogicConfiguration::RoundUp << 120 << 112;
        QTest::newRow("plusOne-4") << 15 << LogicConfiguration::PlusOne << 105 << 97;
        QTest::newRow("plusOne-5") << 15 << LogicConfiguration::PlusOne << 106 << 98;
        QTest::newRow("plusOne-6") << 15 << LogicConfiguration::PlusOne << 112 << 104;
        QTest::newRow("plusOne-7") << 15 << LogicConfiguration::PlusOne << 113 << 105;
        QTest::newRow("plusOne-8") << 15 << LogicConfiguration::PlusOne << 119 << 111;
        QTest::newRow("plusOne-9") << 15 << LogicConfiguration::PlusOne << 120 << 111;

        QTest::newRow("punishHp0") << 15 << LogicConfiguration::RoundDown << 10 << 10;
        QTest::newRow("default") << 3 << LogicConfiguration::PunishHpRoundStrategy(100) << 10 << 7;
    }
    void QMdmmPlayerslash2()
    {
        QFETCH(int, punishHpModifier);
        QFETCH(LogicConfiguration::PunishHpRoundStrategy, punishHpRoundStrategy);
        QFETCH(int, maxHp);
        QFETCH(int, punishedHp);

        LogicConfiguration conf = LogicConfiguration::defaults();
        conf.setPunishHpModifier(punishHpModifier);
        conf.setPunishHpRoundStrategy(punishHpRoundStrategy);

        r.reset(new Room(conf, this));

        p1 = r->addPlayer(u"test1"_s);
        p1->setMaxHp(maxHp);
        p2 = r->addPlayer(u"test2"_s);

        r->prepareForRoundStart();

        p1->setHasKnife(true);
        p1->setPlace(p2->place());

        QSignalSpy s(p2, &Player::hpChanged);
        QSignalSpy s2(p1, &Player::hpChanged);

        QVERIFY(p1->slash(p2));
        QCOMPARE(s.length(), 1);
        if (s.length() > 0) {
            QCOMPARE(s.first().first().toInt(), 9);
            QCOMPARE(p2->hp(), 9);
        }

        if (maxHp == punishedHp) {
            QCOMPARE(s2.length(), 0);
        } else {
            QCOMPARE(s2.length(), 1);
            if (s2.length() > 0) {
                QCOMPARE(s2.first().first().toInt(), punishedHp);
                QCOMPARE(p1->hp(), punishedHp);
            }
        }
    }

    void QMdmmPlayerkick()
    {
        r->prepareForRoundStart();

        // case 1: kick in different place / i.e. canKick returns false
        {
            p1->setHasHorse(true);

            QSignalSpy s(p2, &Player::hpChanged);

            QVERIFY(!p1->kick(p2));
            QCOMPARE(s.length(), 0);
        }

        r->prepareForRoundStart();

        // case 2: normal case
        {
            p1->setHasHorse(true);
            p1->setPlace(p2->place());

            QSignalSpy s(p2, &Player::hpChanged);
            QSignalSpy s2(p2, &Player::placeChanged);

            QVERIFY(p1->kick(p2));
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 8);
                QCOMPARE(p2->hp(), 8);
            }
            QCOMPARE(s2.length(), 1);
            if (s2.length() > 0) {
                QCOMPARE(s2.first().first().toInt(), Data::Village);
                QCOMPARE(p2->place(), Data::Village);
            }
        }

        r->prepareForRoundStart();

        // case 3: kick kills
        {
            p1->setHasHorse(true);
            p1->setPlace(p2->place());
            p2->setHp(1);

            QSignalSpy s(p2, &Player::hpChanged);
            QSignalSpy s2(p2, &Player::die);
            QSignalSpy s3(p1, &Player::upgradePointChanged);
            QSignalSpy s4(p2, &Player::placeChanged);

            QVERIFY(p1->kick(p2));
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), -1);
                QCOMPARE(p2->hp(), -1);
            }
            QCOMPARE(s2.length(), 1);
            QCOMPARE(s3.length(), 1);
            if (s3.length() > 0) {
                QCOMPARE(s3.first().first().toInt(), 1);
                QCOMPARE(p1->upgradePoint(), 1);
            }
            QCOMPARE(s4.length(), 0);
        }
    }

    void QMdmmPlayermove()
    {
        r->prepareForRoundStart();

        {
            QSignalSpy s(p1, &Player::placeChanged);

            QVERIFY(!p1->move(p2->place()));
            QCOMPARE(s.length(), 0);
        }

        r->prepareForRoundStart();

        {
            QSignalSpy s(p1, &Player::placeChanged);

            QVERIFY(p1->move(Data::Village));
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), Data::Village);
                QCOMPARE(p1->place(), Data::Village);
            }
        }
    }

    void QMdmmPlayerletMove()
    {
        r->prepareForRoundStart();

        {
            QSignalSpy s(p2, &Player::placeChanged);

            QVERIFY(!p1->letMove(p2, p1->place()));
            QCOMPARE(s.length(), 0);
        }

        r->prepareForRoundStart();

        {
            p1->setPlace(Data::Village);
            QSignalSpy s(p2, &Player::placeChanged);

            QVERIFY(p1->letMove(p2, Data::Village));
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), Data::Village);
                QCOMPARE(p2->place(), Data::Village);
            }
        }
    }

    void QMdmmPlayerdoNothing()
    {
        r->prepareForRoundStart();
        QVERIFY(p1->doNothing());
    }

    void QMdmmPlayerupgradeKnife()
    {
        r->resetUpgrades();

        {
            QSignalSpy s(p1, &Player::knifeDamageChanged);

            QVERIFY(p1->upgradeKnife());
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 2);
                QCOMPARE(p1->knifeDamage(), 2);
            }
        }

        r->resetUpgrades();

        {
            p1->setKnifeDamage(10);

            QSignalSpy s(p1, &Player::knifeDamageChanged);

            QVERIFY(!p1->upgradeKnife());
            QCOMPARE(s.length(), 0);
        }
    }

    void QMdmmPlayerupgradeHorse()
    {
        r->resetUpgrades();

        {
            QSignalSpy s(p1, &Player::horseDamageChanged);

            QVERIFY(p1->upgradeHorse());
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 3);
                QCOMPARE(p1->horseDamage(), 3);
            }
        }

        r->resetUpgrades();

        {
            p1->setHorseDamage(10);

            QSignalSpy s(p1, &Player::horseDamageChanged);

            QVERIFY(!p1->upgradeHorse());
            QCOMPARE(s.length(), 0);
        }
    }

    void QMdmmPlayerupgradeMaxHp()
    {
        r->resetUpgrades();

        {
            QSignalSpy s(p1, &Player::maxHpChanged);

            QVERIFY(p1->upgradeMaxHp());
            QCOMPARE(s.length(), 1);
            if (s.length() > 0) {
                QCOMPARE(s.first().first().toInt(), 11);
                QCOMPARE(p1->maxHp(), 11);
            }
        }

        r->resetUpgrades();

        {
            p1->setMaxHp(20);

            QSignalSpy s(p1, &Player::maxHpChanged);

            QVERIFY(!p1->upgradeMaxHp());
            QCOMPARE(s.length(), 0);
        }
    }

    void QMdmmPlayerupgradeBudgetOverflow()
    {
        // A player whose upgrade budget exceeds a single stat's remaining capacity must not
        // over-allocate that stat. `6189f0d` fixed the e2e smoke bot, which looped
        // `canUpgradeKnife()` on an unchanged mirror player and appended the same (now-maxed)
        // item past its maximum; the Core primitive it relies on -- `upgradeXxx()` returning
        // false once the stat is maxed -- has no unit test guarding this budget-overflow flow.
        r->resetUpgrades();

        // Max out knife via repeated upgradeKnife() calls (initial 1, maximum 10 -> 9 left).
        const int knifeLeft = p1->upgradeKnifeRemainingTimes();
        QCOMPARE(knifeLeft, 9);
        for (int i = 0; i < knifeLeft; ++i) {
            QVERIFY(p1->upgradeKnife());
        }
        QCOMPARE(p1->knifeDamage(), p1->room()->logicConfiguration().maximumKnifeDamage());

        // Budget still remains, but knife is maxed: a further knife upgrade is refused rather
        // than silently over-allocated past the maximum.
        QVERIFY(!p1->canUpgradeKnife());
        QVERIFY(!p1->upgradeKnife());
        QCOMPARE(p1->knifeDamage(), p1->room()->logicConfiguration().maximumKnifeDamage());

        // The remaining budget flows to horse, which must also cap at its own maximum.
        const int horseLeft = p1->upgradeHorseRemainingTimes();
        QCOMPARE(horseLeft, 8);
        for (int i = 0; i < horseLeft; ++i) {
            QVERIFY(p1->upgradeHorse());
        }
        QCOMPARE(p1->horseDamage(), p1->room()->logicConfiguration().maximumHorseDamage());

        QVERIFY(!p1->canUpgradeHorse());
        QVERIFY(!p1->upgradeHorse());
        QCOMPARE(p1->horseDamage(), p1->room()->logicConfiguration().maximumHorseDamage());
    }
};

namespace {
RegisterTestObject<tst_QMdmmPlayer> _;
}
#include "tst_qmdmmplayer.moc"

// NOLINTEND

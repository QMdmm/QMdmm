#include "qmdmmcoreglobal.h"
#include "qmdmmlogic.h"
#include "qmdmmroom.h"
#include "qmdmmsettings.h"
#include "test.h"

#include <QMdmmCore/QMdmmCoreGlobal>

#include <QHash>
#include <QMetaEnum>
#include <QString>
#include <QTest>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmCore;
using namespace Qt::StringLiterals;

class tst_QMdmmCore : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmCore() = default;

private slots:
    void QMdmmDataisPlaceAdjacent_data()
    {
        QTest::addColumn<int>("p1");
        QTest::addColumn<int>("p2");
        QTest::addColumn<bool>("result");

        QTest::newRow("nc-nc") << 1 << 2 << false;
        QTest::newRow("c-c") << (int)Data::Village << (int)Data::Village << false;
        QTest::newRow("nc-c") << 1 << (int)Data::Village << true;
        QTest::newRow("c-nc") << (int)Data::Village << 2 << true;
    }
    void QMdmmDataisPlaceAdjacent()
    {
        QFETCH(int, p1);
        QFETCH(int, p2);
        QFETCH(bool, result);

        bool r = Data::isPlaceAdjacent(p1, p2);
        QCOMPARE(r, result);
    }

    void QMdmmDatarockPaperScissorsWinners_data()
    {
        typedef QHash<QString, Data::RockPaperScissors> JudgeHash;

        QTest::addColumn<JudgeHash>("judgers");
        QTest::addColumn<QStringList>("result");

        QTest::newRow("tie-allsame") << JudgeHash {
            std::make_pair(u"1"_s, Data::Rock),
            std::make_pair(u"2"_s, Data::Rock),
            std::make_pair(u"3"_s, Data::Rock),
            std::make_pair(u"4"_s, Data::Rock),
        } << QStringList {};
        QTest::newRow("tie-alldiff") << JudgeHash {
            std::make_pair(u"1"_s, Data::Rock),
            std::make_pair(u"2"_s, Data::Scissors),
            std::make_pair(u"3"_s, Data::Paper),
            std::make_pair(u"4"_s, Data::Rock),
        } << QStringList {};
        QTest::newRow("rock-vs-scissors") << JudgeHash {
            std::make_pair(u"1"_s, Data::Rock),
            std::make_pair(u"2"_s, Data::Scissors),
            std::make_pair(u"3"_s, Data::Rock),
            std::make_pair(u"4"_s, Data::Scissors),
        } << QStringList {
            u"1"_s,
            u"1"_s,
            u"3"_s,
            u"3"_s,
        };
        QTest::newRow("paper-vs-rock") << JudgeHash {
            std::make_pair(u"1"_s, Data::Rock),
            std::make_pair(u"2"_s, Data::Paper),
            std::make_pair(u"3"_s, Data::Paper),
            std::make_pair(u"4"_s, Data::Paper),
        } << QStringList {
            u"2"_s,
            u"3"_s,
            u"4"_s,
        };
    }
    void QMdmmDatarockPaperScissorsWinners()
    {
        typedef QHash<QString, Data::RockPaperScissors> JudgeHash;

        QFETCH(JudgeHash, judgers);
        QFETCH(QStringList, result);

        QStringList r = Data::rockPaperScissorsWinners(judgers);
        foreach (const QString &a, result)
            r.removeOne(a);

        QCOMPARE(r, QStringList {});
    }

    void QMdmmGlobalversion()
    {
        QVersionNumber r = Global::version();
        QCOMPARE(r, QVersionNumber::fromString(u"" QMDMM_VERSION ""_s));
    }

    void QMdmmUtilitieslist2Set()
    {
        const int l1[] = {1, 2, 3, 4, 5, 2, 3, 4, 5, 6};
        QSet<int> s {1, 2, 3, 4, 5, 6};

        QSet<int> r = Utilities::list2Set(l1);
        QCOMPARE(r, s);

        std::list<int> l2(std::begin(l1), std::end(l1));
        r = Utilities::list2Set(l2);
        QCOMPARE(r, s);

        QList<int> l3(std::begin(l1), std::end(l1));
        r = Utilities::list2Set(l3);
        QCOMPARE(r, s);

        std::array l4 = std::to_array(l1);
        r = Utilities::list2Set(l4);
        QCOMPARE(r, s);

        r = Utilities::list2Set<std::initializer_list<int>>({1, 2, 3, 4, 5, 2, 3, 4, 5, 6});
        QCOMPARE(r, s);
    }

    void QMdmmUtilitiesenumList2VariantList1()
    {
        QList<Data::RockPaperScissors> l {Data::Rock, Data::Scissors, Data::Paper, Data::Rock};
        QVariantList s {static_cast<int>(Data::Rock), static_cast<int>(Data::Scissors), static_cast<int>(Data::Paper), static_cast<int>(Data::Rock)};

        QVariantList r = Utilities::enumList2VariantList(l);
        QCOMPARE(r, s);
    }

    void QMdmmUtilitiesenumList2VariantList2()
    {
        QList<Data::AgentState> l {Data::StateOnlineBot, Data::StateOnlineTrust, Data::StateOffline};
        QVariantList s {static_cast<int>(Data::AgentState(Data::StateOnlineBot)), static_cast<int>(Data::AgentState(Data::StateOnlineTrust)),
                        static_cast<int>(Data::AgentState(Data::StateOffline))};

        QVariantList r = Utilities::enumList2VariantList(l);
        QCOMPARE(r, s);
    }

    void QMdmmUtilitiesintList2VariantList()
    {
        QList<int> l {1, 2, 3, 4, 5, 6};
        QVariantList s {1, 2, 3, 4, 5, 6};

        QVariantList r = Utilities::intList2VariantList(l);
        QCOMPARE(r, s);
    }

    void QMdmmUtilitiesvariantList2IntList()
    {
        QVariantList l {6, 7, 8, 9, 10, 11};
        QList<int> s {6, 7, 8, 9, 10, 11};

        QList<int> r = Utilities::variantList2IntList(l);
        QCOMPARE(r, s);
    }

    void QMdmmUtilitiesstringList2VariantList()
    {
        QStringList l {u"Fs"_s, u"u"_s, u"0413"_s};
        QVariantList s {u"Fs"_s, u"u"_s, u"0413"_s};

        QVariantList r = Utilities::stringList2VariantList(l);
        QCOMPARE(r, s);
    }

    void QMdmmUtilitiesvariantList2StringList()
    {
        QVariantList l {u"3140"_s, u"u"_s, u"sF"_s};
        QStringList s {u"3140"_s, u"u"_s, u"sF"_s};

        QStringList r = Utilities::variantList2StringList(l);
        QCOMPARE(r, s);
    }

    // Every enum this library declares is meant to be readable through QMetaEnum under the name
    // it is declared with: that is how the Data namespace is reached from QML (the GUI hands QML
    // its staticMetaObject), how the debug stream spells a value, and how a name is turned back
    // into one. Losing the registration does not slip through -- fromType() stops compiling, the
    // static assert names the macro -- so what is left to guard is the content. The reflection is
    // generated from the declaration, so a member inserted or reordered is reflected just as
    // faithfully as before while every reader that indexes by value quietly answers differently.
    // The counts below are that reading, together with the numbers the wire already depends on.
    void QMdmmCoreenumsReflectTheirDeclarations()
    {
        const QMetaEnum place = QMetaEnum::fromType<Data::Place>();
        QVERIFY(place.isValid());
        QCOMPARE(place.keyCount(), 1);

        const QMetaEnum damageReason = QMetaEnum::fromType<Data::DamageReason>();
        QVERIFY(damageReason.isValid());
        QCOMPARE(damageReason.keyCount(), 4);

        const QMetaEnum rockPaperScissors = QMetaEnum::fromType<Data::RockPaperScissors>();
        QVERIFY(rockPaperScissors.isValid());
        QCOMPARE(rockPaperScissors.keyCount(), 3);
        // The wire carries the value, so the numbers are the contract -- see the enum's own note
        // about the historical encoding.
        QCOMPARE(rockPaperScissors.keyToValue("Rock"), 0);
        QCOMPARE(rockPaperScissors.keyToValue("Scissors"), 1);
        QCOMPARE(rockPaperScissors.keyToValue("Paper"), 2);

        const QMetaEnum action = QMetaEnum::fromType<Data::Action>();
        QVERIFY(action.isValid());
        QCOMPARE(action.keyCount(), 7);

        const QMetaEnum upgradeItem = QMetaEnum::fromType<Data::UpgradeItem>();
        QVERIFY(upgradeItem.isValid());
        QCOMPARE(upgradeItem.keyCount(), 3);

        const QMetaEnum agentState = QMetaEnum::fromType<Data::AgentState>();
        QVERIFY(agentState.isValid());
        QVERIFY(agentState.isFlag());
        QCOMPARE(agentState.keyCount(), 8);
        QCOMPARE(agentState.keyToValue("StateOnlineBot"), static_cast<int>(Data::StateOnlineBot));

        const QMetaEnum logicState = QMetaEnum::fromType<Logic::State>();
        QVERIFY(logicState.isValid());
        QCOMPARE(logicState.keyCount(), 6);

        const QMetaEnum instance = QMetaEnum::fromType<Settings::Instance>();
        QVERIFY(instance.isValid());
        QCOMPARE(instance.keyCount(), 3);

        const QMetaEnum punishHpRoundStrategy = QMetaEnum::fromType<LogicConfiguration::PunishHpRoundStrategy>();
        QVERIFY(punishHpRoundStrategy.isValid());
        QCOMPARE(punishHpRoundStrategy.keyCount(), 4);
    }
};

namespace {
RegisterTestObject<tst_QMdmmCore> _;
}
#include "tst_qmdmmcore.moc"

// NOLINTEND

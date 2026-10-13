#include "test.h"

#include <QMdmmCore/QMdmmPlayer>
#include <QMdmmCore/QMdmmRoom>

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmCore;

class tst_QMdmmLogicConfiguration : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmLogicConfiguration() = default;

private slots:

#define TEST_CONFIGURATION(valueName, ValueName, defaultValue, v1value, value) \
    void QMdmmLogicConfiguration##valueName()                                  \
    {                                                                          \
        {                                                                      \
            LogicConfiguration c;                                              \
            QCOMPARE(c.valueName(), (defaultValue));                           \
        }                                                                      \
        {                                                                      \
            LogicConfiguration c = LogicConfiguration::v1();                   \
            QCOMPARE(c.valueName(), (v1value));                                \
        }                                                                      \
        {                                                                      \
            LogicConfiguration c = LogicConfiguration::defaults();             \
            QCOMPARE(c.valueName(), (defaultValue));                           \
            c.set##ValueName(value);                                           \
            QCOMPARE(c.valueName(), (value));                                  \
        }                                                                      \
    }

    TEST_CONFIGURATION(initialKnifeDamage, InitialKnifeDamage, 1, 1, 5)
    TEST_CONFIGURATION(maximumKnifeDamage, MaximumKnifeDamage, 10, 3, 30)
    TEST_CONFIGURATION(initialHorseDamage, InitialHorseDamage, 2, 3, 6)
    TEST_CONFIGURATION(maximumHorseDamage, MaximumHorseDamage, 10, 5, 31)
    TEST_CONFIGURATION(initialMaxHp, InitialMaxHp, 10, 7, 20)
    TEST_CONFIGURATION(maximumMaxHp, MaximumMaxHp, 20, 7, 50)
    TEST_CONFIGURATION(punishHpModifier, PunishHpModifier, 2, 0, 5)
    TEST_CONFIGURATION(punishHpRoundStrategy, PunishHpRoundStrategy, LogicConfiguration::RoundToNearest45, LogicConfiguration::RoundToNearest45, LogicConfiguration::RoundDown)
    TEST_CONFIGURATION(zeroHpAsDead, ZeroHpAsDead, true, false, false)
    TEST_CONFIGURATION(enableLetMove, EnableLetMove, true, false, false)
    TEST_CONFIGURATION(canBuyOnlyInInitialCity, CanBuyOnlyInInitialCity, false, false, true)

#undef TEST_CONFIGURATION

    void QMdmmLogicConfigurationdeserialize_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("result");

        struct ConfigurationTestTriplet
        {
            QString key;
            QJsonValue validValue;
            QJsonValue invalidValue;
        };

        static const QList<ConfigurationTestTriplet> testTriplets {
            {u"initialKnifeDamage"_s, 2, true},
            {u"maximumKnifeDamage"_s, 2, true},
            {u"initialHorseDamage"_s, 2, true},
            {u"maximumHorseDamage"_s, 2, true},
            {u"initialMaxHp"_s, 2, true},
            {u"maximumMaxHp"_s, 2, true},
            {u"punishHpModifier"_s, 2, true},
            {u"punishHpRoundStrategy"_s, 2, true},
            {u"zeroHpAsDead"_s, false, u"what?"_s},
            {u"enableLetMove"_s, false, u"what?"_s},
            {u"canBuyOnlyInInitialCity"_s, false, u"what?"_s},
        };

        QJsonObject ob;

        foreach (const ConfigurationTestTriplet &t, testTriplets) {
            ob.insert(t.key, t.invalidValue);
            QTest::newRow((t.key + u"-invalid"_s).toUtf8().constData()) << QJsonValue(ob) << false;
            ob.insert(t.key, t.validValue);
        }

        QTest::newRow("valid") << QJsonValue(ob) << true;
        QTest::newRow("notObject") << QJsonValue(QJsonValue::Null) << false;

        // Value-level validation (defensive programming): negative / fraction / NaN /
        // enum-out-of-range / initial-exceeds-maximum must all be rejected.
        QJsonObject validOb {
            {u"initialKnifeDamage"_s, 1},
            {u"maximumKnifeDamage"_s, 10},
            {u"initialHorseDamage"_s, 2},
            {u"maximumHorseDamage"_s, 10},
            {u"initialMaxHp"_s, 10},
            {u"maximumMaxHp"_s, 20},
            {u"punishHpModifier"_s, 2},
            {u"punishHpRoundStrategy"_s, static_cast<int>(LogicConfiguration::RoundToNearest45)},
            {u"zeroHpAsDead"_s, true},
            {u"enableLetMove"_s, true},
            {u"canBuyOnlyInInitialCity"_s, false},
        };

        {
            QJsonObject negativeOb = validOb;
            negativeOb.insert(u"initialKnifeDamage"_s, -1);
            QTest::newRow("negative") << QJsonValue(negativeOb) << false;
        }
        {
            QJsonObject fractionOb = validOb;
            fractionOb.insert(u"initialKnifeDamage"_s, 1.5);
            QTest::newRow("fraction") << QJsonValue(fractionOb) << false;
        }
        {
            QJsonObject nanOb = validOb;
            nanOb.insert(u"initialKnifeDamage"_s, std::numeric_limits<double>::quiet_NaN());
            QTest::newRow("nan") << QJsonValue(nanOb) << false;
        }
        {
            QJsonObject enumOb = validOb;
            enumOb.insert(u"punishHpRoundStrategy"_s, 4);
            QTest::newRow("enumOutOfRange") << QJsonValue(enumOb) << false;
        }
        {
            QJsonObject knifeOb = validOb;
            knifeOb.insert(u"initialKnifeDamage"_s, 11);
            QTest::newRow("initialKnifeGreaterThanMaximum") << QJsonValue(knifeOb) << false;
        }
        {
            QJsonObject horseOb = validOb;
            horseOb.insert(u"initialHorseDamage"_s, 11);
            QTest::newRow("initialHorseGreaterThanMaximum") << QJsonValue(horseOb) << false;
        }
        {
            QJsonObject maxHpOb = validOb;
            maxHpOb.insert(u"initialMaxHp"_s, 21);
            QTest::newRow("initialMaxHpGreaterThanMaximum") << QJsonValue(maxHpOb) << false;
        }
    }
    void QMdmmLogicConfigurationdeserialize()
    {
        QFETCH(QJsonValue, value);
        QFETCH(bool, result);

        LogicConfiguration conf;
        bool r = conf.deserialize(value);
        QCOMPARE(r, result);
        if (r) {
            QCOMPARE(QJsonValue(QJsonObject(conf)), value);
        }
    }

    void QMdmmLogicConfigurationdeserializeAbsentKeysFallBackToDefaults()
    {
        // An empty object is a valid configuration: every absent key falls back to its default value
        // (a server without any explicit logic configuration item broadcasts exactly this), and the
        // deserialization does not materialize the absent keys.
        const QJsonObject emptyOb;
        LogicConfiguration conf;
        QVERIFY(conf.deserialize(QJsonValue(emptyOb)));
        QCOMPARE(QJsonValue(QJsonObject(conf)), QJsonValue(emptyOb));

        const LogicConfiguration &defaults = LogicConfiguration::defaults();
        QCOMPARE(conf.initialKnifeDamage(), defaults.initialKnifeDamage());
        QCOMPARE(conf.maximumKnifeDamage(), defaults.maximumKnifeDamage());
        QCOMPARE(conf.initialHorseDamage(), defaults.initialHorseDamage());
        QCOMPARE(conf.maximumHorseDamage(), defaults.maximumHorseDamage());
        QCOMPARE(conf.initialMaxHp(), defaults.initialMaxHp());
        QCOMPARE(conf.maximumMaxHp(), defaults.maximumMaxHp());
        QCOMPARE(conf.punishHpModifier(), defaults.punishHpModifier());
        QCOMPARE(conf.punishHpRoundStrategy(), defaults.punishHpRoundStrategy());
        QCOMPARE(conf.zeroHpAsDead(), defaults.zeroHpAsDead());
        QCOMPARE(conf.enableLetMove(), defaults.enableLetMove());
        QCOMPARE(conf.canBuyOnlyInInitialCity(), defaults.canBuyOnlyInInitialCity());
    }

    void QMdmmLogicConfigurationdeserializePresentKeysWinOverDefaults()
    {
        // Present keys take effect, absent ones still fall back to their default value.
        const QJsonObject partialOb {
            {u"maximumMaxHp"_s, 15},
            {u"enableLetMove"_s, false},
        };

        LogicConfiguration conf;
        QVERIFY(conf.deserialize(QJsonValue(partialOb)));
        QCOMPARE(QJsonValue(QJsonObject(conf)), QJsonValue(partialOb));

        QCOMPARE(conf.maximumMaxHp(), 15);
        QCOMPARE(conf.enableLetMove(), false);

        const LogicConfiguration &defaults = LogicConfiguration::defaults();
        QCOMPARE(conf.initialKnifeDamage(), defaults.initialKnifeDamage());
        QCOMPARE(conf.maximumKnifeDamage(), defaults.maximumKnifeDamage());
        QCOMPARE(conf.initialHorseDamage(), defaults.initialHorseDamage());
        QCOMPARE(conf.maximumHorseDamage(), defaults.maximumHorseDamage());
        QCOMPARE(conf.initialMaxHp(), defaults.initialMaxHp());
        QCOMPARE(conf.punishHpModifier(), defaults.punishHpModifier());
        QCOMPARE(conf.punishHpRoundStrategy(), defaults.punishHpRoundStrategy());
        QCOMPARE(conf.zeroHpAsDead(), defaults.zeroHpAsDead());
        QCOMPARE(conf.canBuyOnlyInInitialCity(), defaults.canBuyOnlyInInitialCity());
    }
};

namespace {
// Not _a: the void ** parameter moc gives the qt_static_metacall() it generates is named _a,
// and MSVC's /W4 reports that parameter as hiding this global (C4459).
RegisterTestObject<tst_QMdmmLogicConfiguration> _;
} // namespace
#include "tst_qmdmmlogicconfiguration.moc"

// NOLINTEND

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QMdmmServer>

#include <QTest>

#include <limits>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmNetworking;
using namespace Qt::StringLiterals;

class tst_QMdmmServerConfiguration : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmServerConfiguration() = default;

private slots:

    void QMdmmServerConfigurationdefaults()
    {
        ServerConfiguration c;
        QCOMPARE(c.tcpEnabled(), true);
        QCOMPARE(c.tcpPort(), (uint16_t)6366);
        QCOMPARE(c.localEnabled(), true);
        QCOMPARE(c.localSocketName(), u"QMdmm"_s);
        QCOMPARE(c.websocketEnabled(), true);
        QCOMPARE(c.websocketName(), u"QMdmm"_s);
        QCOMPARE(c.websocketPort(), (uint16_t)6367);
        QCOMPARE(c.playerNumPerRoom(), 3);
        QCOMPARE(c.requestTimeout(), 20);
    }

    void QMdmmServerConfigurationsetters()
    {
        ServerConfiguration c;
        c.setTcpEnabled(false);
        c.setTcpPort(7000);
        c.setLocalEnabled(false);
        c.setLocalSocketName(u"test-local"_s);
        c.setWebsocketEnabled(false);
        c.setWebsocketName(u"test-websocket"_s);
        c.setWebsocketPort(8000);
        c.setPlayerNumPerRoom(8);
        c.setRequestTimeout(30);

        QCOMPARE(c.tcpEnabled(), false);
        QCOMPARE(c.tcpPort(), (uint16_t)7000);
        QCOMPARE(c.localEnabled(), false);
        QCOMPARE(c.localSocketName(), u"test-local"_s);
        QCOMPARE(c.websocketEnabled(), false);
        QCOMPARE(c.websocketName(), u"test-websocket"_s);
        QCOMPARE(c.websocketPort(), (uint16_t)8000);
        QCOMPARE(c.playerNumPerRoom(), 8);
        QCOMPARE(c.requestTimeout(), 30);
    }

    void QMdmmServerConfigurationdeserialize_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("result");

        const QJsonObject validOb {
            {u"tcpEnabled"_s, true}, //
            {u"tcpPort"_s, 6366}, //
            {u"localEnabled"_s, true}, //
            {u"localSocketName"_s, u"QMdmm"_s}, //
            {u"websocketEnabled"_s, true}, //
            {u"websocketName"_s, u"QMdmm"_s}, //
            {u"websocketPort"_s, 6367}, //
            {u"playerNumPerRoom"_s, 3}, //
            {u"requestTimeout"_s, 20}, //
        };

        QTest::newRow("valid") << QJsonValue(validOb) << true;
        QTest::newRow("notObject") << QJsonValue(QJsonValue::Null) << false;

        // Wrong type: boolean field holding a string.
        {
            QJsonObject ob = validOb;
            ob.insert(u"tcpEnabled"_s, u"true"_s);
            QTest::newRow("boolWrongType") << QJsonValue(ob) << false;
        }
        // Wrong type: string field holding a number.
        {
            QJsonObject ob = validOb;
            ob.insert(u"localSocketName"_s, 123);
            QTest::newRow("stringWrongType") << QJsonValue(ob) << false;
        }
        // Fractional port.
        {
            QJsonObject ob = validOb;
            ob.insert(u"tcpPort"_s, 6366.5);
            QTest::newRow("fraction") << QJsonValue(ob) << false;
        }
        // Negative port.
        {
            QJsonObject ob = validOb;
            ob.insert(u"tcpPort"_s, -1);
            QTest::newRow("negativePort") << QJsonValue(ob) << false;
        }
        // Port 0 is reserved.
        {
            QJsonObject ob = validOb;
            ob.insert(u"tcpPort"_s, 0);
            QTest::newRow("portZero") << QJsonValue(ob) << false;
        }
        // Port out of uint16_t range (would silently truncate on read).
        {
            QJsonObject ob = validOb;
            ob.insert(u"websocketPort"_s, 70000);
            QTest::newRow("portOverflow") << QJsonValue(ob) << false;
        }
        // playerNumPerRoom below the minimum of 2.
        {
            QJsonObject ob = validOb;
            ob.insert(u"playerNumPerRoom"_s, 1);
            QTest::newRow("playersBelowMinimum") << QJsonValue(ob) << false;
        }
        // requestTimeout below 15 but not 0.
        {
            QJsonObject ob = validOb;
            ob.insert(u"requestTimeout"_s, 1);
            QTest::newRow("timeoutBelowMinimum") << QJsonValue(ob) << false;
        }
        // Negative requestTimeout.
        {
            QJsonObject ob = validOb;
            ob.insert(u"requestTimeout"_s, -1);
            QTest::newRow("negativeTimeout") << QJsonValue(ob) << false;
        }
        // NaN requestTimeout.
        {
            QJsonObject ob = validOb;
            ob.insert(u"requestTimeout"_s, std::numeric_limits<double>::quiet_NaN());
            QTest::newRow("nanTimeout") << QJsonValue(ob) << false;
        }
        // requestTimeout = 0 (no timeout) is valid.
        {
            QJsonObject ob = validOb;
            ob.insert(u"requestTimeout"_s, 0);
            QTest::newRow("timeoutZero") << QJsonValue(ob) << true;
        }
        // requestTimeout = 15 (minimum explicit timeout) is valid.
        {
            QJsonObject ob = validOb;
            ob.insert(u"requestTimeout"_s, 15);
            QTest::newRow("timeoutMinimum") << QJsonValue(ob) << true;
        }
    }

    void QMdmmServerConfigurationdeserialize()
    {
        QFETCH(QJsonValue, value);
        QFETCH(bool, result);

        ServerConfiguration conf;
        bool r = conf.deserialize(value);
        QCOMPARE(r, result);
        if (r) {
            QCOMPARE(QJsonValue(QJsonObject(conf)), value);
        }
    }

    void QMdmmServerConfigurationdeserializeAbsentKeysFallBackToDefaults()
    {
        // An empty object is a valid configuration: every absent key falls back to its default value
        // (this is what a server configuration with no explicit item looks like), and the
        // deserialization does not materialize the absent keys.
        const QJsonObject emptyOb;
        ServerConfiguration conf;
        QVERIFY(conf.deserialize(QJsonValue(emptyOb)));
        QCOMPARE(QJsonValue(QJsonObject(conf)), QJsonValue(emptyOb));

        const ServerConfiguration &defaults = ServerConfiguration::defaults();
        QCOMPARE(conf.tcpEnabled(), defaults.tcpEnabled());
        QCOMPARE(conf.tcpPort(), defaults.tcpPort());
        QCOMPARE(conf.localEnabled(), defaults.localEnabled());
        QCOMPARE(conf.localSocketName(), defaults.localSocketName());
        QCOMPARE(conf.websocketEnabled(), defaults.websocketEnabled());
        QCOMPARE(conf.websocketName(), defaults.websocketName());
        QCOMPARE(conf.websocketPort(), defaults.websocketPort());
        QCOMPARE(conf.playerNumPerRoom(), defaults.playerNumPerRoom());
        QCOMPARE(conf.requestTimeout(), defaults.requestTimeout());
    }

    void QMdmmServerConfigurationdeserializeIgnoresUnknownKeys()
    {
        const QJsonObject ob {
            {u"tcpEnabled"_s, true}, //
            {u"tcpPort"_s, 6366}, //
            {u"localEnabled"_s, true}, //
            {u"localSocketName"_s, u"QMdmm"_s}, //
            {u"websocketEnabled"_s, true}, //
            {u"websocketName"_s, u"QMdmm"_s}, //
            {u"websocketPort"_s, 6367}, //
            {u"playerNumPerRoom"_s, 3}, //
            {u"requestTimeout"_s, 20}, //
            {u"unknownKey"_s, 123}, //
        };

        ServerConfiguration conf;
        QVERIFY(conf.deserialize(QJsonValue(ob)));
        QVERIFY(!conf.contains(u"unknownKey"_s));
    }
};

namespace {
RegisterTestObject<tst_QMdmmServerConfiguration> _a;
} // namespace
#include "tst_qmdmmserverconfiguration.moc"

// NOLINTEND

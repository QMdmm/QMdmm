#include "test.h"

#include <QMdmmCore/QMdmmProtocol>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

// The fixtures below read the protocol header at run time, so its path has to come
// from the build; see QMdmmCore/test/CMakeLists.txt.
#ifndef QMDMM_PROTOCOL_HEADER
#error "QMDMM_PROTOCOL_HEADER must name QMdmmCore/src/qmdmmprotocol.h; see QMdmmCore/test/CMakeLists.txt"
#endif

// The case at the end reads the wire contract beside the header, for the same reason.
#ifndef QMDMM_PROTOCOL_DOC
#error "QMDMM_PROTOCOL_DOC must name doc/protocol.md; see QMdmmCore/test/CMakeLists.txt"
#endif

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmCore;

namespace {

// One legal packet per enumerator a packet can carry, so that an enumerator added to
// the header without a fixture here is a test failure instead of a silent gap.
struct ProtocolIdFixture
{
    const char *enumName;
    const char *enumerator;
    Protocol::PacketType type;
    Protocol::RequestId requestId;
    Protocol::NotifyId notifyId;
};

// A sentinel (RequestInvalid / NotifyInvalid / TypeInvalid) or a mask marker
// (NotifyFromServerMask and friends) names no packet of its own, so the table carries
// no row for it -- and the header scan does not ask for one either.
bool isWireId(const QString &enumerator)
{
    return !enumerator.endsWith(u"Invalid"_s) && !enumerator.contains(u"Mask"_s);
}

const ProtocolIdFixture protocolIdFixtures[] = {
    {"RequestId", "RequestRockPaperScissors", Protocol::TypeRequest, Protocol::RequestRockPaperScissors, Protocol::NotifyInvalid},
    {"RequestId", "RequestActionOrder", Protocol::TypeRequest, Protocol::RequestActionOrder, Protocol::NotifyInvalid},
    {"RequestId", "RequestAction", Protocol::TypeRequest, Protocol::RequestAction, Protocol::NotifyInvalid},
    {"RequestId", "RequestUpgrade", Protocol::TypeRequest, Protocol::RequestUpgrade, Protocol::NotifyInvalid},

    {"NotifyId", "NotifyPongServer", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyPongServer},
    {"NotifyId", "NotifyVersion", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyVersion},
    {"NotifyId", "NotifyLogicConfiguration", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyLogicConfiguration},
    {"NotifyId", "NotifyAgentStateChanged", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyAgentStateChanged},
    {"NotifyId", "NotifyPlayerAdded", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyPlayerAdded},
    {"NotifyId", "NotifyPlayerRemoved", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyPlayerRemoved},
    {"NotifyId", "NotifyGameStart", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyGameStart},
    {"NotifyId", "NotifyRoundStart", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyRoundStart},
    {"NotifyId", "NotifyRockPaperScissors", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyRockPaperScissors},
    {"NotifyId", "NotifyActionOrder", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyActionOrder},
    {"NotifyId", "NotifyAction", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyAction},
    {"NotifyId", "NotifyRoundOver", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyRoundOver},
    {"NotifyId", "NotifyUpgrade", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyUpgrade},
    {"NotifyId", "NotifyGameOver", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyGameOver},
    {"NotifyId", "NotifySpoken", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifySpoken},
    {"NotifyId", "NotifyOperated", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyOperated},
    {"NotifyId", "NotifyPingServer", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyPingServer},
    {"NotifyId", "NotifySignIn", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifySignIn},
    {"NotifyId", "NotifyObserve", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyObserve},
    {"NotifyId", "NotifyManagedChanged", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyManagedChanged},
    {"NotifyId", "NotifySpeak", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifySpeak},
    {"NotifyId", "NotifyOperate", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyOperate},

    {"PacketType", "TypeRequest", Protocol::TypeRequest, Protocol::RequestRockPaperScissors, Protocol::NotifyInvalid},
    {"PacketType", "TypeReply", Protocol::TypeReply, Protocol::RequestRockPaperScissors, Protocol::NotifyInvalid},
    {"PacketType", "TypeNotify", Protocol::TypeNotify, Protocol::RequestInvalid, Protocol::NotifyLogicConfiguration},
};

// The comparison in QMdmmProtocolenumeratorsMatchTheFixtures runs at run time: neither
// std::map nor QHash is constexpr, so it cannot be a static_assert yet. C++26 is where a
// constexpr std::map arrives, so the standard version carries the tripwire -- the day
// this tree moves to C++26 the build stops here and asks for the comparison to be lifted
// into the assertion (the QHash half never can be, being implicitly shared).
static_assert(__cplusplus < 202400L, "a constexpr std::map is available: lift the fixture comparison into a static_assert");

// The enumerators of every enum the header declares, grouped by enum name. They come
// from the header rather than from a second list here: noticing an enumerator that has
// no fixture is the whole point, and a copy of the list could not do that.
QMap<QString, QStringList> enumeratorsInProtocolHeader(const QByteArray &source)
{
    // The bodies of these enums carry braces inside their line comments (e.g. "object
    // { string playerName: int rps }"), so the comments go first.
    static const QRegularExpression lineComment(u"//[^\n]*"_s);
    QString text = QString::fromUtf8(source);
    text.remove(lineComment);

    QMap<QString, QStringList> ret;
    static const QRegularExpression enumDeclaration(u"enum\\s+(\\w+)\\s*:\\s*\\w+\\s*\\{([^}]*)\\}"_s);
    QRegularExpressionMatchIterator matches = enumDeclaration.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();

        QStringList enumerators;
        const QStringList pieces = match.captured(2).split(u',', Qt::SkipEmptyParts);
        for (const QString &piece : pieces) {
            // The last field is the whitespace between the trailing comma and the
            // closing brace; it is not an enumerator.
            const QString enumerator = piece.section(u'=', 0, 0).trimmed();
            if (!enumerator.isEmpty())
                enumerators.append(enumerator);
        }

        ret.insert(match.captured(1), enumerators);
    }

    return ret;
}

QStringList wireIdsOf(const QStringList &enumerators)
{
    QStringList ret;
    for (const QString &enumerator : enumerators) {
        if (isWireId(enumerator))
            ret.append(enumerator);
    }
    ret.sort();

    return ret;
}

// The id tables in doc/protocol.md sit between these two comments, and the check at the
// end of this file reads the ids out of exactly that text: a table that drifts away from
// the markers stops being read rather than passing quietly, and an empty reading fails
// against the header. The markers are documentation's own -- a reader of the page never
// sees them, HTML comments being stripped by every renderer that gets one.
constexpr char ID_TABLE_BEGIN[] = "<!-- protocol-ids: begin -->";
constexpr char ID_TABLE_END[] = "<!-- protocol-ids: end -->";

// Every id the tables list, in file order, one entry per row. A row is
// "| `AnId` | ... |": the header row and the rule carry no backtick in their first cell,
// so this reads ids and nothing else.
QStringList idsInProtocolDocument(const QByteArray &source)
{
    static const QRegularExpression row(u"^\\|\\s*`([A-Za-z0-9_]+)`\\s*\\|"_s);

    QStringList ret;
    bool inATable = false;
    const QStringList lines = QString::fromUtf8(source).split(u'\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed == QString::fromLatin1(ID_TABLE_BEGIN)) {
            inATable = true;
            continue;
        }
        if (trimmed == QString::fromLatin1(ID_TABLE_END)) {
            inATable = false;
            continue;
        }
        if (!inATable)
            continue;

        const QRegularExpressionMatch match = row.match(trimmed);
        if (match.hasMatch())
            ret.append(match.captured(1));
    }

    return ret;
}

} // namespace

class tst_QMdmmProtocol : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmProtocol() = default;

private slots:
    // coverage for QMdmmPacketData::QMdmmPacketData
    void QMdmmPacketDataCopy()
    {
        QJsonObject ob;
        p::PacketDataP e = ob;
        QJsonObject ob2;
        ob2.insert(u"test"_s, QJsonValue());
        e = ob2;
    }

    void QMdmmProtocolprotocolVersion()
    {
        int r = Protocol::version();
        QCOMPARE(r, 0);
    }

    void QMdmmPackettype()
    {
        // case 1
        {
            Packet p;
            Protocol::PacketType t = p.type();
            QCOMPARE(t, Protocol::TypeInvalid);
        }

        // case 2
        {
            Packet p(Protocol::TypeRequest, Protocol::RequestRockPaperScissors, {});
            Protocol::PacketType t = p.type();
            QCOMPARE(t, Protocol::TypeRequest);
        }

        // case 3
        {
            Packet p(Protocol::NotifyVersion, {});
            Protocol::PacketType t = p.type();
            QCOMPARE(t, Protocol::TypeNotify);
        }
    }

    void QMdmmPacketrequestId()
    {
        // case 1
        {
            Packet p;
            Protocol::RequestId t = p.requestId();
            QCOMPARE(t, Protocol::RequestInvalid);
        }

        // case 2
        {
            Packet p(Protocol::TypeRequest, Protocol::RequestRockPaperScissors, {});
            Protocol::RequestId t = p.requestId();
            QCOMPARE(t, Protocol::RequestRockPaperScissors);
        }

        // case 3
        {
            Packet p(Protocol::NotifyVersion, {});
            Protocol::RequestId t = p.requestId();
            QCOMPARE(t, Protocol::RequestInvalid);
        }
    }

    void QMdmmPacketnotifyId()
    {
        // case 1
        {
            Packet p;
            Protocol::NotifyId t = p.notifyId();
            QCOMPARE(t, Protocol::NotifyInvalid);
        }

        // case 2
        {
            Packet p(Protocol::TypeRequest, Protocol::RequestRockPaperScissors, {});
            Protocol::NotifyId t = p.notifyId();
            QCOMPARE(t, Protocol::NotifyInvalid);
        }

        // case 3
        {
            Packet p(Protocol::NotifyVersion, {});
            Protocol::NotifyId t = p.notifyId();
            QCOMPARE(t, Protocol::NotifyVersion);
        }
    }

    void QMdmmPacketvalue()
    {
        // case 1
        {
            Packet p;
            QJsonValue t = p.value();
            QVERIFY(t.isNull());
        }

        // case 2
        {
            Packet p(Protocol::TypeRequest, Protocol::RequestRockPaperScissors, {1});
            QJsonValue t = p.value();
            QVERIFY(!t.isNull());
            QCOMPARE(t.toInt(), 1);
        }
    }

    void QMdmmPacketserialize()
    {
        {
            Packet p(Protocol::TypeRequest, Protocol::RequestRockPaperScissors, {1});
            QByteArray arr = p;

            // The Json object created by Qt is sorted by key
            QByteArray d = R"json({"notifyId":0,"requestId":1,"type":1,"value":1})json"_ba;

            QCOMPARE(arr, d);
        }
    }

    void QMdmmPacketfromJsonhasError_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QString>("errorString");

        QTest::newRow("not-object") << "[1,2,3]"_ba << u"Document is not object"_s;
        QTest::newRow("type-notexist") << "{}"_ba << u"'type' is non-existent"_s;
        QTest::newRow("type-invalid") << R"json({"type": "Fsu0413"})json"_ba << u"'type' is not number"_s;
        QTest::newRow("type-fractional") << R"json({"type": 1.5})json"_ba << u"'type' is not an integer"_s;
        QTest::newRow("type-outofrange") << R"json({"type": 99})json"_ba << u"'type' is out of range"_s;
        QTest::newRow("requestid-notexist") << R"json({"type": 1})json"_ba << u"'requestId' is non-existent"_s;
        QTest::newRow("requestid-invalid") << R"json({"type": 1, "requestId": "Fsu0413"})json"_ba << u"'requestId' is not number"_s;
        QTest::newRow("requestid-fractional") << R"json({"type": 1, "requestId": 2.5})json"_ba << u"'requestId' is not an integer"_s;
        QTest::newRow("requestid-outofrange") << R"json({"type": 1, "requestId": 99})json"_ba << u"'requestId' is out of range"_s;
        QTest::newRow("notifyid-notexist") << R"json({"type": 1, "requestId": 2})json"_ba << u"'notifyId' is non-existent"_s;
        QTest::newRow("notifyid-invalid") << R"json({"type": 1, "requestId": 2, "notifyId": "Fsu0413"})json"_ba << u"'notifyId' is not number"_s;
        QTest::newRow("notifyid-fractional") << R"json({"type": 1, "requestId": 2, "notifyId": 8193.5})json"_ba << u"'notifyId' is not an integer"_s;
        QTest::newRow("notifyid-outofrange") << R"json({"type": 1, "requestId": 2, "notifyId": 99})json"_ba << u"'notifyId' is out of range"_s;
        QTest::newRow("request-requestid-invalid") << R"json({"type": 1, "requestId": 0, "notifyId": 0, "value": null})json"_ba
                                                   << u"'requestId' is invalid for a request/reply packet"_s;
        QTest::newRow("request-notifyid-notinvalid") << R"json({"type": 1, "requestId": 1, "notifyId": 8193, "value": null})json"_ba
                                                     << u"'notifyId' should be invalid for a request/reply packet"_s;
        QTest::newRow("notify-notifyid-invalid") << R"json({"type": 3, "requestId": 0, "notifyId": 0, "value": null})json"_ba << u"'notifyId' is invalid for a notify packet"_s;
        QTest::newRow("notify-requestid-notinvalid") << R"json({"type": 3, "requestId": 1, "notifyId": 8193, "value": null})json"_ba
                                                     << u"'requestId' should be invalid for a notify packet"_s;
        QTest::newRow("value-notexist") << R"json({"type": 1, "requestId": 2, "notifyId": 8193})json"_ba << u"'value' is non-existent"_s;
        QTest::newRow("valid") << R"json({"type": 1, "requestId": 2, "notifyId": 0, "value": "Fsu0413"})json"_ba << QString {};
    }
    void QMdmmPacketfromJsonhasError()
    {
        QFETCH(QByteArray, input);
        QFETCH(QString, errorString);

        QString actualErrorString;

        Packet p = Packet::fromJson(input);

        QCOMPARE(p.hasError(&actualErrorString), !errorString.isEmpty());
        QCOMPARE(errorString, actualErrorString);

        if (actualErrorString.isEmpty()) {
            QCOMPARE(p.type(), Protocol::TypeRequest);
            QCOMPARE(p.requestId(), Protocol::RequestActionOrder);
            QCOMPARE(p.value(), u"Fsu0413"_s);

            // This packet is TypeRequest so notifyId should get invalid
            QCOMPARE(p.notifyId(), Protocol::NotifyInvalid);
        }
    }
    void QMdmmPacketfromJsonhasError2()
    {
        {
            // the no-errorString overload of hasError() reports a parse error
            QVERIFY(Packet::fromJson("some_invalid"_ba).hasError());
        }
        {
            QString actualErrorString;
            QVERIFY(Packet::fromJson("some_invalid"_ba).hasError(&actualErrorString));

            bool r = actualErrorString.startsWith(u"Json error: "_s);
            QVERIFY(r);
        }
        {
            // Check of a notify JSON
            QByteArray input = R"json({"type": 3, "requestId": 0, "notifyId": 8193, "value": "Fsu0413"})json"_ba;
            QString actualErrorString;

            Packet p = Packet::fromJson(input);

            QCOMPARE(p.hasError(&actualErrorString), false);
            QVERIFY(actualErrorString.isEmpty());
            // the no-errorString overload agrees with the errorString overload
            QCOMPARE(p.hasError(), false);

            if (actualErrorString.isEmpty()) {
                QCOMPARE(p.type(), Protocol::TypeNotify);
                QCOMPARE(p.notifyId(), Protocol::NotifyLogicConfiguration);
                QCOMPARE(p.value(), u"Fsu0413"_s);

                // This packet is TypeNotify so requestId should get invalid
                QCOMPARE(p.requestId(), Protocol::RequestInvalid);
            }
        }
    }

    void QMdmmPacketQ_DECLARE_METATYPE()
    {
        // coverage for Q_DECLARE_METATYPE

        Packet p(Protocol::TypeRequest, Protocol::RequestRockPaperScissors, {});
        QVariant v = QVariant::fromValue<Packet>(p);
        QCOMPARE(v.value<Packet>().serialize(), p.serialize());
    }

    // Every enumerator the protocol header declares either carries a packet or is a
    // sentinel / mask marker, and every one of the former has a fixture above. An
    // enumerator added without a fixture, or a fixture left behind by a removed
    // enumerator, fails here.
    void QMdmmProtocolenumeratorsMatchTheFixtures()
    {
        QFile header(QStringLiteral(QMDMM_PROTOCOL_HEADER));
        if (!header.open(QIODevice::ReadOnly))
            QFAIL(qPrintable(u"%1: %2"_s.arg(header.fileName(), header.errorString())));

        const QMap<QString, QStringList> found = enumeratorsInProtocolHeader(header.readAll());

        QMap<QString, QStringList> covered;
        for (const ProtocolIdFixture &fixture : protocolIdFixtures)
            covered[QString::fromLatin1(fixture.enumName)].append(QString::fromLatin1(fixture.enumerator));

        // A whole enum added to the header goes with its own group of fixtures.
        QCOMPARE(covered.keys(), found.keys());

        const QStringList enumNames = found.keys();
        for (const QString &enumName : enumNames) {
            QStringList coveredHere = covered.value(enumName);
            coveredHere.sort();
            QCOMPARE(coveredHere, wireIdsOf(found.value(enumName)));
        }
    }

    // The same fixtures as packets that travel: each one parses, and comes back byte
    // for byte. A fixture whose id is not also in the deserializer's whitelist fails
    // here, which is the coupling that id lives by.
    void QMdmmProtocolfixturePackets_data()
    {
        QTest::addColumn<QByteArray>("serialized");

        for (const ProtocolIdFixture &fixture : protocolIdFixtures) {
            QJsonObject ob;
            ob.insert(u"type"_s, static_cast<int>(fixture.type));
            ob.insert(u"requestId"_s, static_cast<int>(fixture.requestId));
            ob.insert(u"notifyId"_s, static_cast<int>(fixture.notifyId));
            ob.insert(u"value"_s, QJsonValue());

            QTest::newRow(fixture.enumerator) << QJsonDocument(ob).toJson(QJsonDocument::Compact);
        }
    }

    void QMdmmProtocolfixturePackets()
    {
        QFETCH(QByteArray, serialized);

        Packet packet = Packet::fromJson(serialized);

        QString errorString;
        const bool failed = packet.hasError(&errorString);
        QVERIFY2(!failed, qPrintable(u"%1: %2"_s.arg(QString::fromUtf8(serialized), errorString)));

        QCOMPARE(packet.serialize(), serialized);

        // Each fixture also has a malformed form: a packet without its payload is not
        // a legal packet.
        QJsonObject ob = QJsonDocument::fromJson(serialized).object();
        ob.remove(u"value"_s);
        QVERIFY2(Packet::fromJson(QJsonDocument(ob).toJson(QJsonDocument::Compact)).hasError(), qPrintable(QString::fromUtf8(serialized)));
    }

    // Every id the header declares, except the sentinels and the mask markers, has a row
    // in the id tables of doc/protocol.md, and no row names an id the header does not
    // declare. The tables and the header are two carriers of one list -- the pages a
    // reader of the protocol goes to, and the enumerators the code decodes -- so this is
    // what keeps them from drifting apart; either side moving alone fails here.
    void QMdmmProtocoldocumentNamesEveryId()
    {
        QFile header(QStringLiteral(QMDMM_PROTOCOL_HEADER));
        if (!header.open(QIODevice::ReadOnly))
            QFAIL(qPrintable(u"%1: %2"_s.arg(header.fileName(), header.errorString())));

        QFile doc(QStringLiteral(QMDMM_PROTOCOL_DOC));
        if (!doc.open(QIODevice::ReadOnly))
            QFAIL(qPrintable(u"%1: %2"_s.arg(doc.fileName(), doc.errorString())));

        // The header side is the same reading the fixture case takes, so the three lists
        // agree by construction; the document side is the one that can be behind.
        QStringList declared;
        const QMap<QString, QStringList> found = enumeratorsInProtocolHeader(header.readAll());
        for (const QString &enumName : found.keys())
            declared.append(wireIdsOf(found.value(enumName)));
        declared.sort();

        const QStringList documented = idsInProtocolDocument(doc.readAll());

        // Two rows for one id would read as two ids, and a sorted comparison below could
        // not tell that from the list simply being right, so it is looked at first.
        QStringList sorted = documented;
        sorted.sort();
        QStringList deduplicated = sorted;
        deduplicated.removeDuplicates();
        QCOMPARE(deduplicated, sorted);

        QCOMPARE(sorted, declared);
    }
};

namespace {
RegisterTestObject<tst_QMdmmProtocol> _;
}
#include "tst_qmdmmprotocol.moc"

// NOLINTEND

// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMPROTOCOL_H
#define QMDMMPROTOCOL_H

#include "qmdmmcoreglobal.h"

#include <QByteArray>
#include <QJsonObject>
#include <QSharedData>

#include <cstdint>
#include <utility>

QMDMM_EXPORT_NAME(QMdmmProtocol)
QMDMM_EXPORT_NAME(QMdmmPacket)

namespace QMdmmCore {

#ifndef DOXYGEN
namespace v0 {
#endif

namespace Protocol {

enum RequestId : uint8_t
{
    // No requests is from server, all requests are from Logic
    // A reply whose value is null means "give up": the client declines to answer and the server
    // applies the default reply (every legal reply value is non-null).
    RequestInvalid = 0,

    RequestRockPaperScissors, // request: array { string playerName } playerNames, int strivedOrder (or 0 for action) reply: int rps
    RequestActionOrder, // request: array { int } remainedOrders, int maximumOrder, int selectionNum, reply: array { int } orders
    RequestAction, // request: int currentOrder, reply: int(Action) action, optional string toPlayer, optional int toPlace
    RequestUpgrade, // request: int remainingTimes, reply: array { int } item
};

enum NotifyId : uint16_t
{
    NotifyInvalid = 0,

    NotifyFromServerMask = 0x1000,
    NotifyPongServer, // int64 epoch-ms timestamp (echoed from the ping)
    NotifyVersion, // string versionNumber, int protocolVersion

    NotifyFromAgentMask = 0x2000,
    NotifyLogicConfiguration, // broadcast, object (see QMdmmCore::LogicConfiguration in qmdmmroom.h)
    NotifyAgentStateChanged, // string playerName, int (AgentState) agentState
    NotifyPlayerAdded, // string playerName, string screenName, int(AgentState) agentState
    NotifyPlayerRemoved, // string playerName
    NotifyGameStart, // broadcast
    NotifyRoundStart, // broadcast
    NotifyRockPaperScissors, // broadcast, object { string playerName: int rps }
    NotifyActionOrder, // broadcast, array { string playerName } (dense 1..N, index i = order i+1)
    NotifyAction, // broadcast, object { string playerName, int(Action) action, optional string toPlayer, optional int toPlace }
    NotifyRoundOver, // broadcast
    NotifyUpgrade, // broadcast, object { string playerName: array { int } item }
    NotifyGameOver, // broadcast, array { string } winnerPlayerNames
    NotifySpoken, // broadcast, string playerName, string content
    NotifyOperated, // TODO: for ob

    NotifyToServerMask = 0x4000,
    NotifyPingServer, // int64 epoch-ms timestamp
    NotifySignIn, // string playerName, string screenName, int(AgentState) agentState, int lastRoundEventSeq
    NotifyObserve, // string observerName, string playerName
    NotifyManagedChanged, // bool managed

    NotifyToAgentMask = 0x8000,
    NotifySpeak, // string
    NotifyOperate, // TODO: for ob
};

enum PacketType : uint8_t
{
    TypeInvalid = 0,

    TypeRequest,
    TypeReply,
    TypeNotify,
};

QMDMMCORE_EXPORT extern int version() noexcept;

} // namespace Protocol

#ifndef DOXYGEN
} // namespace v0

namespace p {

// Cannot pimpl following class since it inherits QSharedData
// So put it to header file and inherit QJsonObject, in order not to affect binary compatibility when more data come in
// ATTENTION: neither of the inherited 2 classes have virtual dtor

// documentation is not needed since it is purely internal to QMdmmCore::Packet
struct QMDMMCORE_EXPORT PacketDataP final : public QSharedData, public QJsonObject
{
    PacketDataP();
    PacketDataP(v0::Protocol::PacketType type, v0::Protocol::RequestId requestId, v0::Protocol::NotifyId notifyId, const QJsonValue &value);

    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor): copy-init from QJsonObject exercised by QMdmmPacketDataCopy test
    PacketDataP(const QJsonObject &ob) noexcept(noexcept(QJsonObject(ob)));
    // std::declval rather than an explicit qualified call: MSVC does not parse the latter in an
    // exception specification (it reports C2382 and C2352 instead).
    PacketDataP &operator=(const QJsonObject &ob) noexcept(noexcept(std::declval<QJsonObject &>() = ob));

    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
    QString error;
};

} // namespace p

namespace v0 {
#endif

class QMDMMCORE_EXPORT Packet final
{
public:
    Packet();
    Packet(Protocol::PacketType type, Protocol::RequestId requestId, const QJsonValue &value);
    Packet(Protocol::NotifyId notifyId, const QJsonValue &value);

    [[nodiscard]] Protocol::PacketType type() const;
    [[nodiscard]] Protocol::RequestId requestId() const;
    [[nodiscard]] Protocol::NotifyId notifyId() const;
    [[nodiscard]] QJsonValue value() const;

    [[nodiscard]] QByteArray serialize() const;
    [[nodiscard]] operator QByteArray() const // NOLINT(cppcoreguidelines-explicit-constructor): implicit conversion relied on by QByteArray-sink call sites
    {
        return serialize();
    }
    bool hasError(QString *errorString = nullptr) const;

    // The enclosing class carries QMDMMCORE_EXPORT, which is what exports this one; MSVC rejects the second marking (C2487).
    static Packet fromJson(const QByteArray &serialized);

#ifndef DOXYGEN
private:
    QSharedDataPointer<p::PacketDataP> d;
#endif
};

#ifndef DOXYGEN
} // namespace v0
inline namespace v1 {
using v0::Packet;
namespace Protocol = v0::Protocol; // NOLINT(misc-unused-alias-decls)
} // namespace v1
#endif

} // namespace QMdmmCore

Q_DECLARE_METATYPE(QMdmmCore::Packet)

#endif // QMDMMPROTOCOL_H

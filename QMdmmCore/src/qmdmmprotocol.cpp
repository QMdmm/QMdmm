// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmprotocol.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSharedData>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

/**
 * @file qmdmmprotocol.h
 * @brief QMdmm protocol definitions
 */

namespace QMdmmCore {

#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @namespace Protocol
 * @brief The namespace for protocol
 */

/**
 * @enum Protocol::RequestId
 * @brief The IDs for requests and replies
 *
 * No requests come from the server; all requests originate from the Logic.
 */

/**
 * @var Protocol::RequestId Protocol::RequestInvalid
 * @brief An invalid request / reply
 */

/**
 * @var Protocol::RequestId Protocol::RequestRockPaperScissors
 * @brief A request of Rock-Paper-Scissors
 *
 * Wire format -- request: @c {"playerNames": [string], "strivedOrder": int} (a @c strivedOrder of
 * @c 0 selects an action instead of a strived order); reply: @c int rps.
 */

/**
 * @var Protocol::RequestId Protocol::RequestActionOrder
 * @brief A request of action order
 *
 * Wire format -- request: @c {"remainedOrders": [int], "maximumOrder": int, "selectionNum": int};
 * reply: @c [int] orders, one entry per selection (the reply length equals the request's
 * @c selectionNum). Each entry is either @c 0 to yield that selection -- the player accepts
 * whatever order is left over and stops competing for it -- or an order in the range
 * @c 1..maximumOrder that the player strives for. A reply longer than @c selectionNum is
 * rejected at the decode layer and disconnects the client. A well-formed reply that is not
 * feasible (a length below @c selectionNum, an order outside @c 1..maximumOrder, or a
 * duplicated / already confirmed order) is not rejected: it falls back to yielding every
 * selection, so a buggy or malicious agent cannot stall the ActionOrder phase.
 */

/**
 * @var Protocol::RequestId Protocol::RequestAction
 * @brief A request of action
 *
 * Wire format -- request: @c int currentOrder; reply: @c {"action": int(Action),
 * "toPlayer": string (optional), "toPlace": int (optional)}. A reply whose action is not
 * feasible (e.g. an unknown @c toPlayer or an unaffordable action) is not rejected: it falls
 * back to @c DoNothing, so a buggy or malicious agent cannot stall the Action phase.
 */

/**
 * @var Protocol::RequestId Protocol::RequestUpgrade
 * @brief A request of upgrade
 *
 * Wire format -- request: @c int remainingTimes; reply: @c [int] item. The request's
 * @c remainingTimes carries the player's upgrade points for this round (the value of
 * @c Player::upgradePoint -- the wire name is a legacy mismatch, the rename is deferred to the
 * v0 freeze); the reply must spend exactly that many points, so its length must equal
 * @c remainingTimes. A reply longer than @c remainingTimes is rejected at the decode layer and
 * disconnects the client. A well-formed reply that is not feasible (a length below
 * @c remainingTimes, or an item whose stat has no upgrade points left) is not rejected: it
 * falls back to spending every point in the order knife -> horse -> maxHp. An empty reply is a
 * deliberate protocol value that triggers this same fallback, so a buggy or malicious agent
 * cannot stall the Upgrade phase.
 */

/**
 * @enum Protocol::NotifyId
 * @brief The IDs for notifies
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyInvalid
 * @brief An invalid notify
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyFromServerMask
 * @brief A mask of notify from server
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyPongServer
 * @brief A notify from server of a ping-pong (heartbeat)
 *
 * Wire format: @c int64 epoch-milliseconds timestamp (echoed from the ping).
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyVersion
 * @brief A notify from server of version number
 *
 * Wire format: @c {"versionNumber": string, "protocolVersion": int}.
 *
 * When @c protocolVersion differs from @c Protocol::version() the client disconnects: the wire
 * protocol is incompatible, so it drops the connection via @c disconnectFromHost() rather than
 * entering the auto-reconnect loop (which would re-hit the same mismatch). A @c versionNumber
 * mismatch is tolerated -- the wire protocol is still compatible -- and is currently a no-op.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyFromAgentMask
 * @brief A mask of notify from agent
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyLogicConfiguration
 * @brief A notify from agent of logic configuration
 *
 * Wire format: broadcast, @c object (see QMdmmCore::LogicConfiguration in qmdmmroom.h).
 *
 * Keys absent from the object fall back to the receiver's own @c LogicConfiguration::defaults(), so a
 * partial object -- or an empty one, meaning "all defaults" -- is accepted; a present key must still be
 * valid, otherwise the receiver ignores the notify and keeps its previous configuration. An empty object
 * is what a server started without any explicit logic configuration broadcasts, i.e. the default case.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyAgentStateChanged
 * @brief A notify from agent of agent state
 *
 * Wire format: @c {"playerName": string, "agentState": int (AgentState)}.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyPlayerAdded
 * @brief A notify from agent of player added
 *
 * Wire format: @c {"playerName": string, "screenName": string, "agentState": int (AgentState)}.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyPlayerRemoved
 * @brief A notify from agent of player removed
 *
 * Wire format: @c {"playerName": string}.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyGameStart
 * @brief A notify from agent of game started
 *
 * Wire format: broadcast, empty @c object.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyRoundStart
 * @brief A notify from agent of round started
 *
 * Wire format: broadcast, empty @c object.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyRockPaperScissors
 * @brief A notify from agent of Rock-Paper-Scissors
 *
 * Wire format: broadcast, @c {playerName: int rps} (an object keyed by player name, value is the
 * Rock-Paper-Scissors choice).
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyActionOrder
 * @brief A notify from agent of action order
 *
 * Wire format: broadcast, @c [string] (a dense array where index @c i is the player taking order
 * @c i + 1; orders are always contiguous 1..N).
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyAction
 * @brief A notify from agent of action
 *
 * Wire format: broadcast, @c {"playerName": string, "action": int (Action),
 * "toPlayer": string (optional), "toPlace": int (optional)}.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyRoundOver
 * @brief A notify from agent of round over
 *
 * Wire format: broadcast, empty @c object.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyUpgrade
 * @brief A notify from agent of upgrade
 *
 * Wire format: broadcast, @c {playerName: [int item]} (an object keyed by player name, value is
 * the list of upgrade items).
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyGameOver
 * @brief A notify from agent of game over
 *
 * Wire format: broadcast, @c [string] (the array of winning player names).
 */

/**
 * @var Protocol::NotifyId Protocol::NotifySpoken
 * @brief A notify from agent of agent spoken
 *
 * Wire format: broadcast, @c {"playerName": string, "content": string}.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyOperated
 * @brief A notify from agent of agent operated
 *
 * @todo OB functionality
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyToServerMask
 * @brief A mask of notify to server
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyPingServer
 * @brief A notify to server of a ping-pong (heartbeat)
 *
 * Wire format: @c int64 epoch-milliseconds timestamp.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifySignIn
 * @brief A notify to server of sign in
 *
 * Wire format: @c {"playerName": string, "screenName": string, "agentState": int (AgentState),
 * "lastRoundEventSeq": int}.
 *
 * @c agentState is a self-declaration: on a fresh sign-in the server stores it verbatim and does
 * not filter or validate the @c StateMaskTrust / @c StateMaskBot flags. Trust ("managed") and Bot
 * are client promises, not server-enforced privileges -- the server never asserts either flag
 * itself, only toggling the Online flag (cleared on disconnect, restored on reconnect) and
 * clearing Trust on disconnect. A reconnect ignores the reported @c agentState and only restores
 * the Online flag (Trust is not re-granted, see @c StateMaskTrust).
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyObserve
 * @brief A notify to server of observe
 *
 * Wire format: @c {"observerName": string, "playerName": string}.
 *
 * @todo OB functionality
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyManagedChanged
 * @brief A notify to server of the player declaring its managed state
 *
 * Wire format: @c {"managed": bool}.
 *
 * The player is identified by the socket, so no name is carried. Like the sign-in @c agentState
 * (see @c NotifySignIn) this is a self-declaration: the server applies it to the @c StateMaskTrust
 * flag, leaves the remaining flags alone, and reports the result back through the ordinary
 * @c NotifyAgentStateChanged broadcast. It is the runtime counterpart of the managed flag a sign-in
 * carries, for a client UI that toggles being managed while the connection is up.
 *
 * A payload that is not an object, or whose @c managed is not a bool, is a protocol error: the
 * server raises @c Socket::ProtocolError and drops the connection rather than ignoring the notify.
 * The same happens on a socket that has not signed in -- there is no player whose flag could be
 * applied.
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyToAgentMask
 * @brief A mask of notify to agent
 */

/**
 * @var Protocol::NotifyId Protocol::NotifySpeak
 * @brief A notify to agent of speaking
 *
 * Wire format: @c string (base64-encoded UTF-8 content).
 */

/**
 * @var Protocol::NotifyId Protocol::NotifyOperate
 * @brief A notify to agent of operating
 *
 * @todo OB functionality
 */

/**
 * @enum Protocol::PacketType
 * @brief The type of a packet
 */

/**
 * @var Protocol::PacketType Protocol::TypeInvalid
 * @brief An invalid packet
 */

/**
 * @var Protocol::PacketType Protocol::TypeRequest
 * @brief A request packet
 */

/**
 * @var Protocol::PacketType Protocol::TypeReply
 * @brief A reply packet
 */

/**
 * @var Protocol::PacketType Protocol::TypeNotify
 * @brief A notify packet
 */

/**
 * @brief get the protocol version of current implementation
 * @return the version of protocol
 *
 * Currently only version 0 is implemented. Different protocol version is incompatible.
 */
int Protocol::version() noexcept
{
    return 0;
}

#ifndef DOXYGEN
} // namespace v0

namespace p {

PacketDataP::PacketDataP()
{
    insert(u"type"_s, static_cast<int>(v0::Protocol::TypeInvalid));
    insert(u"requestId"_s, static_cast<int>(v0::Protocol::RequestInvalid));
    insert(u"notifyId"_s, static_cast<int>(v0::Protocol::NotifyInvalid));
    insert(u"value"_s, QJsonValue());
}

PacketDataP::PacketDataP(v0::Protocol::PacketType type, v0::Protocol::RequestId requestId, v0::Protocol::NotifyId notifyId, const QJsonValue &v)
{
    insert(u"type"_s, static_cast<int>(type));
    insert(u"requestId"_s, static_cast<int>(requestId));
    insert(u"notifyId"_s, static_cast<int>(notifyId));
    insert(u"value"_s, v);
}

PacketDataP::PacketDataP(const QJsonObject &ob) noexcept(noexcept(QJsonObject(ob)))
    : QJsonObject(ob)
{
}

PacketDataP &PacketDataP::operator=(const QJsonObject &ob) noexcept(noexcept(QJsonObject::operator=(ob)))
{
    QJsonObject::operator=(ob);
    return *this;
}

} // namespace p

namespace v0 {
#endif

/**
 * @class Packet
 * @brief A packet for QMdmm protocol
 *
 * A packet of QMdmm Protocol is a JSON object, encoded in a single line.
 *
 * The top-level object carries four keys:
 * - @c "type": @c int, one of Protocol::PacketType.
 * - @c "requestId": @c int, one of Protocol::RequestId. Meaningful only for request/reply packets,
 *   and @c Protocol::RequestInvalid for notify/invalid packets.
 * - @c "notifyId": @c int, one of Protocol::NotifyId. Meaningful only for notify packets, and
 *   @c Protocol::NotifyInvalid for request/reply/invalid packets.
 * - @c "value": the payload. Its shape depends on the requestId (for request/reply) or notifyId
 *   (for notify); see the per-ID documentation below.
 *
 * Unknown keys are preserved and ignored.
 */

/**
 * @brief ctor.
 */
Packet::Packet()
    : d(new p::PacketDataP)
{
}

/**
 * @brief ctor of a request or reply packet
 * @param type the packet type
 * @param requestId the request ID
 * @param value the value / payload of the packet
 */
Packet::Packet(Protocol::PacketType type, Protocol::RequestId requestId, const QJsonValue &value)
    : d(new p::PacketDataP(type, requestId, Protocol::NotifyInvalid, value))
{
}

/**
 * @brief ctor of a notify packet
 * @param notifyId the notify ID
 * @param value the value / payload of the packet
 */
Packet::Packet(Protocol::NotifyId notifyId, const QJsonValue &value)
    : d(new p::PacketDataP(Protocol::TypeNotify, Protocol::RequestInvalid, notifyId, value))
{
}

/**
 * @brief get the type of this packet
 * @return packet type
 */
Protocol::PacketType Packet::type() const
{
    return static_cast<Protocol::PacketType>(d->value(u"type"_s).toInt(Protocol::TypeInvalid));
}

/**
 * @brief get the request ID of this packet
 * @return request ID
 */
Protocol::RequestId Packet::requestId() const
{
    Protocol::PacketType t = type();
    if (t == Protocol::TypeRequest || t == Protocol::TypeReply)
        return static_cast<Protocol::RequestId>(d->value(u"requestId"_s).toInt(Protocol::RequestInvalid));

    return Protocol::RequestInvalid;
}

/**
 * @brief get the notify ID of this packet
 * @return notify ID
 */
Protocol::NotifyId Packet::notifyId() const
{
    Protocol::PacketType t = type();
    if (t == Protocol::TypeNotify)
        return static_cast<Protocol::NotifyId>(d->value(u"notifyId"_s).toInt(Protocol::NotifyInvalid));

    return Protocol::NotifyInvalid;
}

/**
 * @brief get the value / payload of this packet
 * @return value / payload
 */
QJsonValue Packet::value() const
{
    return d->value(u"value"_s);
}

/**
 * @brief serialize the packet
 * @return serialized byte array for sending
 *
 * To deserialize the returned QByteArray, use @c Packet::fromJson() function.
 */
QByteArray Packet::serialize() const
{
    QJsonDocument doc(*d);
    return doc.toJson(QJsonDocument::Compact);
}

/**
 * @fn Packet::operator QByteArray() const
 * @brief serialize the packet
 * @return serialized byte array for sending
 *
 * overload conversion.
 */

/**
 * @brief checks if this packet has error
 * @param errorString (out) the optional error string
 * @return if the packet has error
 */
bool Packet::hasError(QString *errorString) const
{
    if (errorString != nullptr)
        *errorString = d->error;

    return !d->error.isEmpty();
}

namespace {

bool isPacketTypeValid(int type)
{
    switch (static_cast<Protocol::PacketType>(type)) {
    case Protocol::TypeInvalid:
    case Protocol::TypeRequest:
    case Protocol::TypeReply:
    case Protocol::TypeNotify:
        return true;
    default:
        return false;
    }
}

bool isRequestIdValid(int requestId)
{
    switch (static_cast<Protocol::RequestId>(requestId)) {
    case Protocol::RequestInvalid:
    case Protocol::RequestRockPaperScissors:
    case Protocol::RequestActionOrder:
    case Protocol::RequestAction:
    case Protocol::RequestUpgrade:
        return true;
    default:
        return false;
    }
}

// Every Protocol::NotifyId must be listed here. Deserialization uses this as the whitelist, and the
// socket layer refuses to deliver a malformed packet: an id missing from this list therefore makes
// every packet carrying it drop the connection, rather than reach whichever side handles it.
bool isNotifyIdValid(int notifyId)
{
    switch (static_cast<Protocol::NotifyId>(notifyId)) {
    case Protocol::NotifyInvalid:
    case Protocol::NotifyPongServer:
    case Protocol::NotifyVersion:
    case Protocol::NotifyLogicConfiguration:
    case Protocol::NotifyAgentStateChanged:
    case Protocol::NotifyPlayerAdded:
    case Protocol::NotifyPlayerRemoved:
    case Protocol::NotifyGameStart:
    case Protocol::NotifyRoundStart:
    case Protocol::NotifyRockPaperScissors:
    case Protocol::NotifyActionOrder:
    case Protocol::NotifyAction:
    case Protocol::NotifyRoundOver:
    case Protocol::NotifyUpgrade:
    case Protocol::NotifyGameOver:
    case Protocol::NotifySpoken:
    case Protocol::NotifyOperated:
    case Protocol::NotifyPingServer:
    case Protocol::NotifySignIn:
    case Protocol::NotifyObserve:
    case Protocol::NotifyManagedChanged:
    case Protocol::NotifySpeak:
    case Protocol::NotifyOperate:
        return true;
    default:
        return false;
    }
}

// JSON numbers are doubles; enum fields must be integral values. Returns true and
// stores the value in @p out if @p value is a whole number that fits in an int.
// Rejects fractional values (e.g. 1.5) and out-of-int-range values (e.g. 1e30),
// which toInt() would otherwise silently truncate or wrap.
bool toIntegral(const QJsonValue &value, int *out)
{
    const double d = value.toDouble();
    if (d != std::floor(d) || d < static_cast<double>(std::numeric_limits<int>::min()) || d > static_cast<double>(std::numeric_limits<int>::max()))
        return false;

    *out = static_cast<int>(d);
    return true;
}

} // namespace

/**
 * @brief deserialize the byte array
 * @param serialized the serialized byte array
 * @return the deserialized packet
 *
 * This does the opposite of @c Packet::serialize() function.
 * The error, if any, is only observable through @c Packet::hasError().
 *
 * Deserialization enforces the same invariants as @c serialize(): a request or
 * reply packet must carry a concrete @c Protocol::RequestId (not
 * @c Protocol::RequestInvalid) and @c Protocol::NotifyInvalid; a notify packet
 * must carry a concrete @c Protocol::NotifyId (not @c Protocol::NotifyInvalid)
 * and @c Protocol::RequestInvalid; an invalid packet must carry both
 * @c Protocol::RequestInvalid and @c Protocol::NotifyInvalid. The three enum
 * fields (@c type, @c requestId, @c notifyId) must be integral JSON numbers:
 * fractional values (e.g. 1.5) and out-of-range values are rejected, not
 * silently truncated.
 */
Packet Packet::fromJson(const QByteArray &serialized)
{
    Packet ret;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(serialized, &err);

    if (err.error != QJsonParseError::NoError) {
        ret.d->error = u"Json error: "_s.append(err.errorString());
        return ret;
    }

    if (!doc.isObject()) {
        ret.d->error = u"Document is not object"_s;
        return ret;
    }

    *(ret.d) = doc.object();

    int typeInt = 0;
    if (!ret.d->contains(u"type"_s)) {
        ret.d->error = u"'type' is non-existent"_s;
        return ret;
    }
    if (!ret.d->value(u"type"_s).isDouble()) {
        ret.d->error = u"'type' is not number"_s;
        return ret;
    }
    if (!toIntegral(ret.d->value(u"type"_s), &typeInt)) {
        ret.d->error = u"'type' is not an integer"_s;
        return ret;
    }
    if (!isPacketTypeValid(typeInt)) {
        ret.d->error = u"'type' is out of range"_s;
        return ret;
    }

    int requestIdInt = 0;
    if (!ret.d->contains(u"requestId"_s)) {
        ret.d->error = u"'requestId' is non-existent"_s;
        return ret;
    }
    if (!ret.d->value(u"requestId"_s).isDouble()) {
        ret.d->error = u"'requestId' is not number"_s;
        return ret;
    }
    if (!toIntegral(ret.d->value(u"requestId"_s), &requestIdInt)) {
        ret.d->error = u"'requestId' is not an integer"_s;
        return ret;
    }
    if (!isRequestIdValid(requestIdInt)) {
        ret.d->error = u"'requestId' is out of range"_s;
        return ret;
    }

    int notifyIdInt = 0;
    if (!ret.d->contains(u"notifyId"_s)) {
        ret.d->error = u"'notifyId' is non-existent"_s;
        return ret;
    }
    if (!ret.d->value(u"notifyId"_s).isDouble()) {
        ret.d->error = u"'notifyId' is not number"_s;
        return ret;
    }
    if (!toIntegral(ret.d->value(u"notifyId"_s), &notifyIdInt)) {
        ret.d->error = u"'notifyId' is not an integer"_s;
        return ret;
    }
    if (!isNotifyIdValid(notifyIdInt)) {
        ret.d->error = u"'notifyId' is out of range"_s;
        return ret;
    }

    if (!ret.d->contains(u"value"_s)) {
        ret.d->error = u"'value' is non-existent"_s;
        return ret;
    }

    const Protocol::PacketType type = static_cast<Protocol::PacketType>(typeInt);
    const Protocol::RequestId requestId = static_cast<Protocol::RequestId>(requestIdInt);
    const Protocol::NotifyId notifyId = static_cast<Protocol::NotifyId>(notifyIdInt);

    if (type == Protocol::TypeRequest || type == Protocol::TypeReply) {
        if (requestId == Protocol::RequestInvalid) {
            ret.d->error = u"'requestId' is invalid for a request/reply packet"_s;
            return ret;
        }
        if (notifyId != Protocol::NotifyInvalid) {
            ret.d->error = u"'notifyId' should be invalid for a request/reply packet"_s;
            return ret;
        }
    } else if (type == Protocol::TypeNotify) {
        if (notifyId == Protocol::NotifyInvalid) {
            ret.d->error = u"'notifyId' is invalid for a notify packet"_s;
            return ret;
        }
        if (requestId != Protocol::RequestInvalid) {
            ret.d->error = u"'requestId' should be invalid for a notify packet"_s;
            return ret;
        }
    } else {
        if (requestId != Protocol::RequestInvalid) {
            ret.d->error = u"'requestId' should be invalid for an invalid packet"_s;
            return ret;
        }
        if (notifyId != Protocol::NotifyInvalid) {
            ret.d->error = u"'notifyId' should be invalid for an invalid packet"_s;
            return ret;
        }
    }

    return ret;
}

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore

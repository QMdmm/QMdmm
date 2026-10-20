// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmserver.h"
#include "qmdmmserver_p.h"

#include "qmdmmagent.h"
#include "qmdmmlogicrunner_p.h"

#include <QLocalSocket>
#include <QTcpSocket>

#include <cmath>
#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

/**
 * @file qmdmmserver.h
 * @brief This is the file where the networking Server is defined.
 */

namespace QMdmmNetworking {
#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class ServerConfiguration
 * @ingroup QMdmmNetworking
 * @brief Contains configurations of server
 *
 * The configuration is a @c QJsonObject with well-known keys. Because the class inherits
 * @c QJsonObject, arbitrary keys can be inserted and values are not validated on access; unknown
 * keys are ignored. Call @c deserialize() to validate a raw JSON value (rejecting wrong types and
 * out-of-range values) before using it.
 *
 * Every key here is a start-up setting rather than a per-game one, and a key an operator would
 * change at start-up is reachable three ways: a command-line option, a stored key in the
 * configuration file (named after the option), and a row in @c doc/command-line.md. The three go
 * together -- an option with no stored key is a setting that cannot be remembered between runs,
 * and a key with no row is one nobody can find -- and the server's own command-line tests hold
 * the properties declared here to the keys the program stores, so a new key cannot miss them
 * without turning a test red.
 *
 * There is one shape today and no second one: every property declared here is a start-up setting
 * an operator would tune, so it carries all three faces. A key whose value no operator would tune
 * therefore has nowhere to go while the gate stands as it does -- the properties declared here and
 * the keys the program stores are held equal, with nothing subtracted on either side -- so adding
 * one means changing that gate in the same commit, not only declaring the property.
 */

/**
 * @property ServerConfiguration::tcpEnabled
 * @brief Whether the TCP server is enabled, default true
 */

/**
 * @property ServerConfiguration::tcpPort
 * @brief The TCP port, default 6366
 */

/**
 * @property ServerConfiguration::localEnabled
 * @brief Whether the local socket server is enabled, default true
 */

/**
 * @property ServerConfiguration::localSocketName
 * @brief The local socket name, default "QMdmm"
 */

/**
 * @property ServerConfiguration::websocketEnabled
 * @brief Whether the WebSocket server is enabled, default true
 */

/**
 * @property ServerConfiguration::websocketName
 * @brief The WebSocket server name, default "QMdmm"
 */

/**
 * @property ServerConfiguration::websocketPort
 * @brief The WebSocket port, default 6367
 */

/**
 * @property ServerConfiguration::controlPort
 * @brief The port the read-only control plane listens on, default 0 (off)
 *
 * The control plane is a second, deliberately narrow listener: it speaks HTTP/1.0 on the loopback
 * address only and serves one path, @c /status, with the same snapshot @c Server::status() returns.
 * It carries no game traffic and cannot change anything. @c 0 turns it off, which is the default,
 * so a server that was not asked for one opens no port beyond the transports it was configured
 * with. Port @c 0 is not "pick one for me": an operator who reads the snapshot has to know where
 * the endpoint is, so the plane is either at a port that was named or not there at all.
 */

/**
 * @property ServerConfiguration::playerNumPerRoom
 * @brief The player number per room, default 3
 */

/**
 * @property ServerConfiguration::requestTimeout
 * @brief The request timeout in seconds, default 20
 *
 * The server's request timer only backstops abnormal cases now: a healthy
 * client replies or explicitly gives up on its own, so the timer fires only when the
 * client is gone or stuck. The value is in seconds; it is converted to milliseconds
 * when the request timer is armed, and a non-zero value is extended by a grace period
 * before the connection is treated as gone. A value of 0 turns the request timer off: a
 * request then waits for its reply for as long as the client takes.
 *
 * Independent of it, every signed-in connection carries a silence deadline of 90 seconds -- three
 * times the interval at which a client pings. A peer that sends nothing at all for that long is
 * dropped, and no setting here turns that off. Waiting on a request and keeping a connection alive
 * are two different questions: a request may be waited on indefinitely, while a connection whose
 * peer has stopped speaking is gone whatever is being waited for.
 */

/**
 * @property ServerConfiguration::maximumPacketSize
 * @brief The largest packet the server accepts from a peer, in bytes, default 1 MiB
 *
 * A packet is one line, so a peer that sends this many bytes without ending a line is not
 * speaking the protocol: the server records a @c Socket::ProtocolError and drops the
 * connection instead of buffering data that can never become a packet. The value has to be
 * at least 1024, so that the protocol's own packets always fit; there is no "unlimited"
 * setting, since the bound is the point.
 */

/**
 * @property ServerConfiguration::signInTimeout
 * @brief How long a connection may stay silent before it is given up on, in seconds, default 5
 *
 * A peer that connects and never signs in is not a player yet, so nothing else in the server
 * would ever notice it: left to itself, the connection (and the memory behind it) is held for
 * as long as the peer cares to keep the socket open. Every accepted socket is given this long
 * to sign in, and one that has not done so by then is dropped with a
 * @c Socket::ProtocolError. A connection that did sign in is left alone by this deadline, which
 * is about a peer that never became a player: what watches a player's connection afterwards is
 * the silence deadline described under @c requestTimeout, and this key does not set it. This
 * deadline is always armed, and a value has to be between 2 and 60 seconds: the upper bound is
 * the one the deadline exists for, keeping a peer that never signs in from holding a port and
 * the memory behind it for longer than a handshake could plausibly take, while the lower bound
 * only has to outlast the slowest honest handshake.
 */

/**
 * @fn ServerConfiguration::tcpEnabled() const
 * @brief getter of @c ServerConfiguration::tcpEnabled
 * @return @c ServerConfiguration::tcpEnabled
 */

/**
 * @fn ServerConfiguration::setTcpEnabled(bool tcpEnabled)
 * @brief setter of @c ServerConfiguration::tcpEnabled
 * @param tcpEnabled @c ServerConfiguration::tcpEnabled
 */

/**
 * @fn ServerConfiguration::tcpPort() const
 * @brief getter of @c ServerConfiguration::tcpPort
 * @return @c ServerConfiguration::tcpPort
 */

/**
 * @fn ServerConfiguration::setTcpPort(uint16_t tcpPort)
 * @brief setter of @c ServerConfiguration::tcpPort
 * @param tcpPort @c ServerConfiguration::tcpPort
 */

/**
 * @fn ServerConfiguration::localEnabled() const
 * @brief getter of @c ServerConfiguration::localEnabled
 * @return @c ServerConfiguration::localEnabled
 */

/**
 * @fn ServerConfiguration::setLocalEnabled(bool localEnabled)
 * @brief setter of @c ServerConfiguration::localEnabled
 * @param localEnabled @c ServerConfiguration::localEnabled
 */

/**
 * @fn ServerConfiguration::localSocketName() const
 * @brief getter of @c ServerConfiguration::localSocketName
 * @return @c ServerConfiguration::localSocketName
 */

/**
 * @fn ServerConfiguration::setLocalSocketName(const QString &localSocketName)
 * @brief setter of @c ServerConfiguration::localSocketName
 * @param localSocketName @c ServerConfiguration::localSocketName
 */

/**
 * @fn ServerConfiguration::websocketEnabled() const
 * @brief getter of @c ServerConfiguration::websocketEnabled
 * @return @c ServerConfiguration::websocketEnabled
 */

/**
 * @fn ServerConfiguration::setWebsocketEnabled(bool websocketEnabled)
 * @brief setter of @c ServerConfiguration::websocketEnabled
 * @param websocketEnabled @c ServerConfiguration::websocketEnabled
 */

/**
 * @fn ServerConfiguration::websocketName() const
 * @brief getter of @c ServerConfiguration::websocketName
 * @return @c ServerConfiguration::websocketName
 */

/**
 * @fn ServerConfiguration::setWebsocketName(const QString &websocketName)
 * @brief setter of @c ServerConfiguration::websocketName
 * @param websocketName @c ServerConfiguration::websocketName
 */

/**
 * @fn ServerConfiguration::websocketPort() const
 * @brief getter of @c ServerConfiguration::websocketPort
 * @return @c ServerConfiguration::websocketPort
 */

/**
 * @fn ServerConfiguration::setWebsocketPort(uint16_t websocketPort)
 * @brief setter of @c ServerConfiguration::websocketPort
 * @param websocketPort @c ServerConfiguration::websocketPort
 */

/**
 * @fn ServerConfiguration::controlPort() const
 * @brief getter of @c ServerConfiguration::controlPort
 * @return @c ServerConfiguration::controlPort
 */

/**
 * @fn ServerConfiguration::setControlPort(uint16_t controlPort)
 * @brief setter of @c ServerConfiguration::controlPort
 * @param controlPort @c ServerConfiguration::controlPort
 */

/**
 * @fn ServerConfiguration::playerNumPerRoom() const
 * @brief getter of @c ServerConfiguration::playerNumPerRoom
 * @return @c ServerConfiguration::playerNumPerRoom
 */

/**
 * @fn ServerConfiguration::setPlayerNumPerRoom(int playerNumPerRoom)
 * @brief setter of @c ServerConfiguration::playerNumPerRoom
 * @param playerNumPerRoom @c ServerConfiguration::playerNumPerRoom
 */

/**
 * @fn ServerConfiguration::requestTimeout() const
 * @brief getter of @c ServerConfiguration::requestTimeout
 * @return @c ServerConfiguration::requestTimeout
 */

/**
 * @fn ServerConfiguration::setRequestTimeout(int requestTimeout)
 * @brief setter of @c ServerConfiguration::requestTimeout
 * @param requestTimeout @c ServerConfiguration::requestTimeout
 */

/**
 * @fn ServerConfiguration::maximumPacketSize() const
 * @brief getter of @c ServerConfiguration::maximumPacketSize
 * @return @c ServerConfiguration::maximumPacketSize
 */

/**
 * @fn ServerConfiguration::setMaximumPacketSize(int maximumPacketSize)
 * @brief setter of @c ServerConfiguration::maximumPacketSize
 * @param maximumPacketSize @c ServerConfiguration::maximumPacketSize
 */

/**
 * @fn ServerConfiguration::signInTimeout() const
 * @brief getter of @c ServerConfiguration::signInTimeout
 * @return @c ServerConfiguration::signInTimeout
 */

/**
 * @fn ServerConfiguration::setSignInTimeout(int signInTimeout)
 * @brief setter of @c ServerConfiguration::signInTimeout
 * @param signInTimeout @c ServerConfiguration::signInTimeout
 */

/**
 * @brief Get default values of configuration
 * @return default configuration
 */
const ServerConfiguration &ServerConfiguration::defaults()
{
    // clang-format off
    static const ServerConfiguration defaultInstance {
        qMakePair(u"tcpEnabled"_s, true),
        qMakePair(u"tcpPort"_s, static_cast<int>(6366U)),
        qMakePair(u"localEnabled"_s, true),
        qMakePair(u"localSocketName"_s, u"QMdmm"_s),
        qMakePair(u"websocketEnabled"_s, true),
        qMakePair(u"websocketName"_s, u"QMdmm"_s),
        qMakePair(u"websocketPort"_s, static_cast<int>(6367U)),
        qMakePair(u"controlPort"_s, 0),
        qMakePair(u"playerNumPerRoom"_s, 3),
        qMakePair(u"requestTimeout"_s, 20),
        qMakePair(u"maximumPacketSize"_s, Socket::DefaultMaximumPacketSize),
        qMakePair(u"signInTimeout"_s, 5),
    };
    // clang-format on

    return defaultInstance;
}

#define CONVERTTOTYPEBOOL(v) ((v).toBool()) // NOLINT(cppcoreguidelines-macro-usage)
#define CONVERTTOTYPEUINT16T(v) ((uint16_t)((v).toInt())) // NOLINT(cppcoreguidelines-macro-usage)
#define CONVERTTOTYPEQSTRING(v) ((v).toString()) // NOLINT(cppcoreguidelines-macro-usage)
#define CONVERTTOTYPEINT(v) ((v).toInt()) // NOLINT(cppcoreguidelines-macro-usage)
// NOLINTBEGIN(bugprone-macro-parentheses)
#define IMPLEMENTATION_CONFIGURATION(type, valueName, ValueName, convertToType, convertToJsonValue) \
    type ServerConfiguration::valueName() const                                                     \
    {                                                                                               \
        if (contains(u"" #valueName ""_s))                                                          \
            return convertToType(value(u"" #valueName ""_s));                                       \
        return convertToType(defaults().value(u"" #valueName ""_s));                                \
    }                                                                                               \
    void ServerConfiguration::set##ValueName(type valueName)                                        \
    {                                                                                               \
        insert(u"" #valueName ""_s, convertToJsonValue(valueName));                                 \
    }

#define IMPLEMENTATION_CONFIGURATION_SETTER_CONST_REFERENCE(type, valueName, ValueName, convertToType, convertToJsonValue) \
    type ServerConfiguration::valueName() const                                                                            \
    {                                                                                                                      \
        if (contains(u"" #valueName ""_s))                                                                                 \
            return convertToType(value(u"" #valueName ""_s));                                                              \
        return convertToType(defaults().value(u"" #valueName ""_s));                                                       \
    }                                                                                                                      \
    void ServerConfiguration::set##ValueName(const type &valueName)                                                        \
    {                                                                                                                      \
        insert(u"" #valueName ""_s, convertToJsonValue(valueName));                                                        \
    }

IMPLEMENTATION_CONFIGURATION(bool, tcpEnabled, TcpEnabled, CONVERTTOTYPEBOOL, )
IMPLEMENTATION_CONFIGURATION(uint16_t, tcpPort, TcpPort, CONVERTTOTYPEUINT16T, )
IMPLEMENTATION_CONFIGURATION(bool, localEnabled, LocalEnabled, CONVERTTOTYPEBOOL, )
IMPLEMENTATION_CONFIGURATION_SETTER_CONST_REFERENCE(QString, localSocketName, LocalSocketName, CONVERTTOTYPEQSTRING, )
IMPLEMENTATION_CONFIGURATION(bool, websocketEnabled, WebsocketEnabled, CONVERTTOTYPEBOOL, )
IMPLEMENTATION_CONFIGURATION_SETTER_CONST_REFERENCE(QString, websocketName, WebsocketName, CONVERTTOTYPEQSTRING, )
IMPLEMENTATION_CONFIGURATION(uint16_t, websocketPort, WebsocketPort, CONVERTTOTYPEUINT16T, )
IMPLEMENTATION_CONFIGURATION(uint16_t, controlPort, ControlPort, CONVERTTOTYPEUINT16T, )
IMPLEMENTATION_CONFIGURATION(int, playerNumPerRoom, PlayerNumPerRoom, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, requestTimeout, RequestTimeout, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, maximumPacketSize, MaximumPacketSize, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, signInTimeout, SignInTimeout, CONVERTTOTYPEINT, )

#undef IMPLEMENTATION_CONFIGURATION_SETTER_CONST_REFERENCE
#undef IMPLEMENTATION_CONFIGURATION
#undef CONVERTTOTYPEQSTRING
#undef CONVERTTOTYPEUINT16T
#undef CONVERTTOTYPEBOOL
#undef CONVERTTOTYPEINT
// NOLINTEND(bugprone-macro-parentheses)

/**
 * @brief deserialize @c QJsonValue to @c ServerConfiguration
 * @param value the value to be deserialized
 * @return if the deserialize succeeded
 * @note It is possible to convert the value to @c QJsonObject and directly assign the value, since this class inherits @c QJsonObject, but the value check in this function will be nonexistent then.
 *
 * The value must be an object. Any absent key falls back to its default value (as returned by
 * @c defaults()), so a partial or empty object is accepted; a present key must still be valid. Boolean
 * fields must be booleans and string fields must be strings; every numeric field must be a whole number
 * (fractions, NaN and negatives are rejected). Ports must be in [1, 65535] (port 0 is reserved);
 * @c controlPort is either 0 (the plane is off, which is its default) or such a port;
 * @c playerNumPerRoom must be at least 2 (a game needs an opponent for rock-paper-scissors action-order
 * resolution); @c requestTimeout is either 0 (no timeout) or at least 15 seconds;
 * @c maximumPacketSize is at least 1024 bytes; @c signInTimeout is between 2 and 60 seconds.
 * Unknown keys are ignored.
 */
bool ServerConfiguration::deserialize(const QJsonValue &value) // NOLINT(readability-function-cognitive-complexity)
{
    if (!value.isObject())
        return false;

    const QJsonObject ob = value.toObject();
    QJsonObject result;

    // A numeric field must be a whole number: JSON numbers are doubles, so reject fractions
    // (e.g. 1.5), NaN, negatives, and values that do not fit in an int, which toInt() would
    // otherwise silently truncate or wrap.
    const auto parseNonNegativeInt = [](const QJsonValue &v, int *out) {
        if (!v.isDouble())
            return false;

        const double d = v.toDouble();
        if (d != std::floor(d) || d < 0.0 || d > static_cast<double>(std::numeric_limits<int>::max()))
            return false;

        *out = static_cast<int>(d);
        return true;
    };

#define CONF_BOOL(member)                                                \
    {                                                                    \
        if (ob.contains(u"" #member ""_s)) {                             \
            if (!ob.value(u"" #member ""_s).isBool())                    \
                return false;                                            \
            result.insert(u"" #member ""_s, ob.value(u"" #member ""_s)); \
        }                                                                \
    }

#define CONF_STRING(member)                                              \
    {                                                                    \
        if (ob.contains(u"" #member ""_s)) {                             \
            if (!ob.value(u"" #member ""_s).isString())                  \
                return false;                                            \
            result.insert(u"" #member ""_s, ob.value(u"" #member ""_s)); \
        }                                                                \
    }

#define CONF_PORT(member)                                                  \
    {                                                                      \
        int parsed = static_cast<int>(defaults().member());                \
        if (ob.contains(u"" #member ""_s)) {                               \
            if (!parseNonNegativeInt(ob.value(u"" #member ""_s), &parsed)) \
                return false;                                              \
            if (parsed == 0 || parsed > 65535)                             \
                return false;                                              \
            result.insert(u"" #member ""_s, parsed);                       \
        }                                                                  \
    }

    CONF_BOOL(tcpEnabled);
    CONF_PORT(tcpPort);
    CONF_BOOL(localEnabled);
    CONF_STRING(localSocketName);
    CONF_BOOL(websocketEnabled);
    CONF_STRING(websocketName);
    CONF_PORT(websocketPort);

#undef CONF_BOOL
#undef CONF_STRING

    // controlPort: 0 (off) or a real port. It cannot go through CONF_PORT, which is about the
    // transports: a transport with no port is not a transport, so 0 is refused there, while the
    // control plane has no reason to exist until it is asked for and 0 is exactly how that is said.
    {
        int parsed = 0;
        if (ob.contains(u"controlPort"_s)) {
            if (!parseNonNegativeInt(ob.value(u"controlPort"_s), &parsed))
                return false;
            if (parsed > 65535)
                return false;
            result.insert(u"controlPort"_s, parsed);
        }
    }

    // playerNumPerRoom: whole number >= 2 (a game needs at least two players).
    {
        int parsed = 0;
        if (ob.contains(u"playerNumPerRoom"_s)) {
            if (!parseNonNegativeInt(ob.value(u"playerNumPerRoom"_s), &parsed))
                return false;
            if (parsed < 2)
                return false;
            result.insert(u"playerNumPerRoom"_s, parsed);
        }
    }

    // requestTimeout: 0 (no timeout) or >= 15 seconds.
    {
        int parsed = 0;
        if (ob.contains(u"requestTimeout"_s)) {
            if (!parseNonNegativeInt(ob.value(u"requestTimeout"_s), &parsed))
                return false;
            if (parsed != 0 && parsed < 15)
                return false;
            result.insert(u"requestTimeout"_s, parsed);
        }
    }

    // maximumPacketSize: at least 1 KiB, so that the protocol's own packets always fit. There is
    // no "unlimited" value to fall back on -- accepting an unbounded amount of data from a peer is
    // the case this bound exists for.
    {
        int parsed = 0;
        if (ob.contains(u"maximumPacketSize"_s)) {
            if (!parseNonNegativeInt(ob.value(u"maximumPacketSize"_s), &parsed))
                return false;
            if (parsed < 1024)
                return false;
            result.insert(u"maximumPacketSize"_s, parsed);
        }
    }

    // signInTimeout: [2, 60] seconds. The upper bound is what keeps a peer that connects and
    // never signs in from holding a port and the memory behind it for longer than a handshake
    // could plausibly take; the lower bound only has to outlast the slowest honest handshake,
    // since a deadline shorter than that would give up on a client that is merely slow rather
    // than on one that is never going to sign in at all.
    {
        int parsed = 0;
        if (ob.contains(u"signInTimeout"_s)) {
            if (!parseNonNegativeInt(ob.value(u"signInTimeout"_s), &parsed))
                return false;
            if (parsed < 2 || parsed > 60)
                return false;
            result.insert(u"signInTimeout"_s, parsed);
        }
    }

    *this = std::move(result);
    return true;
}

/**
 * @class Server
 * @ingroup QMdmmNetworking
 * @brief The server that accepts connections and runs games.
 *
 * The server listens on the configured transports (TCP / local socket / WebSocket) and,
 * once enough players sign in, starts a @c LogicRunner for a complete game.
 *
 * It can also open a read-only control plane at @c ServerConfiguration::controlPort: an HTTP/1.0
 * listener on the loopback address that serves @c status() at @c /status. It is meant for whoever
 * is running the server, not for players, and is off unless a port is named.
 */

/**
 * @brief ctor.
 * @param serverConfiguration The configuration of the server
 * @param logicConfiguration The configuration of the logic used by the games
 * @param parent QObject parent.
 */
Server::Server(ServerConfiguration serverConfiguration, QMdmmCore::LogicConfiguration logicConfiguration, QObject *parent)
    : QObject(parent)
    , d(new p::ServerP(std::move(serverConfiguration), std::move(logicConfiguration), this))
{
}

/**
 * @brief Start listening on all enabled transports
 * @return @c true if all enabled transports are listening successfully
 *
 * Each enabled transport is started independently; a transport that fails to start emits
 * @c listenError() with its name and the underlying error string, so the caller can tell which
 * transport failed and why (the aggregate return value alone cannot). The return value is
 * @c false if any enabled transport failed.
 *
 * The control plane is started here too, but it is not one of the transports: it is opened only
 * when a port is configured for it, and it binds the loopback address, so it is reachable from
 * this machine and from nowhere else. Its name in @c listenError() is @c "control".
 */
bool Server::listen()
{
    bool ret = true;

    if (d->serverConfiguration.tcpEnabled() && !d->t->listen(QHostAddress::Any, d->serverConfiguration.tcpPort())) {
        emit listenError(u"tcp"_s, d->t->errorString(), QPrivateSignal());
        ret = false;
    }

    if (d->serverConfiguration.localEnabled() && !d->l->listen(d->serverConfiguration.localSocketName())) {
        emit listenError(u"local"_s, d->l->errorString(), QPrivateSignal());
        ret = false;
    }

    if (d->serverConfiguration.websocketEnabled() && !d->w->listen(QHostAddress::Any, d->serverConfiguration.websocketPort())) {
        emit listenError(u"websocket"_s, d->w->errorString(), QPrivateSignal());
        ret = false;
    }

    if (d->serverConfiguration.controlPort() != 0 && !d->c->listen(QHostAddress::LocalHost, d->serverConfiguration.controlPort())) {
        emit listenError(u"control"_s, d->c->errorString(), QPrivateSignal());
        ret = false;
    }

    return ret;
}

/**
 * @brief Stop listening on all enabled transports.
 *
 * Closes every listening socket that @c listen() started, releasing the ports / local-socket
 * names so they can be reused. Already-accepted connections are left untouched; only the
 * listening sockets are shut down. Safe to call even if the server is not currently listening.
 */
void Server::close()
{
    if (d->t != nullptr)
        d->t->close();
    if (d->l != nullptr)
        d->l->close();
    if (d->w != nullptr)
        d->w->close();
    if (d->c != nullptr)
        d->c->close();
}

/**
 * @brief Read a snapshot of what the server is doing right now
 * @return a @c QJsonObject with the fixed keys @c "rooms", @c "games", @c "players" and @c "roomList"
 *
 * @c "rooms" counts the rooms that exist right now: both the one still recruiting players and any
 * room whose game is running in the background. @c "games" counts how many of them have started a
 * game -- a room starts one as soon as it fills up. @c "players" counts the players currently
 * online across all rooms, so a player whose connection dropped and who has not reconnected yet is
 * not counted even though the seat is kept for them.
 *
 * @c "roomList" is the same rooms one by one, each an object with @c "id", @c "players" and
 * @c "rounds": the room's @c LogicRunner::id() (an unsigned 32-bit number, written as a JSON
 * number like the counts are), how many of its players are online, and how many rounds it has
 * started (@c LogicRunner::roundCount()). The entries are ordered by id, and their
 * player counts add up to the top-level @c "players" -- the two readings come from one pass, not
 * from two. A server with nothing going on answers with an empty array. The three counts are what
 * this key was added beside rather than in place of: their shape does not change, so a caller that
 * reads them keeps reading them.
 *
 * The snapshot is read-only and can be taken at any time; it changes nothing. It is also what the
 * control plane serves at @c /status, so the same readings are available to a caller that cannot
 * link against this library (see @c ServerConfiguration::controlPort).
 */
QJsonObject Server::status() const
{
    return d->status();
}

/**
 * @fn Server::listenError(const QString &transportName, const QString &errorString, QPrivateSignal)
 * @brief emitted when a transport fails to start listening in @c listen()
 * @param transportName which transport failed: @c "tcp", @c "local" or @c "websocket", or
 * @c "control" for the read-only control plane
 * @param errorString the transport's own description of the failure
 */

/**
 * @brief dtor.
 */
Server::~Server() = default;

#ifndef DOXYGEN
} // namespace v0
#endif
} // namespace QMdmmNetworking

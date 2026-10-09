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
 * before the connection is treated as gone. A value of 0 turns the timer off: the
 * server then never gives up on a silent client.
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
        qMakePair(u"playerNumPerRoom"_s, 3),
        qMakePair(u"requestTimeout"_s, 20),
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
IMPLEMENTATION_CONFIGURATION(int, playerNumPerRoom, PlayerNumPerRoom, CONVERTTOTYPEINT, )
IMPLEMENTATION_CONFIGURATION(int, requestTimeout, RequestTimeout, CONVERTTOTYPEINT, )

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
 * @c playerNumPerRoom must be at least 2 (a game needs an opponent for rock-paper-scissors action-order
 * resolution); @c requestTimeout is either 0 (no timeout) or at least 15 seconds.
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
}

/**
 * @fn Server::listenError(const QString &transportName, const QString &errorString, QPrivateSignal)
 * @brief emitted when a transport fails to start listening in @c listen()
 * @param transportName which transport failed: @c "tcp", @c "local" or @c "websocket"
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

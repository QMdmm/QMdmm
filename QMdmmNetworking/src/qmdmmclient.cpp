// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmclient.h"
#include "qmdmmclient_p.h"

#include <QMdmmRoom>

#include <QDateTime>
#include <QJsonArray>

#include <random>

using namespace Qt::StringLiterals;

/**
 * @file qmdmmclient.h
 * @brief This is the file where the networking Client is defined.
 */

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class ClientConfiguration
 * @ingroup QMdmmNetworking
 * @brief Contains configurations of client
 *
 * The configuration is a @c QVariantMap with well-known keys. Because the class inherits
 * @c QVariantMap, arbitrary keys can be inserted and values are not validated on access; unknown
 * keys are ignored.
 */

/**
 * @property ClientConfiguration::screenName
 * @brief The screen name of the client, default "QMdmm-Fans"
 */

/**
 * @fn ClientConfiguration::screenName() const
 * @brief getter of @c ClientConfiguration::screenName
 * @return @c ClientConfiguration::screenName
 */

/**
 * @fn ClientConfiguration::setScreenName(const QString &screenName)
 * @brief setter of @c ClientConfiguration::screenName
 * @param screenName @c ClientConfiguration::screenName
 */

/**
 * @brief Get default values of configuration
 * @return default configuration
 */
const ClientConfiguration &ClientConfiguration::defaults()
{
    // clang-format off
    static const ClientConfiguration defaultInstance {
        std::make_pair(u"screenName"_s, u"QMdmm-Fans"_s),
    };
    // clang-format on

    return defaultInstance;
}

#define CONVERTTOTYPEQSTRING(v) v.toString() // NOLINT(cppcoreguidelines-macro-usage)
// NOLINTBEGIN(bugprone-macro-parentheses)
#define IMPLEMENTATION_CONFIGURATION(type, valueName, ValueName, convertToType, convertToQVariant) \
    type ClientConfiguration::valueName() const                                                    \
    {                                                                                              \
        if (contains(u"" #valueName ""_s))                                                         \
            return convertToType(value(u"" #valueName ""_s));                                      \
        return convertToType(defaults().value(u"" #valueName ""_s));                               \
    }                                                                                              \
    void ClientConfiguration::set##ValueName(type valueName)                                       \
    {                                                                                              \
        insert(u"" #valueName ""_s, convertToQVariant(valueName));                                 \
    }

#define IMPLEMENTATION_CONFIGURATION2(type, valueName, ValueName, convertToType, convertToQVariant) \
    type ClientConfiguration::valueName() const                                                     \
    {                                                                                               \
        if (contains(u"" #valueName ""_s))                                                          \
            return convertToType(value(u"" #valueName ""_s));                                       \
        return convertToType(defaults().value(u"" #valueName ""_s));                                \
    }                                                                                               \
    void ClientConfiguration::set##ValueName(const type &valueName)                                 \
    {                                                                                               \
        insert(u"" #valueName ""_s, convertToQVariant(valueName));                                  \
    }

IMPLEMENTATION_CONFIGURATION2(QString, screenName, ScreenName, CONVERTTOTYPEQSTRING, )

#undef IMPLEMENTATION_CONFIGURATION2
#undef IMPLEMENTATION_CONFIGURATION
#undef CONVERTTOTYPEQSTRING
// NOLINTEND(bugprone-macro-parentheses)

namespace {
inline QString generateRandomString()
{
    static thread_local std::random_device random1;
    static thread_local std::mt19937 random2(random1());

    QByteArray arr;
    for (int i = 0; i < 30; ++i)
        arr.append(static_cast<char>(random2() % 255));

    return QString::fromLatin1(arr.toBase64(QByteArray::OmitTrailingEquals));
}
} // namespace

/**
 * @class Client
 * @ingroup QMdmmNetworking
 * @brief The client that connects to a server and plays the game.
 *
 * A Client maintains the connection to a server (through a @c Socket) and the local
 * @c Room where the game state is mirrored. It converges to a pure connection: the
 * controller interface (requests / notifications / replies / speech / operation) lives
 * on its own @c Agent (see @c Client::agent()), which a UI or an automated player drives.
 *
 * The client's objectName is its playerName: the constructor assigns a random objectName,
 * the self Agent is registered under that name, and the sign-in reports it to the server as
 * the playerName. playerName is therefore not configurable by design, and the operation
 * side must not rename the client (setObjectName()) while it is connected. Renaming
 * desynchronizes the self Agent's objectName key from the sign-in playerName, so the server
 * ends up tracking a different identity than the one the self Agent drives.
 */

/**
 * @brief ctor.
 * @param clientConfiguration The configuration of the client
 * @param parent QObject parent.
 */
Client::Client(ClientConfiguration clientConfiguration, QObject *parent)
    : QObject(parent)
    , d(new p::ClientP(std::move(clientConfiguration), this))
{
    setObjectName(generateRandomString());
    d->initSelfAgent();
}

/**
 * @brief dtor.
 */
Client::~Client() = default;

/**
 * @brief Connect to a server
 * @param host the host address to connect to (the scheme decides the transport, see @c Socket::connectToHost())
 * @param initialState the initial agent state used when signing in
 * @return @c true if the connection is initiated successfully, @c false otherwise
 */
bool Client::connectToHost(const QString &host, QMdmmCore::Data::AgentState initialState)
{
    if (d->socket != nullptr) {
        d->socket->disconnect(d);
        d->socket->deleteLater();
    }

    // Remember the host so the client can reconnect by itself after a drop. An
    // explicit connectToHost always starts a fresh session: clear any in-flight
    // reconnect state so a manual reconnect and the automatic retry never fight.
    d->host = host;
    d->initialState = initialState;
    d->reconnectAttempts = 0;
    d->reconnectInProgress = false;
    d->reconnectTimer->stop();

    return d->connectSocket();
}

/**
 * @brief Actively disconnect from the server.
 *
 * Stops the automatic reconnect loop and drops the underlying socket without going through the
 * "connection lost" path, so no @c socketConnectionLost / @c socketErrorDisconnected signal is
 * emitted (the caller already knows it disconnected). A later @c connectToHost() reconnects as a
 * fresh session.
 */
void Client::disconnectFromHost()
{
    d->reconnectTimer->stop();
    d->heartbeatTimer->stop();
    d->reconnectInProgress = false;
    d->connected = false;
    d->currentRequest = QMdmmCore::Protocol::RequestInvalid;

    if (d->socket != nullptr) {
        // Detach the socket's signals first so the drop does not enter handleSocketGone and
        // re-arm the reconnect loop.
        d->socket->disconnect(d);
        d->socket->disconnectFromHost();
        d->socket->deleteLater();
    }
}

/**
 * @brief whether the client currently has an active connection
 * @return @c true if a connection is up, @c false otherwise
 *
 * Returns @c true once @c connectToHost() successfully initiates the connection, and @c false
 * after a drop, an error, @c disconnectFromHost() or the server's game-over teardown.
 */
bool Client::isConnected() const
{
    return d->connected;
}

/**
 * @brief get the local room where the game state is mirrored
 * @return the local room; never @c nullptr (created on construction as an empty room, before the
 * connection is established)
 */
QMdmmCore::Room *Client::room()
{
    return d->room;
}

/**
 * @brief get the local room where the game state is mirrored (const version)
 * @return the local room; never @c nullptr (created on construction as an empty room, before the
 * connection is established)
 */
const QMdmmCore::Room *Client::room() const
{
    return d->room;
}

/**
 * @brief get this client's own agent
 * @return this client's own agent
 *
 * The client pre-creates its own Agent on construction (symmetric to the server side where the
 * operation side creates the agent and hands it to LogicRunner). The operation side (GUI / Bot)
 * drives the controller interface through it: incoming requests arrive on its xxxRequested
 * signals, and replies / speech / operation are sent back by calling its bare-verb methods
 * (rockPaperScissors / actionOrder / action / upgrade) and speak / operate.
 *
 * The returned pointer is the stable handle stored by initSelfAgent(), not a lookup by
 * objectName(). Renaming the client is forbidden (see the @c Client class doc); the stable
 * handle is defense-in-depth so agent() never detaches even if that invariant is violated.
 */
Agent *Client::agent()
{
    return d->selfAgent;
}

/**
 * @brief get this client's own agent (const version)
 * @return this client's own agent
 */
const Agent *Client::agent() const
{
    return d->selfAgent;
}

/**
 * @fn Client::socketConnectionLost(const QString &errorString, QPrivateSignal)
 * @brief emitted once when the connection drops and the client starts retrying internally
 * @param errorString the reason the connection dropped
 *
 * This is the "connection lost" notice for the upper layer, distinct from
 * @c socketErrorDisconnected(), which fires only after the automatic reconnect gives up.
 */

/**
 * @fn Client::socketReconnectSucceeded(QPrivateSignal)
 * @brief emitted when the client re-establishes the connection and re-signed in after a disconnect
 */

/**
 * @fn Client::socketErrorDisconnected(const QString &errorString, QPrivateSignal)
 * @brief emitted when the socket encounters an error and gets disconnected
 * @param errorString the error description
 */

#ifndef DOXYGEN
} // namespace v0
#endif
} // namespace QMdmmNetworking

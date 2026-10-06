// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmsocket.h"
#include "qmdmmsocket_p.h"

#include <QLocalSocket>
#include <QTcpSocket>

/**
 * @file qmdmmsocket.h
 * @brief This is the file where the networking Socket is defined.
 */

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace v0 {
#endif

/**
 * @class Socket
 * @ingroup QMdmmNetworking
 * @brief A wrapper around a concrete socket that serializes / deserializes packets.
 *
 * Socket abstracts over TCP socket, local socket and WebSocket transports. It exposes
 * packet-based signals so that the upper layers (@c Server, @c Client, @c Agent) can
 * work with @c QMdmmCore::Packet without caring about the underlying transport.
 */

/**
 * @enum Socket::Type
 * @ingroup QMdmmNetworking
 * @brief The type of the underlying transport.
 */

/**
 * @var Socket::Type Socket::TypeUnknown
 * @brief Unknown or invalid type.

 * @var Socket::Type Socket::TypeQTcpSocket
 * @brief TCP socket transport.

 * @var Socket::Type Socket::TypeQLocalSocket
 * @brief Local socket transport.

 * @var Socket::Type Socket::TypeQWebSocket
 * @brief WebSocket transport.
 */

/**
 * @enum Socket::ErrorCode
 * @ingroup QMdmmNetworking
 * @brief The kind of error a Socket can be in. Kept as a stable, transport-agnostic code rather than
 * a mapping of each transport's native error enums, so it does not change across Qt versions.
 */

/**
 * @var Socket::ErrorCode Socket::NoError
 * @brief No error occurred.

 * @var Socket::ErrorCode Socket::TransportError
 * @brief The underlying transport reported an error.

 * @var Socket::ErrorCode Socket::ProtocolError
 * @brief A malformed / invalid packet was received, or a protocol violation.
 */

/**
 * @struct Socket::Error
 * @ingroup QMdmmNetworking
 * @brief A value type describing a socket error: a stable code plus a human-readable description.
 */

/**
 * @var Socket::Error::code
 * @brief The stable error code.

 * @var Socket::Error::errorString
 * @brief A human-readable description of the error.
 */

/**
 * @brief ctor for server side, wrapping an already-open TCP socket
 * @param t the TCP socket
 * @param parent QObject parent.
 */
Socket::Socket(QTcpSocket *t, QObject *parent)
    : QObject(parent)
    , d(p::SocketPFactory::create(t, this))
{
}

/**
 * @brief ctor for server side, wrapping an already-open local socket
 * @param l the local socket
 * @param parent QObject parent.
 */
Socket::Socket(QLocalSocket *l, QObject *parent)
    : QObject(parent)
    , d(p::SocketPFactory::create(l, this))
{
}

/**
 * @brief ctor for server side, wrapping an already-open WebSocket
 * @param w the WebSocket
 * @param parent QObject parent.
 */
Socket::Socket(QWebSocket *w, QObject *parent)
    : QObject(parent)
    , d(p::SocketPFactory::create(w, this))
{
}

/**
 * @brief ctor for client side
 * @param parent QObject parent.
 *
 * The underlying transport is created lazily by @c connectToHost() based on the address.
 */
Socket::Socket(QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
}

/**
 * @brief dtor.
 */
Socket::~Socket() = default;

/**
 * @brief The transport type of this socket.
 * @return the underlying transport type, or @c TypeUnknown if no transport is active yet (a
 *         client-side socket that has not connected).
 */
Socket::Type Socket::type() const
{
    if (d != nullptr)
        return d->type();

    return TypeUnknown;
}

/**
 * @brief Mark the socket as errored and disconnect the underlying transport.
 * @param error the error to record
 *
 * Stores the error and disconnects the underlying transport, which walks the full disconnect
 * path on its own and emits @c socketDisconnected, so upper layers detect protocol violations
 * through this and run their disconnect handling. Callers must not disconnect again or emit
 * @c socketDisconnected manually. Unlike the transport error path, it does not emit
 * @c socketErrorOccurred (mirroring the previous @c setHasError behaviour).
 */
void Socket::setError(const Error &error)
{
    if (d != nullptr) {
        d->error = error;
        d->disconnectFromHost();
    }
}

/**
 * @brief The recorded error, if any.
 * @return the socket's error, or @c std::nullopt if no error has occurred (including before any
 *         transport has been created)
 */
std::optional<Socket::Error> Socket::error() const
{
    if (d != nullptr)
        return d->error;

    return std::nullopt;
}

/**
 * @brief if the socket is in error state
 * @return @c true if the socket has an error
 */
bool Socket::hasError() const
{
    return error().has_value();
}

/**
 * @brief Connect to a host
 * @param host the address to connect to. The scheme decides the transport: @c qmdmm
 *             for TCP, @c ws / @c wss for WebSocket, and a plain (non-URL) string
 *             for local socket. The scheme is matched case-insensitively (RFC 3986).
 * @return @c true if the connection is initiated successfully, @c false if the address
 *         cannot be parsed to a known transport
 */
bool Socket::connectToHost(const QString &host)
{
    Type t = p::SocketP::typeByConnectAddr(host);
    if (t == TypeUnknown)
        return false;

    if (d != nullptr) {
        d->disconnectFromHost();
        d->deleteLater();
    }

    d = p::SocketPFactory::create(t, this);
    if (d == nullptr)
        return false;
    return d->connectToHost(host);
}

/**
 * @brief Actively disconnect the underlying transport.
 *
 * Disconnects the underlying socket (and releases it) if one is currently active. The
 * wrapper itself stays usable: a later @c connectToHost() creates a fresh transport.
 */
void Socket::disconnectFromHost()
{
    if (d != nullptr)
        d->disconnectFromHost();
}

/**
 * @fn Socket::sendPacket(QMdmmCore::Packet packet)
 * @brief emitted when a packet should be sent to the peer
 * @param packet the packet to be sent
 */

/**
 * @fn Socket::packetReceived(QMdmmCore::Packet packet, QPrivateSignal)
 * @brief emitted when a complete packet is received from the peer
 * @param packet the received packet
 */

/**
 * @fn Socket::socketErrorOccurred(Error error, QPrivateSignal)
 * @brief emitted when a socket error occurs
 * @param error the error, carrying a stable code and a human-readable description
 */

/**
 * @fn Socket::socketDisconnected(QPrivateSignal)
 * @brief emitted when the socket is disconnected
 */

#ifndef DOXYGEN
} // namespace v0
#endif
} // namespace QMdmmNetworking

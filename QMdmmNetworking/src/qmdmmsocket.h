// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMSOCKET_H
#define QMDMMSOCKET_H

#include "qmdmmnetworkingglobal.h"

#include <QMdmmPacket>

#include <QLocalSocket>
#include <QObject>
#include <QTcpSocket>
#include <QWebSocket>

#include <cstdint>
#include <optional>

QMDMM_EXPORT_NAME(QMdmmSocket)

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace p {
class SocketP;
} // namespace p
#endif

#ifndef DOXYGEN
namespace v0 {
#endif

class QMDMMNETWORKING_EXPORT Socket : public QObject
{
    Q_OBJECT

public:
    enum Type : uint8_t
    {
        TypeUnknown,
        TypeQTcpSocket,
        TypeQLocalSocket,
        TypeQWebSocket,
    };

    enum ErrorCode : uint8_t
    {
        NoError,
        TransportError,
        ProtocolError,
    };

    // The bound a socket starts with: one packet is one line, so a peer that has sent this many
    // bytes without ending a line is not speaking the protocol and is given up on. It is far
    // above any packet this protocol produces (they are small JSON objects), so it does not get
    // in the way of a legitimate peer; it only keeps a peer from filling memory with data that
    // can never become a packet.
    static constexpr int DefaultMaximumPacketSize = 1024 * 1024;

    struct Error
    {
        ErrorCode code = NoError;
        QString errorString;
    };

    Q_DISABLE_COPY_MOVE(Socket);

    explicit Socket(QTcpSocket *t, QObject *parent = nullptr);
    explicit Socket(QLocalSocket *l, QObject *parent = nullptr);
    explicit Socket(QWebSocket *w, QObject *parent = nullptr);

    explicit Socket(QObject *parent = nullptr);
    ~Socket() override;

    [[nodiscard]] Type type() const;

    [[nodiscard]] int maximumPacketSize() const;
    void setMaximumPacketSize(int maximumPacketSize);

    void setError(const Error &error);
    [[nodiscard]] std::optional<Error> error() const;
    [[nodiscard]] bool hasError() const;

    bool connectToHost(const QString &host);
    void disconnectFromHost();

signals:
    void sendPacket(QMdmmCore::Packet);

    void packetReceived(QMdmmCore::Packet, QPrivateSignal);
    void socketErrorOccurred(Error error, QPrivateSignal);
    void socketDisconnected(QPrivateSignal);

#ifndef DOXYGEN
private:
    friend class p::SocketP;
    // non-const d-ptr.
    // SocketP is pv class and its internal implementation varies by socket type.
    // function connectToHost alters this d-ptr with proper implementation.
    // SocketP is QObject. QPointer can't be used since it is incomplete here
    p::SocketP *d;

    // Kept here as well as in the implementation so that a bound set before a transport exists
    // survives the transport being created later (a client-side socket creates its transport in
    // connectToHost, long after the caller may have set this). Named apart from the accessors:
    // a data member and a member function cannot share a name.
    int packetSizeBound;
#endif
};

#ifndef DOXYGEN
} // namespace v0

inline namespace v1 {
using v0::Socket;
}
#endif

} // namespace QMdmmNetworking

Q_DECLARE_METATYPE(QMdmmNetworking::Socket::Error)

#endif

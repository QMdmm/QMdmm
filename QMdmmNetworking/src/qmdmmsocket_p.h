// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMSOCKET_P
#define QMDMMSOCKET_P

#include "qmdmmsocket.h"

#include <QObject>
#include <QPointer>
#include <optional>

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): This is private header
namespace QMdmmNetworking {
namespace p {
class QMDMMNETWORKING_PRIVATE_EXPORT SocketP : public QObject
{
    Q_OBJECT

public:
    static Socket::Type typeByConnectAddr(const QString &addr);

    explicit SocketP(Socket *q);
    [[nodiscard]] virtual Socket::Type type() const = 0;

    virtual bool connectToHost(const QString &addr) = 0;
    virtual bool disconnectFromHost() = 0;

    // The bound lives here, next to the transport that has to honour it.
    void setMaximumPacketSize(int maximumPacketSize);

    Socket *q;
    std::optional<Socket::Error> error;
    int maximumPacketSize = Socket::DefaultMaximumPacketSize;

public slots: // NOLINT(readability-redundant-access-specifiers)
    virtual void sendPacket(QMdmmCore::Packet packet) = 0;
    bool packetReceived(const QByteArray &arr);
    void socketDisconnected();
    void errorOccurred(const Socket::Error &error);

protected:
    // Push the bound onto the transport itself, so it stops reading from the peer once the bound
    // is reached instead of buffering an unbounded amount of data. The transports that frame by
    // lines implement this; a WebSocket delivers whole messages and has nothing to cap here.
    virtual void applyMaximumPacketSize();

    // Called once a transport has no complete line left to hand over: if what it still holds has
    // already reached the bound, the peer is not ending its lines at all, so the connection is
    // given up on (a @c ProtocolError, exactly as for any other unusable packet).
    // Returns @c true when it did that, in which case the caller must stop touching the transport.
    bool giveUpOnUnterminatedData(qint64 availableBytes);
};

class QMDMMNETWORKING_PRIVATE_EXPORT SocketP_QTcpSocket : public SocketP
{
    Q_OBJECT

public:
    SocketP_QTcpSocket(QTcpSocket *socket, Socket *q);
    explicit SocketP_QTcpSocket(Socket *q);

    [[nodiscard]] Socket::Type type() const override
    {
        return Socket::TypeQTcpSocket;
    }

    bool connectToHost(const QString &addr) override;
    bool disconnectFromHost() override;

    QPointer<QTcpSocket> socket;
    void setupSocket();

protected:
    void applyMaximumPacketSize() override;

public slots: // NOLINT(readability-redundant-access-specifiers)
    void sendPacket(QMdmmCore::Packet packet) override;
    void readyRead();
    void errorOccurredTcpSocket(QAbstractSocket::SocketError e);
};

class QMDMMNETWORKING_PRIVATE_EXPORT SocketP_QLocalSocket : public SocketP
{
    Q_OBJECT

public:
    SocketP_QLocalSocket(QLocalSocket *socket, Socket *q);
    explicit SocketP_QLocalSocket(Socket *q);

    [[nodiscard]] Socket::Type type() const override
    {
        return Socket::TypeQLocalSocket;
    }

    bool connectToHost(const QString &addr) override;
    bool disconnectFromHost() override;

    QPointer<QLocalSocket> socket;
    void setupSocket();

protected:
    void applyMaximumPacketSize() override;

public slots: // NOLINT(readability-redundant-access-specifiers)
    void sendPacket(QMdmmCore::Packet packet) override;
    void readyRead();
    void errorOccurredLocalSocket(QLocalSocket::LocalSocketError e);
};

class QMDMMNETWORKING_PRIVATE_EXPORT SocketP_QWebSocket : public SocketP
{
    Q_OBJECT

public:
    SocketP_QWebSocket(QWebSocket *socket, Socket *q);
    explicit SocketP_QWebSocket(Socket *q);

    [[nodiscard]] Socket::Type type() const override
    {
        return Socket::TypeQWebSocket;
    }

    bool connectToHost(const QString &addr) override;
    bool disconnectFromHost() override;

    QPointer<QWebSocket> socket;
    void setupSocket();

public slots: // NOLINT(readability-redundant-access-specifiers)
    void sendPacket(QMdmmCore::Packet packet) override;
    void errorOccurredWebSocket(QAbstractSocket::SocketError e);
};

namespace SocketPFactory {
QMDMMNETWORKING_PRIVATE_EXPORT SocketP *create(QLocalSocket *l, Socket *p);
QMDMMNETWORKING_PRIVATE_EXPORT SocketP *create(QTcpSocket *t, Socket *p);
QMDMMNETWORKING_PRIVATE_EXPORT SocketP *create(QWebSocket *w, Socket *p);
QMDMMNETWORKING_PRIVATE_EXPORT SocketP *create(Socket::Type type, Socket *p);
} // namespace SocketPFactory
} // namespace p
} // namespace QMdmmNetworking

// NOLINTEND(misc-non-private-member-variables-in-classes): This is private header

#endif

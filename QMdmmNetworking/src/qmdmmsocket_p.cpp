// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmsocket_p.h"

using namespace Qt::StringLiterals;

namespace QMdmmNetworking {
namespace p {

namespace SocketPFactory {
SocketP *create(QTcpSocket *t, Socket *p)
{
    return new SocketP_QTcpSocket(t, p);
}
SocketP *create(QLocalSocket *l, Socket *p)
{
    return new SocketP_QLocalSocket(l, p);
}
SocketP *create(QWebSocket *w, Socket *p)
{
    return new SocketP_QWebSocket(w, p);
}

SocketP *create(Socket::Type type, Socket *p)
{
    switch (type) {
    case Socket::TypeQTcpSocket:
        return new SocketP_QTcpSocket(p);
    case Socket::TypeQLocalSocket:
        return new SocketP_QLocalSocket(p);
    case Socket::TypeQWebSocket:
        return new SocketP_QWebSocket(p);
    default:
        break;
    }

    return nullptr;
}
} // namespace SocketPFactory

Socket::Type SocketP::typeByConnectAddr(const QString &addr)
{
    // Prefix whitelist decides the transport. The prefixes are matched case-insensitively:
    // URI schemes are case-insensitive (RFC 3986), so "QMDMM://host" names the same
    // transport as "qmdmm://host". Note that qmdmms:// is deliberately NOT
    // accepted: it would silently promise TLS over a still-plaintext transport, the
    // worst combination. Re-add it once TLS is actually implemented.
    if (addr.startsWith(u"qmdmm://"_s, Qt::CaseInsensitive))
        return Socket::TypeQTcpSocket;
    if (addr.startsWith(u"ws://"_s, Qt::CaseInsensitive) || addr.startsWith(u"wss://"_s, Qt::CaseInsensitive))
        return Socket::TypeQWebSocket;
    if (!addr.contains(u"://"_s))
        return Socket::TypeQLocalSocket;

    return Socket::TypeUnknown;
}

SocketP::SocketP(Socket *q)
    : QObject(q)
    , q(q)
{
    connect(q, &Socket::sendPacket, this, &SocketP::sendPacket);
}

void SocketP::setMaximumPacketSize(int _maximumPacketSize)
{
    maximumPacketSize = _maximumPacketSize;
    applyMaximumPacketSize();
}

void SocketP::applyMaximumPacketSize()
{
    // Nothing to cap by default: only the transports that frame packets by lines have a buffer
    // that a peer can grow.
}

// NOLINTNEXTLINE(readability-make-member-function-const)
bool SocketP::giveUpOnUnterminatedData(qint64 availableBytes)
{
    if (maximumPacketSize <= 0 || availableBytes < maximumPacketSize)
        return false;

    const Socket::Error err {.code = Socket::ProtocolError, .errorString = u"the peer sent %1 bytes without ending a packet"_s.arg(availableBytes)};
    errorOccurred(err);
    q->setError(err);
    return true;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
bool SocketP::packetReceived(const QByteArray &arr)
{
    QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(arr);

    QString packetError;
    if (packet.hasError(&packetError)) {
        // Don't process more package for this connection. It is not guaranteed to be the desired client
        const Socket::Error err {.code = Socket::ProtocolError, .errorString = packetError};
        errorOccurred(err);
        q->setError(err);
        return false;
    }

    emit q->packetReceived(packet, Socket::QPrivateSignal());
    return !error.has_value();
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void SocketP::socketDisconnected()
{
    emit q->socketDisconnected(Socket::QPrivateSignal());
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void SocketP::errorOccurred(const Socket::Error &_error)
{
    error = _error;
    emit q->socketErrorOccurred(_error, Socket::QPrivateSignal());
}

SocketP_QTcpSocket::SocketP_QTcpSocket(QTcpSocket *socket, Socket *q)
    : SocketP(q)
    , socket(socket)
{
    if (socket != nullptr)
        setupSocket();
}

SocketP_QTcpSocket::SocketP_QTcpSocket(Socket *q)
    : SocketP_QTcpSocket(nullptr, q)
{
}

bool SocketP_QTcpSocket::connectToHost(const QString &addr)
{
    if (socket != nullptr)
        socket->deleteLater();

    socket = new QTcpSocket(this);
    setupSocket();
    QUrl u(addr);
    QString host = u.host();
    uint16_t port = u.port(6366);

    socket->connectToHost(host, port);
    return true;
}

bool SocketP_QTcpSocket::disconnectFromHost()
{
    if (socket != nullptr) {
        socket->disconnectFromHost();
        socket->deleteLater();
        return true;
    }
    return false;
}

void SocketP_QTcpSocket::setupSocket()
{
    connect(socket, &QTcpSocket::readyRead, this, &SocketP_QTcpSocket::readyRead);
    connect(this, &SocketP_QTcpSocket::destroyed, socket, &QTcpSocket::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, this, &SocketP_QTcpSocket::errorOccurredTcpSocket);
    connect(socket, &QTcpSocket::disconnected, this, &SocketP_QTcpSocket::socketDisconnected);
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    // Through the class name on purpose: this also runs from the constructor, where an unqualified
    // call would resolve to the SocketP virtual (the dispatch for an object under construction)
    // and silently do nothing.
    SocketP_QTcpSocket::applyMaximumPacketSize();
}

void SocketP_QTcpSocket::applyMaximumPacketSize()
{
    if (socket == nullptr)
        return;

    // A non-positive bound means "no bound", which is what a read buffer size of 0 asks Qt for.
    socket->setReadBufferSize(maximumPacketSize > 0 ? static_cast<qint64>(maximumPacketSize) : 0);
}

void SocketP_QTcpSocket::sendPacket(QMdmmCore::Packet packet)
{
    if (socket != nullptr) {
        socket->write(QByteArray(packet).append("\n"_ba));
        socket->flush();
    }
}

void SocketP_QTcpSocket::readyRead()
{
    if (socket == nullptr)
        return;

    while (socket->canReadLine()) {
        const QByteArray arr = socket->readLine();
        if (!packetReceived(arr))
            return; // the connection is already being given up on
    }

    giveUpOnUnterminatedData(socket->bytesAvailable());
}

void SocketP_QTcpSocket::errorOccurredTcpSocket(QAbstractSocket::SocketError /*e*/)
{
    if (socket != nullptr)
        errorOccurred(Socket::Error {.code = Socket::TransportError, .errorString = socket->errorString()});
}

SocketP_QLocalSocket::SocketP_QLocalSocket(QLocalSocket *socket, Socket *q)
    : SocketP(q)
    , socket(socket)
{
    if (socket != nullptr)
        setupSocket();
}

SocketP_QLocalSocket::SocketP_QLocalSocket(Socket *q)
    : SocketP_QLocalSocket(nullptr, q)
{
}

bool SocketP_QLocalSocket::connectToHost(const QString &addr)
{
    if (socket != nullptr)
        socket->deleteLater();

    socket = new QLocalSocket(this);
    setupSocket();
    socket->connectToServer(addr);
    return true;
}

bool SocketP_QLocalSocket::disconnectFromHost()
{
    if (socket != nullptr) {
        socket->disconnectFromServer();
        socket->deleteLater();
        return true;
    }
    return false;
}

void SocketP_QLocalSocket::setupSocket()
{
    connect(socket, &QLocalSocket::readyRead, this, &SocketP_QLocalSocket::readyRead);
    connect(this, &SocketP_QLocalSocket::destroyed, socket, &QLocalSocket::deleteLater);
    connect(socket, &QLocalSocket::errorOccurred, this, &SocketP_QLocalSocket::errorOccurredLocalSocket);
    connect(socket, &QLocalSocket::disconnected, this, &SocketP_QLocalSocket::socketDisconnected);
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);

    // Through the class name on purpose: this also runs from the constructor, where an unqualified
    // call would resolve to the SocketP virtual (the dispatch for an object under construction)
    // and silently do nothing.
    SocketP_QLocalSocket::applyMaximumPacketSize();
}

void SocketP_QLocalSocket::applyMaximumPacketSize()
{
    if (socket == nullptr)
        return;

    // A non-positive bound means "no bound", which is what a read buffer size of 0 asks Qt for.
    socket->setReadBufferSize(maximumPacketSize > 0 ? static_cast<qint64>(maximumPacketSize) : 0);
}

void SocketP_QLocalSocket::sendPacket(QMdmmCore::Packet packet)
{
    if (socket != nullptr) {
        socket->write(QByteArray(packet).append("\n"_ba));
        socket->flush();
    }
}

void SocketP_QLocalSocket::readyRead()
{
    if (socket == nullptr)
        return;

    while (socket->canReadLine()) {
        const QByteArray arr = socket->readLine();
        if (!packetReceived(arr))
            return; // the connection is already being given up on
    }

    giveUpOnUnterminatedData(socket->bytesAvailable());
}

void SocketP_QLocalSocket::errorOccurredLocalSocket(QLocalSocket::LocalSocketError /*e*/)
{
    if (socket != nullptr)
        errorOccurred(Socket::Error {.code = Socket::TransportError, .errorString = socket->errorString()});
}

SocketP_QWebSocket::SocketP_QWebSocket(QWebSocket *socket, Socket *q)
    : SocketP(q)
    , socket(socket)
{
    if (socket != nullptr)
        setupSocket();
}

SocketP_QWebSocket::SocketP_QWebSocket(Socket *q)
    : SocketP_QWebSocket(nullptr, q)
{
}

bool SocketP_QWebSocket::connectToHost(const QString &addr)
{
    if (socket != nullptr)
        socket->deleteLater();

    // The origin is what rides along in the handshake's Origin header, and nothing more:
    // the server never looks at it, so this names the client, it does not guard anything.
    socket = new QWebSocket(u"fsu0413.me"_s, QWebSocketProtocol::VersionLatest, this);
    setupSocket();
    QUrl url(addr);
    socket->open(url);

    return true;
}

bool SocketP_QWebSocket::disconnectFromHost()
{
    if (socket != nullptr) {
        socket->close();
        socket->deleteLater();
        return true;
    }
    return false;
}

void SocketP_QWebSocket::setupSocket()
{
    connect(socket, &QWebSocket::binaryMessageReceived, this, &SocketP_QWebSocket::packetReceived);
    connect(this, &SocketP_QWebSocket::destroyed, socket, &QWebSocket::deleteLater);
    connect(socket, &QWebSocket::errorOccurred, this, &SocketP_QWebSocket::errorOccurredWebSocket);
    connect(socket, &QWebSocket::disconnected, this, &SocketP_QWebSocket::socketDisconnected);
    connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);
}

void SocketP_QWebSocket::sendPacket(QMdmmCore::Packet packet)
{
    if (socket != nullptr)
        socket->sendBinaryMessage(packet);
}

void SocketP_QWebSocket::errorOccurredWebSocket(QAbstractSocket::SocketError /*e*/)
{
    if (socket != nullptr)
        errorOccurred(Socket::Error {.code = Socket::TransportError, .errorString = socket->errorString()});
}
} // namespace p
} // namespace QMdmmNetworking

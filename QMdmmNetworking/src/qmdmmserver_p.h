// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMSERVER_P
#define QMDMMSERVER_P

#include "qmdmmlogicrunner.h"
#include "qmdmmserver.h"
#include "qmdmmsocket.h"

#include <QMdmmPacket>

#include <QHash>
#include <QIODevice>
#include <QJsonObject>
#include <QList>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): This is private header

namespace QMdmmNetworking {
namespace p {

class QMDMMNETWORKING_PRIVATE_EXPORT ServerP final : public QObject
{
    Q_OBJECT

    // The protocol dispatch table is returned from an accessor rather than declared as a static
    // data member: building it allocates, and a failure must reach the caller instead of
    // terminating the process during static initialization (cert-err58-cpp).
    static const QHash<QMdmmCore::Protocol::NotifyId, void (ServerP::*)(Socket *, const QJsonValue &)> &notifyCallbacks();

public:
    ServerP(ServerConfiguration serverConfiguration, QMdmmCore::LogicConfiguration logicConfiguration, Server *q);

    // callbacks
    void pingServer(Socket *socket, const QJsonValue &packetValue);
    void signIn(Socket *socket, const QJsonValue &packetValue);
    void stateChange(Socket *socket, const QJsonValue &packetValue);
    void observe(Socket *socket, const QJsonValue &packetValue);

    void introduceSocket(Socket *socket);

    // The control plane is not a transport: it never becomes a Socket and never carries protocol
    // frames. It reads one HTTP request line and answers with status().
    void serveControlRequest(QTcpSocket *socket);

    [[nodiscard]] QJsonObject status() const;

public slots: // NOLINT(readability-redundant-access-specifiers)
    void tcpServerNewConnection();
    void localServerNewConnection();
    void websocketServerNewConnection();
    void controlServerNewConnection();
    void socketPacketReceived(const QMdmmCore::Packet &packet);

    void logicRunnerGameOver();

public: // NOLINT(readability-redundant-access-specifiers)
    // variables
    ServerConfiguration serverConfiguration;
    QMdmmCore::LogicConfiguration logicConfiguration;
    Server *q;
    QTcpServer *t;
    QLocalServer *l;
    QWebSocketServer *w;
    QTcpServer *c;
    LogicRunner *current;
};

} // namespace p
} // namespace QMdmmNetworking

// NOLINTEND(misc-non-private-member-variables-in-classes): This is private header

#endif

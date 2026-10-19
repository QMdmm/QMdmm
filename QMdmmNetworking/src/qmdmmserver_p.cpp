// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmserver_p.h"

#include "qmdmmagent.h"
#include "qmdmmserverconnection_p.h"
#include "qmdmmsocket.h"

#include <QMdmmProtocol>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace QMdmmNetworking {
namespace p {

const QHash<QMdmmCore::Protocol::NotifyId, void (ServerP::*)(Socket *, const QJsonValue &)> &ServerP::notifyCallbacks()
{
    static const QHash<QMdmmCore::Protocol::NotifyId, void (ServerP::*)(Socket *, const QJsonValue &)> callbacks {
        std::make_pair(QMdmmCore::Protocol::NotifyPingServer, &ServerP::pingServer),
        std::make_pair(QMdmmCore::Protocol::NotifySignIn, &ServerP::signIn),
        std::make_pair(QMdmmCore::Protocol::NotifyStateChange, &ServerP::stateChange),
        std::make_pair(QMdmmCore::Protocol::NotifyObserve, &ServerP::observe),
    };
    return callbacks;
}

ServerP::ServerP(ServerConfiguration serverConfiguration_, QMdmmCore::LogicConfiguration logicConfiguration, Server *q)
    : QObject(q)
    , serverConfiguration(std::move(serverConfiguration_))
    , logicConfiguration(std::move(logicConfiguration))
    , q(q)
    , t(nullptr)
    , l(nullptr)
    , w(nullptr)
    , c(nullptr)
    , current(nullptr)
{
    // Tcp
    if (serverConfiguration.tcpEnabled()) {
        t = new QTcpServer(this);

        // Qt post-6.4: new pendingConnectionAvailable signal, emitted after connection is added to pending connection queue
        // instead of the connection is established (Pre 6.3 behavior)
        connect(t, &QTcpServer::pendingConnectionAvailable, this, &ServerP::tcpServerNewConnection);
    }

    // Local
    if (serverConfiguration.localEnabled()) {
        l = new QLocalServer(this);
        l->setSocketOptions(QLocalServer::WorldAccessOption);
        connect(l, &QLocalServer::newConnection, this, &ServerP::localServerNewConnection);
    }

    // WebSocket
    if (serverConfiguration.websocketEnabled()) {
        w = new QWebSocketServer(serverConfiguration.websocketName(), QWebSocketServer::NonSecureMode, this);
        connect(w, &QWebSocketServer::newConnection, this, &ServerP::websocketServerNewConnection);
    }

    // Control plane. Unlike the transports above it is not created from an on/off flag: a port of
    // its own is what asks for it, and 0 -- the default -- is not a port.
    if (serverConfiguration.controlPort() != 0) {
        c = new QTcpServer(this);
        connect(c, &QTcpServer::newConnection, this, &ServerP::controlServerNewConnection);
    }
}

void ServerP::pingServer(Socket *socket, const QJsonValue &packetValue)
{
    emit socket->sendPacket(QMdmmCore::Packet(QMdmmCore::Protocol::NotifyPongServer, packetValue));
}

void ServerP::signIn(Socket *socket, const QJsonValue &packetValue)
{
    // NOLINTBEGIN(cppcoreguidelines-avoid-do-while,cppcoreguidelines-macro-usage)
    do {
        if (!packetValue.isObject())
            break;

        QJsonObject ob = packetValue.toObject();

#define CONVERTAGENTSTATE() QMdmmCore::Data::AgentState(v.toInt())

        // NOLINTBEGIN(bugprone-macro-parentheses)

        // no do .. while (0) here since I'd like 'break' to exit outside this block
        // where "socket->setError(...)" should be done
#define CONF(member, check, convert)               \
    {                                              \
        if (!ob.contains(u"" #member ""_s))        \
            break;                                 \
        QJsonValue v = ob.value(u"" #member ""_s); \
        if (!v.check())                            \
            break;                                 \
        member = convert();                        \
    }

        // NOLINTEND(bugprone-macro-parentheses)

        QString playerName;
        CONF(playerName, isString, v.toString);

        QString screenName;
        CONF(screenName, isString, v.toString);

        QMdmmCore::Data::AgentState agentState;
        CONF(agentState, isDouble, CONVERTAGENTSTATE);

#undef CONF
#undef CONVERTAGENTSTATE

        // The client reports how many round events it received before a drop, so the server can
        // replay only the events it missed (precise catch-up). A fresh sign-in omits the field and
        // defaults to 0 -- harmless, since a fresh room has an empty round-event log.
        int lastRoundEventSeq = 0;
        if (ob.contains(u"lastRoundEventSeq"_s)) {
            QJsonValue vlastRoundEventSeq = ob.value(u"lastRoundEventSeq"_s);
            if (vlastRoundEventSeq.isDouble())
                lastRoundEventSeq = vlastRoundEventSeq.toInt();
        }

        // Reconnect path: a reconnecting player may live in ANY room, not just `current`. `current`
        // only tracks the room currently recruiting players; full rooms keep running in the
        // background and are deleted later on gameOver. So scan every LogicRunner child for the
        // offline agent and reconnect it in whichever room it is found. A still-online duplicate
        // name falls through to addAgent below, which rejects it as a spurious "new player" error.
        const QList<LogicRunner *> runners = findChildren<LogicRunner *>();
        for (LogicRunner *runner : runners) {
            Agent *existing = runner->agent(playerName);
            if (existing == nullptr || existing->state().testFlag(QMdmmCore::Data::StateMaskOnline))
                continue;

            // D-018: the socket is digested at the wire layer and the room only deals with agents,
            // so a reconnect is split across the two. Find the agent's wire plumbing, rebind the
            // socket + replay the missed round events on it, then restore the agent's state +
            // snapshot on the logic side.
            p::ServerConnectionP *conn = existing->findChild<p::ServerConnectionP *>();
            if (conn == nullptr) {
                // A local agent has no wire; a sign-in over the wire cannot reconnect it.
                socket->setError({.code = Socket::ProtocolError, .errorString = {}});
                return;
            }

            conn->reconnect(socket, lastRoundEventSeq);
            if (runner->reconnectAgent(existing) != nullptr)
                return;

            socket->setError({.code = Socket::ProtocolError, .errorString = {}});
            return;
        }

        if (current == nullptr || current->full()) {
            current = new LogicRunner(logicConfiguration, serverConfiguration.playerNumPerRoom(), this);
            connect(current, &LogicRunner::gameOver, this, &ServerP::logicRunnerGameOver);
        }

        // Assemble the agent on the operation side (network path): create the agent (identity +
        // controller) and its wire plumbing (ServerConnectionP), bind the socket, then register the
        // whole thing with the room via addAgent. The ServerConnectionP is a child of the agent so
        // it travels with it; it reports socket drops as an Agent event the room listens to.
        Agent *agent = new Agent(playerName, current);
        agent->setScreenName(screenName);
        agent->setState(agentState);
        p::ServerConnectionP *conn = new p::ServerConnectionP(agent, logicConfiguration, serverConfiguration.requestTimeout(), agent);
        conn->setSocket(socket);

        if (current->addAgent(agent) == nullptr)
            break;

        return;
    } while (false);

    socket->setError({.code = Socket::ProtocolError, .errorString = {}});
    // NOLINTEND(cppcoreguidelines-avoid-do-while,cppcoreguidelines-macro-usage)
}

void ServerP::stateChange(Socket *socket, const QJsonValue &packetValue)
{
    // The runtime counterpart of the state a sign-in carries: the player's own client declares the
    // state it holds for itself while the connection is up, and the whole of it is applied here --
    // the Trust flag among the rest, not on its own. The socket identifies the player, so a
    // socket-bound agent's wire plumbing is what gets looked up (the connection is a child of its
    // agent, D-018); a socket that has not signed in has neither a connection nor an agent to
    // update.
    if (packetValue.isObject()) {
        QJsonValue vmanaged = packetValue.toObject().value(u"agentState"_s);
        if (vmanaged.isDouble()) {
            for (p::ServerConnectionP *connection : findChildren<p::ServerConnectionP *>()) {
                if (connection->socket != socket)
                    continue;

                connection->agent->setState({vmanaged.toInt()});
                return;
            }
        }
    }

    socket->setError({.code = Socket::ProtocolError, .errorString = {}});
}

void ServerP::observe(Socket *socket, const QJsonValue &packetValue)
{
    // TODO
    Q_UNUSED(packetValue);
    socket->setError({.code = Socket::ProtocolError, .errorString = {}});
}

void ServerP::introduceSocket(Socket *socket) // NOLINT(readability-make-member-function-const)
{
    connect(socket, &Socket::packetReceived, this, &ServerP::socketPacketReceived);

    // One packet is one line, so a peer that never ends a line must not be allowed to make this
    // process grow: the socket is bounded by the configured packet size and gives up on such a peer.
    socket->setMaximumPacketSize(serverConfiguration.maximumPacketSize());

    QJsonObject ob;
    ob.insert(u"versionNumber"_s, QMdmmCore::Global::version().toString());
    ob.insert(u"protocolVersion"_s, QMdmmCore::Protocol::version());
    QMdmmCore::Packet packet(QMdmmCore::Protocol::NotifyVersion, ob);
    emit socket->sendPacket(packet);

    // A peer that connects and never signs in is not a player yet, so nothing else in this server
    // would ever notice it: left to itself, the connection (and the memory behind it) is held for
    // as long as the peer cares to keep the socket open. The deadline is armed here -- the one
    // place every transport's accepted socket passes through -- and judged when it fires: a socket
    // that has become an agent's wire in the meantime has signed in and is left alone, since a
    // signed-in agent that goes quiet is the connection's own request timer's business.
    const int signInTimeout = serverConfiguration.signInTimeout();
    QTimer *deadline = new QTimer(socket);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, socket, [this, socket, deadline, signInTimeout] {
        deadline->deleteLater();

        // signIn() builds the connection object that makes a socket an agent's wire. It is a
        // descendant of this server, and the socket it was handed is the one this deadline
        // watches, so the presence of that pair is exactly "this socket signed in".
        for (const p::ServerConnectionP *connection : findChildren<p::ServerConnectionP *>()) {
            if (connection->socket == socket)
                return;
        }

        socket->setError({.code = Socket::ProtocolError, .errorString = u"the peer did not sign in within %1 second(s)"_s.arg(signInTimeout)});
    });
    deadline->start(signInTimeout * 1000);
}

QJsonObject ServerP::status() const
{
    int rooms = 0;
    int games = 0;
    int players = 0;

    // A room is a LogicRunner. Every runner is visited, not just `current`: a full room keeps
    // running in the background while `current` moves on to the next recruiter (see signIn above).
    // A room whose game has started is exactly a full one -- both addAgent() and agentDisconnected()
    // switch on full() to tell "the game is running" from "still recruiting".
    QList<LogicRunner *> runners = findChildren<LogicRunner *>();

    // The per-room entries are ordered by room id, not by whatever order the children happen to be
    // in: the snapshot is something a caller can diff between readings, and a list that reshuffles
    // when a room is built would make that impossible.
    std::ranges::sort(runners, [](const LogicRunner *left, const LogicRunner *right) { return left->id() < right->id(); });

    QJsonArray roomList;
    for (const LogicRunner *runner : runners) {
        ++rooms;

        if (runner->full())
            ++games;

        // The agents are children of the runner (signIn parents a network agent to its runner, and
        // a local agent follows the same shape), so a runner's own subtree holds the room's players.
        // Counted the same way as the top-level total -- online only -- so the entries add up to it.
        int roomPlayers = 0;
        const QList<Agent *> agents = runner->findChildren<Agent *>();
        for (const Agent *agent : agents) {
            if (agent->state().testFlag(QMdmmCore::Data::StateMaskOnline))
                ++roomPlayers;
        }
        players += roomPlayers;

        QJsonObject room;
        // The id is unsigned, and QJsonValue has no unsigned constructor: passing it through
        // qint64 keeps every 32-bit id exact and leaves the constructor overload unambiguous.
        room.insert(u"id"_s, static_cast<qint64>(runner->id()));
        room.insert(u"players"_s, roomPlayers);
        room.insert(u"rounds"_s, runner->roundCount());
        roomList.append(room);
    }

    QJsonObject ob;
    ob.insert(u"rooms"_s, rooms);
    ob.insert(u"games"_s, games);
    ob.insert(u"players"_s, players);
    ob.insert(u"roomList"_s, roomList);
    return ob;
}

void ServerP::tcpServerNewConnection()
{
    while (t->hasPendingConnections()) {
        QTcpSocket *socket = t->nextPendingConnection();
        Socket *mdmmSocket = new Socket(socket, this);
        introduceSocket(mdmmSocket);
    }
}

void ServerP::localServerNewConnection()
{
    while (l->hasPendingConnections()) {
        QLocalSocket *socket = l->nextPendingConnection();
        Socket *mdmmSocket = new Socket(socket, this);
        introduceSocket(mdmmSocket);
    }
}

void ServerP::websocketServerNewConnection()
{
    while (w->hasPendingConnections()) {
        QWebSocket *socket = w->nextPendingConnection();
        Socket *mdmmSocket = new Socket(socket, this);
        introduceSocket(mdmmSocket);
    }
}

namespace {

// How much of a request the control plane reads before giving up on it. A GET carries no body and
// the request line is the whole of what this plane looks at, so anything past this is a peer that
// is not asking a question: it is dropped rather than buffered without bound.
constexpr qsizetype controlRequestSizeLimit = 8192;

// How long a control-plane connection gets to deliver its request. This plane answers one short
// request per connection and closes, so a peer that connects and then says nothing has nothing to
// ask -- and the exit above is one such a peer never reaches, whatever it holds back. Without a
// deadline it keeps the socket, the buffer behind it and this server's memory for as long as it
// cares to, which is a resource bound every other accepted socket in this server has (the sign-in
// deadline, the request timer) and this one did not.
//
// Fixed rather than configured: what it bounds is not a policy an operator tunes but a peer that
// has not spoken at all, and it never cuts a request that has arrived -- the answer closes the
// connection on its own. Five seconds is orders of magnitude more than a loopback client needs to
// write a request line.
constexpr int controlRequestTimeout = 5;

// One HTTP/1.0 response, header and body together. HTTP/1.0 with no keep-alive means a connection
// carries exactly one exchange and then closes, which is what lets the caller be a bare client
// that negotiates nothing. The length is what lets it read the body without guessing where it ends.
QByteArray controlResponse(const QByteArray &statusLine, const QByteArray &contentType, const QByteArray &body)
{
    QByteArray response;
    response += "HTTP/1.0 "_ba;
    response += statusLine;
    response += "\r\nContent-Type: "_ba;
    response += contentType;
    response += "\r\nContent-Length: "_ba;
    response += QByteArray::number(static_cast<qlonglong>(body.size()));
    response += "\r\nConnection: close\r\n\r\n"_ba;
    response += body;
    return response;
}

} // namespace

void ServerP::controlServerNewConnection()
{
    while (c->hasPendingConnections())
        serveControlRequest(c->nextPendingConnection());
}

void ServerP::serveControlRequest(QTcpSocket *socket)
{
    // Both of these are shared with the lambda below rather than owned by it: a request arrives in
    // as many pieces as the socket cares to deliver it in, so what has been read so far, and
    // whether it has been answered, have to outlive any single delivery.
    std::shared_ptr<QByteArray> received = std::make_shared<QByteArray>();
    std::shared_ptr<bool> answered = std::make_shared<bool>(false);

    // Armed at accept rather than at the first byte: what this bounds is a connection that has not
    // asked anything yet, so waiting for the peer to speak would miss exactly the peer it is for.
    // It is a child of the socket, so it goes away with it.
    QTimer *deadline = new QTimer(socket);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, socket, [socket] { socket->disconnectFromHost(); });
    deadline->start(controlRequestTimeout * 1000);

    connect(socket, &QTcpSocket::readyRead, socket, [this, socket, received, answered, deadline] {
        if (*answered)
            return;

        received->append(socket->readAll());

        // The request ends at the blank line that closes its header block. Waiting for that is
        // what keeps a request split across several reads from being answered twice.
        const qsizetype headerEnd = received->indexOf("\r\n\r\n"_ba);
        if (headerEnd < 0) {
            if (received->size() > controlRequestSizeLimit)
                socket->disconnectFromHost();
            return;
        }

        *answered = true;

        // The request arrived in time: what ends this connection from here is the answer, not the
        // clock, so the deadline stops where the request stops.
        deadline->stop();

        // Only the request line is read -- "GET /status HTTP/1.0". The two words it must carry are
        // the whole of what this plane serves, and a request that does not have them in that shape
        // is answered as a miss rather than guessed at.
        const QList<QByteArray> requestLine = received->left(headerEnd).split('\n').value(0).trimmed().split(' ');
        const bool asksForTheSnapshot = requestLine.size() >= 2 && requestLine.at(0) == "GET"_ba && requestLine.at(1) == "/status"_ba;

        if (asksForTheSnapshot) {
            const QByteArray body = QJsonDocument(status()).toJson(QJsonDocument::Compact);
            socket->write(controlResponse("200 OK"_ba, "application/json"_ba, body));
        } else {
            socket->write(controlResponse("404 Not Found"_ba, "text/plain"_ba, "no such resource\n"_ba));
        }

        // Closing is what tells the caller the answer is complete: there is no chunking and no
        // keep-alive here for a caller to negotiate.
        socket->disconnectFromHost();
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void ServerP::socketPacketReceived(const QMdmmCore::Packet &packet)
{
    Socket *socket = qobject_cast<Socket *>(sender());

    if (socket == nullptr)
        return;

    if (packet.type() == QMdmmCore::Protocol::TypeNotify) {
        if ((packet.notifyId() & QMdmmCore::Protocol::NotifyToServerMask) != 0) {
            // These packages should be processed in Server
            void (ServerP::*call)(Socket *, const QJsonValue &) = notifyCallbacks().value(packet.notifyId(), nullptr);
            if (call != nullptr)
                (this->*call)(socket, packet.value());
            else
                socket->setError({.code = Socket::ProtocolError, .errorString = {}});
        }
    }
}

void ServerP::logicRunnerGameOver()
{
    if (LogicRunner *runner = qobject_cast<LogicRunner *>(sender()); runner != nullptr) {
        if (current == runner)
            current = nullptr;

        runner->deleteLater();
    }
}

} // namespace p
} // namespace QMdmmNetworking

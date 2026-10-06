/**
 * \defgroup QMdmmNetworking QMdmm Networking library
 *
 * The network layer. It knows how players connect and exchange JSON packets,
 * but delegates every rule decision to \ref QMdmmCore.
 *
 * @c Server accepts connections and manages the rooms: each sign-in joins the
 * recruiting room, and once that room is full it is handed to a
 * @c LogicRunner, which carries one complete game -- a @c QMdmmCore::Logic on
 * a worker thread, plus one @c Agent and one @c ServerConnection per player.
 * @c Client is the other end: it signs in, keeps a local mirror of the game
 * state, and turns the traffic into request signals and reply slots.
 * @c Socket is the single class behind all three transports (TCP, local
 * socket and WebSocket).
 *
 * Nothing here is tied to a user interface or to a particular executable --
 * the CLI server, the GUI and the bots are all just @c Client / @c Server
 * users.
 */

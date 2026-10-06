# Concepts

The mental model behind QMdmm, for someone building on its libraries or its
programs rather than playing the game. [Architecture](architecture.md) is the
companion page: it lists the types each library holds. This page is about what
those parts are for and how they meet.

## Two libraries, two kinds of knowledge

The split is the design:

- **`QMdmmCore` knows how a game of QMdmm is played.** It owns the rules: the
  round state machine, the players, the room, and the configuration that says
  how a game behaves. It has no network and no threads of its own; every
  decision goes in as a slot call and comes back out as a signal.
- **`QMdmmNetworking` knows how players connect.** It owns the sockets, the
  packet protocol, the server, the client, and the record of who is sitting
  where. It makes no rule decision of its own: a request is forwarded to a
  client, the answer is carried back, and `QMdmmCore` decides what it means.

So the rules can be driven with nothing but `QMdmmCore` -- the rules tests do
exactly that -- and the same game can be carried over the wire with nothing but
`QMdmmNetworking`: connect a `Client` and answer the same requests. Neither
library has to know how the other does its job.

## The joints between them

Two objects are where a game becomes concrete, one on each side:

- **`LogicRunner` (server side)** is the server's unit of one game, one per
  room. It holds both halves of that game: the `Logic` that decides it, and, for
  each player, an `Agent` paired one-to-one with a `ServerConnection` (the
  player record, and the socket that player speaks through). Its job is to be
  the seam -- `Logic`'s request signals leave as packets for the right
  connection, and the connections' replies come back into `Logic`'s slots.
  Nothing else crosses.
- **`Client` (client side)** is one player's end of that game. It speaks only
  the packet protocol, and keeps its own `Room` as a *mirror* of the state. The
  server side holds the truth; the client converges to it, and is told what it
  missed after a reconnect.

Two properties of that seam matter when you build on it:

- **`Logic` runs on a worker thread; the connections live on the server
  thread.** The crossings are queued in both directions, so a slow client cannot
  stall the rules and a rules computation cannot block the server from taking
  players.
- **Which side owns the state is never in doubt.** The rules-side state is the
  only source of truth; everything on the client side is a mirror that exists to
  be rendered, and that is rebuilt by replaying what it missed.

## The three programs

The programs add no layer of their own -- each is a thin `main()` around the
libraries:

- **`QMdmmServer`** reads its command-line options into a `ServerConfiguration`
  and a `LogicConfiguration`, builds a `Server`, and listens. It is the
  reference user of the server half of `QMdmmNetworking`.
- **`QMdmmGui`** is the reference user of the client half, with one twist: a
  local game makes it a host as well. It starts a `QMdmmServer` of its own on a
  local socket and fills the remaining seats with `QMdmmBot` processes.
- **`QMdmmBot`** is a `Client` with an auto-player on top. It takes a seat,
  decides each request with a bot strategy, and answers through the same reply
  slots any other client would use.

If you want to write a player of your own, `QMdmmBot` and the smoke test's
auto-player (`QMdmmNetworking/test/tst_smoke_main.cpp`) are the two worked
examples; [Getting Started](getting-started.md) shows how to run the latter and
how to drive a `Client` directly.

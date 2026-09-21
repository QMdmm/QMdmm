# Architecture

A high-level overview of how QMdmm is put together. Assumes you have read the
[README](../README.md).

## Two layers

QMdmm splits cleanly into two libraries plus a few thin executables:

- **`QMdmmCore`** — the game rules engine. Pure logic, no network, no I/O. It
  knows how a game of QMdmm is played, but not how players connect.
- **`QMdmmNetworking`** — the network layer. It knows how players connect and
  exchange JSON packets, but delegates every rule decision to `QMdmmCore`.

The bridge between the two is `LogicRunner`: it owns one `QMdmmCore::Logic` (the
round state machine) and, per connected player, an `Agent` (the player record)
paired with a `ServerConnection` (the socket plumbing), and shuttles requests
and replies between them.

```
QMdmmServer ──► Server ──► LogicRunner ──► QMdmmCore::Logic
  (CLI)         (listens)    │  (one game)      (state machine)
                             │
                   Agent + ServerConnection ◄── Socket ──► Client ──► QMdmmGui / QMdmmBot
                   (one pair per player)      (TCP/local/WS)  (mirrors state)
```

## QMdmmCore

The engine. The important types:

- **`Logic`** — the round state machine. It walks through `BeforeRoundStart →
  RpsForAction → ActionOrder → RpsForActionOrder → Action → Upgrade` (see
  `Logic::State`). Drive it by calling its reply slots (`rpsReply`,
  `actionOrderReply`, `actionReply`, `upgradeReply`); it reacts by emitting
  request signals (`requestRpsForAction`, `requestActionOrder`,
  `requestAction`, `requestUpgrade`) and result signals (`RpsResult`,
  `actionOrderResult`, `actionResult`, `roundOver`, `upgradeResult`,
  `gameOver`).
- **`Room`** — a set of `Player`s plus a `LogicConfiguration`. Tracks alive /
  dead, and answers `isRoundOver()` / `isGameOver()`.
- **`Player`** — one player's state: HP, knife, horse, position, upgrade points.
- **`LogicConfiguration`** — the game rules (damage and HP ranges, punish
  rules, the LetMove toggle, …). JSON-serializable.
- **`Data`** — enums and flags: `RockPaperScissors`, `Action`, `UpgradeItem`,
  `Place`, `AgentState`, `DamageReason`.
- **`Protocol`** — the wire format: `Packet` (`Request` / `Reply` / `Notify`)
  and the `RequestId` / `NotifyId` enums.

`Logic` is transport-agnostic: it only emits signals and accepts slot calls,
which is what makes it unit-testable with no network involved.

## QMdmmNetworking

- **`Server`** — accepts connections and manages game rooms. Holds a
  `ServerConfiguration` (which transports to listen on, the players-per-room
  size, and the request timeout) and a `LogicConfiguration` (the rules for
  every game). Each `signIn` adds a player
  to the recruiting room or, once that room is full, spins up a new
  `LogicRunner` for the next room.
- **`Client`** — the player-facing end. `connectToHost` + sign-in, then it
  exposes request signals and reply slots mirroring `Logic`, and keeps a local
  `Room` mirror of the game state.
- **`LogicRunner`** — one complete game. Owns a `Logic` (moved to a dedicated
  worker thread) and, per player, an `Agent` plus a `ServerConnection`.
- **`Agent`** — the server's record of one player: name, screen name,
  `AgentState`.
- **`ServerConnection`** — the wire side of one player: the `Socket`, the
  request timer, and the protocol dispatch. Paired one-to-one with an `Agent`.
- **`Socket`** — a thin wrapper over `QTcpSocket` / `QLocalSocket` /
  `QWebSocket` that serializes and deserializes `Packet`s. One class, three
  transports.

### The LogicRunner bridge

`LogicRunnerP` is the glue. It connects `Logic`'s request signals to the
`ServerConnection` request slots (which send a `Request` packet to that player's
client) and the `ServerConnection` reply callbacks back to `Logic`'s reply slots.
Because `Logic` lives on a worker thread while the agents live on the server
thread, these connections are queued.

A full round flows like this:

1. The room fills → `Logic::roundStart()`.
2. `Logic` emits `requestRpsForAction` → every agent asks its client for a
   rock-paper-scissors pick → `rpsResult` decides the acting order.
3. `Logic` emits `requestActionOrder` (a tie-break RPS if needed) → the order
   is fixed.
4. `Logic` emits `requestAction` for each player in order → each acts (buy /
   slash / kick / move / let-move) → `actionResult`.
5. The round ends (`roundOver`) → survivors earn upgrade points →
   `requestUpgrade` → `upgradeResult`.
6. `Logic` checks `Room::isGameOver()`; if someone maxed out all three stats,
   `gameOver`, otherwise the next round starts.

The client never talks to `Logic` directly — only through the packets
`ServerConnection` relays.

### Reconnect

When a player disconnects, the server keeps their seat: the agent stays in the
room with its online / trusted state cleared. A reconnecting client re-signs in
with the same player name; the server finds the offline agent, rebinds its
socket, and replays the round events the client missed so its mirror converges
(the "precise catch-up" path).

## The executables

- **`QMdmmServer`** — a thin `main()` that reads CLI options into a
  `ServerConfiguration` + `LogicConfiguration`, builds a `Server`, and calls
  `listen()`.
- **`QMdmmGui`** — the QML client. It plays full games: a local game brings up a
  server plus a few auto-replying bots inside its own process and joins them over
  loopback TCP, and an online game connects to a running `QMdmmServer` over TCP or
  WebSocket. Still in progress: running that local server and those bots as
  separate processes.
- **`QMdmmBot`** — a scripted client. It reuses `Client` and `Agent` like
  `QMdmmGui`, but drives them with a bot strategy (chosen with `-s`:
  `knifePreferred` / `horsePreferred` / `rl`) instead of a human.
  `knifePreferred` and `horsePreferred` play real strategies; `rl` is still a
  placeholder and refuses to start.
- **`smoke`** — a headless end-to-end test: an in-process `Server` plus N
  auto-driven `Client`s (one human + bots) play a full game over loopback TCP,
  including a mid-game disconnect/reconnect.

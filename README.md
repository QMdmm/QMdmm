# QMdmm

A networked, turn-based battle game built with Qt 6 / C++20. It recreates a
schoolyard game that was popular during recess — a light strategy brawler where
you buy gear, pick fights, and level up to win.

> Nine years in the making: the engine is largely done, the frontend steering
> wheel is still on the drawing board.

## The game

QMdmm is a multiplayer, turn-based brawler. Players act in turns each round;
slaying an opponent earns upgrade points, and the first player to max out
everything wins.

Each player has:

- **HP** — reset to the player's max HP at the start of every round.
- **Knife** and **horse** — weapons that must be bought before attacking.
- **Position** — players move between places on the map.
- **Upgrade points** — earned by slaying opponents, spent on upgrades.

There are three upgradable stats: **knife damage**, **horse damage**, and
**max HP**. The first player to fully upgrade all three wins the game.

### Actions

| Action | Effect |
|---|---|
| `DoNothing` | Pass the turn. |
| `BuyKnife` | Buy a knife (required before `Slash`). |
| `BuyHorse` | Buy a horse (required before `Kick`). |
| `Slash` | Knife attack on a player standing in the same place. |
| `Kick` | Horse attack on a player standing in the same place. |
| `Move` | Move to an adjacent place. |
| `LetMove` | Pull an adjacent player toward you, or push a player sharing your place away. |

Both `Slash` and `Kick` are melee attacks that require the target to stand in
the same place; `Kick` cannot be used in the `Village` place. `LetMove` is
config-gated (`enableLetMove`).

### Round flow

1. **Rock-paper-scissors** decides the acting order.
2. Players act in turn — buy, attack, move, pull.
3. A round ends when at most one player is left standing; survivors gain
   upgrade points and the next round begins.

## Project status

| Module | Purpose | Status |
|---|---|---|
| `QMdmmCore` | Game rules engine (players / rooms / round state machine / config) | Usable, tested |
| `QMdmmNetworking` | Network layer (server / client / signaling) | Main flow + reconnect work; spectate / lobby missing |
| `QMdmmServer` | Standalone server program | Runs; full CLI configuration |
| `QMdmmGui` | Graphical client (QML) | Playable: a local game (a spawned `QMdmmServer` process + in-process bots) or an online one |
| `QMdmmBot` | Scripted client that plays automatically via a bot strategy | Runs; `knifePreferred` / `horsePreferred` implemented, `rl` not yet |

In other words: the GUI plays full games now. A local game starts a `QMdmmServer`
process of its own and joins it over a local socket, with a few auto-replying
bots for the other seats still inside the GUI's own process; an online game
connects to a running `QMdmmServer` over TCP or WebSocket. Still in progress:
running those bots as separate `QMdmmBot` processes.

## Documentation

- [Architecture](doc/architecture.md) — how the modules fit together, and how
  a round actually runs.
- [Getting Started](doc/getting-started.md) — build, run, and play a headless
  game end-to-end.
- [API reference](https://nemn9852.github.io/qmdmm-docs/) — Doxygen for the
  `QMdmmCore` and `QMdmmNetworking` public classes.

## Building

Requirements:

- CMake ≥ 3.19
- Qt ≥ 6.7 (Core / Network / WebSockets / Gui / Qml / Quick / Widgets / QuickWidgets)
- A C++20 compiler

```sh
cd ../
mkdir build-QMdmm-Release
cd build-QMdmm-Release
cmake \
   -DCMAKE_PREFIX_PATH=/path/to/Qt \
   -GNinja \
   -DCMAKE_BUILD_TYPE=Release \
   ../QMdmm
cmake --build . --parallel --
```

Add `-DBUILD_TESTING=ON` to enable tests. Binaries land in `../build-QMdmm-Release/build/bin/`.

## Running

Start the server first (TCP on port 6366 and WebSocket on port 6367 by
default):

```sh
../build-QMdmm-Release/build/bin/QMdmmServer6
```

The server accepts a full set of command-line options (room size, damage
values, timeouts, transport toggles, ...). Run `--help` to see the table;
`-d, --show-current-configuration` prints the configuration the server actually
resolved as JSON, and `-c` / `-C` store it. During v0 the placement of
configuration keys may still change.

Then start a client:

```sh
../build-QMdmm-Release/build/bin/QMdmm6
```

## Testing

Enable `BUILD_TESTING` at configure time, then:

```sh
cd ../build-QMdmm-Release
ctest --test-dir . --output-on-failure
```

## Network protocol

The wire protocol is JSON-based. Packets come in three kinds
(`Protocol::PacketType`):

- **Request** — the server (`Logic`) asks an agent to make a decision:
  `RockPaperScissors`, `ActionOrder`, `Action`, `Upgrade`.
- **Reply** — the agent's answer to a request.
- **Notify** — a one-way message. Four masks split notifications by direction:

| Mask | Direction | Examples |
|---|---|---|
| `NotifyFromServerMask` | server → client | `Pong`, `Version` |
| `NotifyFromAgentMask` | broadcast game state | `PlayerAdded`, `GameStart`, `RoundStart`, `Action`, `RoundOver`, `GameOver`, `Spoken` |
| `NotifyToServerMask` | client → server | `PingServer`, `SignIn`, `Observe` |
| `NotifyToAgentMask` | server → a specific agent | `Speak`, `Operate` |

Transports: TCP (default port 6366), local sockets, and WebSocket (default
port 6367).

## Directory layout

```
QMdmmCore/       Game rules engine
QMdmmNetworking/ Network layer (server / client / protocol transport)
QMdmmServer/     Standalone server program
QMdmmGui/        Graphical client (QML)
QMdmmBot/        Scripted client that plays automatically (bot strategies)
smoke/           Headless networked-gameplay regression test
doc/             Documentation: Doxygen config + guides (architecture, getting started)
3rdparty/        Third-party dependencies
cmake/           CMake helper modules
```

## License

[AGPL-3.0-or-later](LICENSE)

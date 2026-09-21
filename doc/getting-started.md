# Getting Started

Build QMdmm and run a complete game end-to-end. The GUI plays full games too;
the "play a game" section below uses the headless smoke test (bots) because it
needs no display, and it runs the same in-process server + clients the GUI's
local-game mode is built on.

## Prerequisites

- CMake ≥ 3.19
- Qt ≥ 6.7 (Core / Network / WebSockets / Gui / Qml / Quick / Widgets /
  QuickWidgets)
- A C++20 compiler

## Build

```sh
qt-cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
ninja -C build
```

Binaries land in `build/build/bin/`.

## Run the server

```sh
./build/build/bin/QMdmmServer6
```

By default it listens for TCP on port 6366 and WebSocket on port 6367. It takes
a full set of command-line options (room size, damage and HP values, timeouts,
transport toggles, …); run `--help` to see the table.

To see the configuration the server actually resolved, run it with
`-d, --show-current-configuration`, which prints the whole configuration as
JSON. `-c, --save-configuration` and `-C, --save-global-configuration` instead
store the fully resolved configuration (defaults included) in the per-user and
system-global settings stores, and exit.

Stored keys carry the long option names and are grouped by what they configure:
transports, room size and the request timeout under `server`; the game rules
(damage, HP and punish values) under `logic`. While QMdmm is still at v0, a key
may move between groups, and a value stored under the old name is then ignored,
falling back to the default.

## Run a client (GUI)

```sh
./build/build/bin/QMdmm6
```

The start menu's "Start game" screen plays a full match: a local game brings up
a server and a few auto-replying bots inside the GUI's own process, and an
online game connects to a running `QMdmmServer`. What is still in progress is
running that local server and those bots as separate `QMdmmServer` / `QMdmmBot`
processes.

## Play a headless game (bots)

The smoke test spins up an in-process server plus two auto-driven clients and
plays a full game to completion, including a mid-game disconnect/reconnect:

```sh
ctest --test-dir build -R qmdmm_smoke --output-on-failure
```

## Run the full test suite

```sh
ctest --test-dir build --output-on-failure
```

## Drive a client yourself

To control a `Client` programmatically (your own bot or a custom frontend),
connect to its request signals and answer through its reply slots:

| Request signal | Reply slot |
|---|---|
| `requestRockPaperScissors` | `replyRockPaperScissors` |
| `requestActionOrder` | `replyActionOrder` |
| `requestAction` | `replyAction` |
| `requestUpgrade` | `replyUpgrade` |

`smoke/main.cpp` contains a complete, competent auto-player (buy a knife, slash
a co-located enemy, otherwise walk toward one, and spend every upgrade point)
you can copy from.

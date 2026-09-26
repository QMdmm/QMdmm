# Getting Started

Build QMdmm and run a complete game end-to-end. The GUI plays full games too;
the "play a game" section below uses the headless smoke test (bots) because it
needs no display, and that test brings up its own in-process server + clients.

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

## Install (macOS)

Installing is `cmake --install`, and on macOS it comes in two shapes, selected
by `QMDMM_MACOS_APP_BUNDLE` (default `ON` on macOS; the other platforms always
install in the plain shape).

### Self-contained bundle (default)

The GUI is installed as `QMdmm6.app` at the top of the prefix, and it carries
what it needs: the Qt libraries, the QML modules, the platform plugins, and the
`QMdmmServer6` / `QMdmmBot6` programs it starts as child processes.

That step deploys Qt into the bundle, so it wants an official Qt archive rather
than a package manager's build of Qt:

```sh
pip install aqtinstall
aqt install-qt mac desktop <qt-version> clang_64 -m qtwebsockets --outputdir ~/Qt
```

Then point the build at it:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$HOME/Qt/<qt-version>/macos" \
    -DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew
cmake --build build
cmake --install build --prefix <prefix>
```

`CMAKE_PREFIX_PATH` already wins over the system prefixes on its own; the
`CMAKE_IGNORE_PREFIX_PATH` is there because on Apple Silicon CMake puts
`/opt/homebrew` into `CMAKE_SYSTEM_PREFIX_PATH` by itself, whatever `PATH`
holds.

Homebrew's Qt does not work for this shape: its QML plugins are symlinks into
the Homebrew cellar, and copying those into the bundle leaves them dangling, so
the installed `.app` does not start.

### Plain prefix layout

Configure with `-DQMDMM_MACOS_APP_BUNDLE=OFF` and the three programs are
installed as siblings in `<prefix>/bin/` instead, with Qt expected on the
machine. No Qt content is copied, so a Qt from a package manager - Homebrew
included - is fine here. This is the shape a distribution package or a bottle
is built from.

### Disk image

The bundle shape also produces a disk image:

```sh
cpack --config build/CPackConfig.cmake -G DragNDrop
```

It holds the application, a symlink to `/Applications`, and a readme. The app is
neither signed with a Developer ID certificate nor notarized, so macOS refuses
the first start on a machine that did not build it; the readme inside the image
has the two ways past that.

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

### Where the settings are stored

Both stores are INI files carrying the same file name:

| Store | File |
|---|---|
| per-user | `$HOME/.QMdmm/Fsu0413.me/QMdmm.ini` |
| system-global | `<configuration prefix>/Fsu0413.me/QMdmm.ini` |

The configuration prefix is fixed when the project is configured: `/etc/QMdmm`
when the install prefix is `/usr` or `/`, and `<install prefix>/etc/QMdmm`
otherwise, so a default build with prefix `/usr/local` writes to
`/usr/local/etc/QMdmm`.

A value is looked up in three places, in order: the value given on the command
line, then the per-user file, then the system-global file. The first place that
holds the key wins, so a per-user value overrides a system-global one.

`-c, --save-configuration` writes the per-user file and `-C,
--save-global-configuration` the system-global one; the run then exits with the
`QSettings::Status` of the save as its exit code, so 0 means it went through.
The system-global file usually sits in a root-owned directory, and the per-user
file overrides it anyway - when the target cannot be written the server says so
on stderr and exits non-zero rather than quietly storing the values elsewhere.

## Run bots against a server

`QMdmmBot` is a client with an auto-player on top of it: it connects, takes a
seat, and answers whatever the server asks it. Start a server with three seats,
then start one bot per seat:

```sh
./build/build/bin/QMdmmServer6 -3
```

```sh
./build/build/bin/QMdmmBot6 -l qmdmm://127.0.0.1:6366 -n Blade -s knifePreferred
./build/build/bin/QMdmmBot6 -l qmdmm://127.0.0.1:6366 -n Hoof -s horsePreferred
./build/build/bin/QMdmmBot6 -l qmdmm://127.0.0.1:6366 -n Spur -s knifePreferred
```

(`-3` is shorthand for `--players=3`.) The server starts the game as soon as the
last seat is taken. With no human in the room nothing waits on input, so the
bots play the game out at once; the room then goes quiet, the server takes the
next room, and each bot stays in its event loop.

### Addresses

The bot's `-l, --host` takes an address, and what comes before `://` picks the
transport:

| Address | Transport |
|---|---|
| `qmdmm://host:port` | TCP; the port defaults to 6366 |
| `ws://host:port`, `wss://host:port` | WebSocket (give a port; the server's is 6367) |
| a bare name, with no `://` in it | local socket, named by the server's `-L, --local-name` (default `QMdmm`) |

The scheme is matched case-insensitively, as URI schemes are (RFC 3986). Any
other scheme is refused outright rather than guessed at, `qmdmms://` included:
it would promise TLS over a transport that is still plaintext.

The server listens on TCP, on WebSocket and on the local socket, all three at
once by default; `--tcp`, `--websocket` and `--local` turn each one on or off.
The GUI's online mode takes the same kind of address as the bot's `--host`.

## Run a client (GUI)

```sh
./build/build/bin/QMdmm6
```

The start menu's "Start game" screen plays a full match: a local game starts a
`QMdmmServer` process of its own and connects to it over a local socket, filling
the other seats with `QMdmmBot` processes; an online game connects to a running
`QMdmmServer`.

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

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
by `QMDMM_MACOS_APP_BUNDLE` (off by default, so a plain `cmake --install` gives
the plain shape below). The switch is macOS-only on purpose: the other platforms
have the plain shape by construction, and configuring them with it on is an
error rather than an option that quietly does nothing.

### Plain prefix layout (default)

The three programs are installed as siblings in `<prefix>/bin/`, with Qt
expected on the machine. No Qt content is copied, so a Qt from a package
manager - Homebrew included - is fine here. This is the shape a distribution
package or a bottle is built from.

An official Qt archive works here as well - the programs then reach its
libraries through the rpath the build records - but that install is not
distributable: it still needs that Qt to be on the machine. The bundle below is
the shape a release is made of, and it cannot be built against a package
manager's Qt at all, which is why it is not the default.

### Self-contained bundle

Configure with `-DQMDMM_MACOS_APP_BUNDLE=ON`. The GUI is then installed as
`QMdmm6.app` at the top of the prefix, and it carries what it needs: the Qt
libraries, the QML modules, the platform plugins, and the `QMdmmServer6` /
`QMdmmBot6` programs it starts as child processes.

That step deploys Qt into the bundle, so it wants an official Qt archive rather
than a package manager's build of Qt:

```sh
pip install aqtinstall
aqt install-qt mac desktop <qt-version> clang_64 -m qtwebsockets --outputdir ~/Qt
```

Then point the build at it:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQMDMM_MACOS_APP_BUNDLE=ON \
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

### Disk image

The bundle shape also produces a disk image (it is configured only when
`QMDMM_MACOS_APP_BUNDLE` is on):

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

The directories follow the tree they are installed into rather than the machine
they were built on. An install prefix of `/usr` or `/` keeps the system-wide
`/etc/QMdmm` and `/var/QMdmm`; any other prefix -- `/usr/local`, the default,
among them -- gets `<install prefix>/etc/QMdmm` and `<install prefix>/var/QMdmm`,
found relative to the executable, so the prefix can be moved after installation
and the paths follow it. The logs go to `<runtime data prefix>/log`, and fall back
to `$HOME/.QMdmm/var/log` when that directory cannot be created or written --
and a run that can write to neither stops rather than continuing without logs.
The macOS application bundle is not installed under a prefix at all: it reads
and writes `$HOME/Library/Application Support/me.fsu0413.QMdmm/{etc,var}`, which
is where this platform keeps an application's own data, named after the
identifier the bundle carries.

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

### The programs a local game runs

A local game runs the two command-line programs, and `--server <path>` and
`--bot <path>` say which to run. They are the only options the client takes
besides `--help`:

```sh
./build/build/bin/QMdmm6 --server /path/to/QMdmmServer6 --bot /path/to/QMdmmBot6
```

A path given this way is used as it is. Left out, each program is looked for by
name - `QMdmmServer6` and `QMdmmBot6` - first next to the client program, then
three levels above it, which is where a build tree keeps them when the client
runs from inside the bundle. Each name is tried with the suffix the platform
runs and taken only if it is executable. Both installed layouts keep the three
programs together: the bundle carries them side by side in `Contents/MacOS/`,
and the plain shape puts all three in `<prefix>/bin/`.

Neither a path that leads nowhere nor a name that was not found is an error at
startup: nothing needs either program until a local game is asked for, and
asking for one is what reports it - "The local server program was not found, so
a local game cannot be started" when the game is started, "The bot program was
not found, so the seat cannot be filled" at the seat left empty. Both lines name
the program that was wanted, and the two options above are how to point at a
different one.

A program that was found and then goes away is reported the same way: "The
local server stopped" and "A bot stopped". A game the player ends, or one that
gives way to the next, takes its own programs down without a word.

The server a local game runs is given the room size and otherwise its own
defaults, so it listens on the default local socket name `QMdmm` (`-L,
--local-name`); that is the name the client and the bots reach it by.

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

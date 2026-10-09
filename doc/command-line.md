# Command-line programs

Each QMdmm program is a thin `main()` around the libraries, and the command
line is where most of its behaviour is decided. Each program has a section
below; [Getting Started](getting-started.md) shows them in use.

## QMdmmBot

`QMdmmBot` is a client with an auto-player on top: it connects to a server,
takes a seat, and answers every request the server sends it. Nothing is read
from the terminal, so a room filled entirely with bots plays itself out.

- `-h`, `--help` -- print the options and exit with status 0.
- `-v`, `--version` -- print the program version and exit with status 0. The
  option is Qt's own; the version is the project version the build recorded.
- `-l`, `--host <host url>` -- the server to connect to. It is required, and a
  run without it is turned down before anything is connected. The scheme of the
  address picks the transport -- `qmdmm://` for TCP, `ws://` and `wss://` for
  WebSocket, and a bare name with no scheme for a local socket; the *Addresses*
  section of [Getting Started](getting-started.md) lists the forms in full.
- `-n`, `--name <screen name>` -- the screen name the other players see.
  Defaults to empty.
- `-s`, `--playing-style <style>` -- the bot strategy that plays the seat.
  Defaults to `knifePreferred`, and the values it accepts are `knifePreferred`,
  `horsePreferred` and `rl`. The first two play real strategies; `rl` is a
  placeholder that is still accepted by the option check and then refuses to
  start.

An unusable command line -- an argument that is not an option, a missing host,
a style that does not exist -- is reported on stderr, and the program exits
with status 3 rather than starting with something else.

## QMdmmServer

`QMdmmServer` is the room host. It listens, fills a room, runs the rules for it
and hands the requests out; a client only ever sees a mirror of the state it
keeps, so everything about a run that is not a game rule is decided here.

Every value below has a default, and can also come from a stored configuration
file; an option given on the command line wins over the file. The stored key of
an option is its long name. [Getting Started](getting-started.md) covers where
the files live and how a value is looked up.

### Help and version

- `-h`, `--help` -- print this table and exit with status 0.
- `-v`, `--version` -- print the project version and exit with status 0. The
  option is Qt's own; the version is the project version the build recorded.

### Network transports

All three listeners run side by side, and each can be turned off on its own;
turning all three off leaves a server that listens on nothing. An `on/off` value
also takes `true`/`false`, `yes`/`no`, `1`/`0` and `enable`/`disable`, in any
case.

- `-t`, `--tcp <on/off>` -- listen on TCP. Defaults to on.
- `-p`, `--tcp-port <port>` -- the TCP port to listen on. Defaults to 6366,
  which is the port a `qmdmm://` address reaches when it names none.
- `-l`, `--local <on/off>` -- listen on a local socket. Defaults to on.
- `-L`, `--local-name <name>` -- the name of that local socket. Defaults to
  `QMdmm`, and it is the name the client and the bots reach the server by.
- `-w`, `--websocket <on/off>` -- listen on WebSocket. Defaults to on.
- `-W`, `--websocket-name <name>` -- the name the WebSocket listener reports.
  Defaults to `QMdmm`.
- `-P`, `--websocket-port <port>` -- the WebSocket port to listen on. Defaults
  to 6367.

A port is an unsigned 16-bit number, so 65535 is its ceiling. A port already in
use is reported as the server starts, and a transport that cannot listen stops
the run rather than letting it go on without one.

### Room and timeout

- `-n`, `--players <2~>` -- the number of seats in a room. Defaults to 3. Two is
  the floor: the rock-paper-scissors that opens a round needs an opponent.
  Anything above nine is accepted with a warning -- nine is only a soft cap,
  since every further player makes a tie more likely.
- `-2`, `-3`, `-4`, `-5`, `-6`, `-7`, `-8`, `-9` -- shorthand for
  `--players=<N>`, so `-4` is `--players=4`. At most one of the eight may be
  given, and none of them together with `--players`.
- `-o`, `--timeout <0,15~>` -- the seconds a player has to answer a request
  before the request is given up on; 0 turns the timeout off. Defaults to 20.
  A non-zero value is only the share the player is given: on top of it the
  server grants a 60 second grace period for the client to draw the request
  and for the network, so an answer later than `--timeout + 60` seconds is
  treated as silence and the connection is dropped. At 0 there is no deadline
  at all -- a silent player is waited on as long as the connection holds.

### Game rules

Three stats can be upgraded -- knife damage, horse damage and max HP. Each one
starts at its initial value, is raised toward its maximum by spending upgrade
points during a round, and the first player to max out all three wins.

| Option | Sets | Default | With `-1` |
|---|---|---|---|
| `-s`, `--slash`, `--knife <1~>` | initial knife damage | 1 | 1 |
| `-S`, `--maximum-slash`, `--maximum-knife <3~>` | the ceiling knife damage is raised to | 10 | 3 |
| `-k`, `--kick`, `--horse <2~>` | initial horse damage | 2 | 3 |
| `-K`, `--maximum-kick`, `--maximum-horse <5~>` | the ceiling horse damage is raised to | 10 | 5 |
| `-m`, `--maxhp <7~>` | initial max HP | 10 | 7 |
| `-M`, `--maximum-maxhp <7~>` | the ceiling max HP is raised to | 20 | 7 |
| `-r`, `--punish-hp-modifier <0,2~>` | the divisor of the slasher's own max HP that a slash in a city costs the slasher; 0 turns the punishment off | 2 | 0 |
| `-R`, `--punish-hp-round-strategy <strategy>` | how that share is rounded | `RoundToNearest45` | `RoundToNearest45` |
| `-z`, `--zero-hp-as-dead <true/false>` | whether a player at 0 HP counts as dead, rather than only below it | `true` | `false` |
| `-f`, `--enable-let-move <true/false>` | whether a player may send another player moving, and not only themselves | `true` | `false` |
| `-i`, `--can-buy-only-in-initial-city <true/false>` | whether a knife or a horse may be bought only in the place a player started in, rather than anywhere outside the village | `false` | `false` |

`-R` takes one of four names, each of which says what becomes of the fraction
before it is applied:

| Value | Rounding |
|---|---|
| `RoundDown` | round down: 1.5 becomes 1 |
| `PlusOne` | round down, then add 1: 1.5 becomes 2 |
| `RoundUp` | round up: 1.1 becomes 2 |
| `RoundToNearest45` | round to the nearest: 1.4 becomes 1, 1.5 becomes 2 |

The strategy is matched without regard to case. Every ceiling has to be at least
its own initial value: an attribute grows inside that pair, so inverting the
pair would leave the attribute with no room to grow at all. The floors written
with the options above are enforced after the command line and the file have
been merged, so a value read from a file is judged exactly like one from a flag.

### Presets

- `-1`, `--use-v1-presets` -- use the version 1 rules in place of the built-in
  ones, which are the version 2 rules. While it is selected a rule value stored
  in a configuration file is not read at all: the rules come from the preset,
  and from the command line wherever an option is given. The `With -1` column of
  the table above is what the preset selects.

### Configuration

- `-c`, `--save-configuration` -- write the whole resolved configuration, the
  defaults included, to the per-user store and exit.
- `-C`, `--save-global-configuration` -- the same for the system-global store.
  A run saves to one store only, so both together is refused.
- `-d`, `--show-current-configuration` -- print the configuration the run
  resolved as JSON, and exit with status 0. Given together with `-c` or `-C`,
  the JSON is printed and the save then decides the exit status.

An unusable command line does not start the run. An option the program does not
know, or one given without its value, is refused by Qt's own option parser with
status 1; anything else -- an extra argument, a value that cannot be parsed, a
value outside its range, a pair that contradicts itself -- is reported on stderr
and the program exits with status 3.

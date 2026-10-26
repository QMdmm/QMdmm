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
  start, with status 3, rather than joining a game with no strategy.
- `--log-level <level>` -- how much of the log is copied out to the terminal.
  Defaults to `off`, and the values are `off`, `debug`, `info`, `warning` and
  `critical`: each but the first names the lowest level that is copied, and
  `off` copies nothing. `warning` copies warnings and criticals and leaves the
  two below them to the file. The log file keeps every line whatever the level
  says, so this decides what a terminal shows rather than what is recorded. A
  run that never opens a log file has nothing to filter, so its terminal keeps
  every line.

An unusable command line -- an argument that is not an option, a missing host,
a style that does not exist, a style that exists but is refused before it can
play -- is reported on stderr, and the program exits with status 3 rather than
starting with something else.

Asked to stop -- by a `SIGTERM` or a `SIGINT`, the two ways a supervisor or a
terminal ask -- a run reports the stop on stderr and exits with status 0,
rather than being killed by the signal and leaving a caller nothing but the
signal (`143`) to read.

## QMdmmCtl

`QMdmmCtl` is the operator tool. It asks a running server a question and prints
the answer, and it does nothing else: it starts nothing, keeps no state, writes
no log and takes no part in a game.

### status

- `status` -- print the server's status snapshot, byte for byte as the control
  plane serves it (the plane itself is under `QMdmmServer`, below).
- `-p`, `--control-port <port>` -- the port that server was given with
  `--control-port`. Required: a server without one has no control plane to ask,
  so there is no default to fall back to.
- `-h`, `--help` -- print the usage table and exit with status 0.
- `-v`, `--version` -- print the project version and exit with status 0.

The snapshot is written out exactly as it arrived, down to the absence of a
trailing newline, so it can be compared with the same answer read any other way:

```sh
diff <(curl -s 127.0.0.1:7900/status) <(QMdmmCtl6 status --control-port 7900)
```

It has no `--log-level` and installs no message handler, and that is a decision
rather than an omission. The two long-running programs have a level because they
open a log file: the level says how much of that record the terminal is told.
This one opens no log file at all, so it has no record to filter and nothing a
level could choose between. What it prints is the whole of what it produces,
and a line of its own on that stream would be one more thing for whoever reads
the comparison above to explain away.

An answer that carries no snapshot is not printed: a `404` from the control
plane, an answer that ends before its headers do, an answer that does not finish
within five seconds, an answer that grows past 1 MiB without ending, a `200`
whose body is not the snapshot, and a port with nothing listening all end the
run the same way -- the reason on stderr, nothing on stdout, and status 1. A
peer that sends rather than answers reaches that bound well inside the five
seconds, so the size is bounded as well as the time.

A command line that is refused -- no command, more than one, an unknown one,
`status` without `--control-port`, or a value that is not a port -- is reported
on stderr with status 3, the status the other programs use for a configuration
they will not run with. An option the program does not know is refused by Qt's
own option parser with status 1, as it is there.

Starting, stopping and enabling a server through the platform's own service
manager is not implemented yet; asking is all this program does.

## QMdmmServer

`QMdmmServer` is the room host. It listens, fills a room, runs the rules for it
and hands the requests out; a client only ever sees a mirror of the state it
keeps, so everything about a run that is not a game rule is decided here.

Every value below has a default, and can also come from a stored configuration
file; an option given on the command line wins over the file. The stored key of
an option is its long name. The two sets are the same set: a setting that no
option names has no stored key either, and the values the file can hold are
exactly the values listed here -- with the level under *Logging* the one
exception, which is an option with no stored key. A setting internal enough that
no one running a server would tune it is documented with the class that declares
it instead of being listed here. [Getting Started](getting-started.md) covers
where the files live and how a value is looked up.

### Help and version

- `-h`, `--help` -- print this table and exit with status 0.
- `-v`, `--version` -- print the project version and exit with status 0. The
  option is Qt's own; the version is the project version the build recorded.

### Network transports

All three listeners run side by side, and each can be turned off on its own;
turning all three off leaves a server that listens on nothing. An `on/off` value
also takes `true`/`false`, `yes`/`no`, `y`/`n`, `1`/`0`, `enable`/`enabled` and
`disable`/`disabled`, in any case.

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
the run rather than letting it go on without one: the reason is reported on
stderr and the run exits with status 3.

### Control plane

A second, deliberately narrow listener can be opened for whoever runs the server
rather than for players. It speaks HTTP/1.0, it is read-only, and it is off
unless a port is named -- a value of `0`, which is the default, is not a port but
the absence of one.

- `--control-port <port>` -- serve a status snapshot on the loopback address at
  `http://127.0.0.1:<port>/status`. Defaults to off.

That address only: the plane binds `127.0.0.1`, so it is reachable from the
machine the server runs on and from nowhere else. The answer to `GET /status` is
the same snapshot an in-process caller reads from the server's `status()` -- a
JSON object with the keys `rooms`, `games` and `players`, each a count, so a
server with nothing going on answers with three zeroes. Beside those three,
`roomList` holds the rooms one at a time: an array of objects with
`id`, `players` and `rounds` -- the room's identifier (an unsigned 32-bit
counter handed out as rooms are built, so it stays put for the room's life), how
many of its players are online, and how many rounds it has started, `0` while it
is still recruiting. The entries are ordered by id and their player counts add up
to `players`, so reading either the total or the breakdown is enough. A path other
than `/status`, and any method other than `GET`, is answered with `404`. Nothing
the plane is asked can change anything.

One exchange per connection, and a connection that has not asked anything within
a few seconds is closed rather than held: a client that connects and then says
nothing cannot pin a socket on the server, which is the same bound the game
transports put on a peer that never finishes a packet.

### Room, connections and timeouts

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
- `--maximum-packet-size <1024~>` -- the largest packet a peer may send, in
  bytes. Defaults to 1048576 (1 MiB). A packet is one line, so a peer that sends
  that many bytes without ending one is not speaking the protocol and the
  connection is dropped rather than buffered. The floor of 1024 is what keeps
  the protocol's own packets inside the bound, and there is no unlimited
  setting: bytes past the bound are the whole reason it exists.
- `--sign-in-timeout <2~60>` -- the seconds an accepted connection has to sign
  in before it is dropped. Defaults to 5. Only a connection that has not signed
  in yet is on this clock; one that did is the request timeout's business. The
  ceiling of 60 is what keeps a peer from holding a port, and the memory behind
  it, for longer than a handshake could plausibly take; the floor only has to
  outlast the slowest honest one.
- `--maximum-rooms <0~>` -- how many rooms the server runs at once. Defaults to
  0, which is no limit. A room holds `--players` players and, once full, plays
  its game to the end in the background while the next room starts recruiting;
  this is what bounds how many are alive at the same time. A sign-in that would
  need one room more is refused and the connection dropped, the way a peer that
  never signs in is given up on. There is no connection limit to set alongside
  it: a signed-in peer takes one seat, so the connections the server can hold
  are this many rooms times the players in each. A seated player keeps their
  room either way, and a reconnect goes back to the room the player came from
  rather than asking for a new one, so the bound never locks a seated player
  out.

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

### Logging

- `--log-level <level>` -- how much of the log is copied out to the terminal.
  Defaults to `off`, and the values are `off`, `debug`, `info`, `warning` and
  `critical`: each but the first names the lowest level that is copied, and
  `off` copies nothing. `warning` copies warnings and criticals and leaves the
  two below them to the file.

The level is the one option here that a stored configuration file cannot also
carry, and `-c` does not write it out. The file the run opens is the record and
keeps every line either way; the level says how much of it the terminal is told,
which is how a run reports itself rather than what the server does. A run that
never opens a log file has nothing to filter, so its terminal keeps every line.

### Stopping

Asked to stop -- by a `SIGTERM` or a `SIGINT`, the two ways a supervisor or a
terminal ask -- the server closes its listeners, reports the stop on stderr and
exits with status 0. That is a clean end rather than a refusal, so it does not
carry the status of one: a peer sees an ordinary close rather than a reset.

An unusable command line does not start the run. An option the program does not
know, or one given without its value, is refused by Qt's own option parser with
status 1; anything else -- an extra argument, a value that cannot be parsed, a
value outside its range, a pair that contradicts itself -- is reported on stderr
and the program exits with status 3.

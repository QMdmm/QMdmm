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

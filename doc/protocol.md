# The QMdmm wire protocol

This page is the contract between a server and its clients: the frame, the kinds of
packet, and every id a packet can carry together with the payload that goes with it. It
says nothing about the transports a frame may travel over -- TCP, the local socket and
WebSocket are all the same protocol to a reader of this page -- and nothing about how
the ids are numbered inside their enums; those belong to the classes that carry this
layer, not to the contract.

## Frames

A frame is a JSON object encoded in a single line. One line is one packet: there is no
length prefix and no delimiter other than the newline, which is also what keeps a frame
readable in a log.

The object carries four keys:

| Key | Holds |
|---|---|
| `type` | int, one of the packet types below |
| `requestId` | int, one of the request ids. Meaningful in a request and in a reply; an invalid id in anything else |
| `notifyId` | int, one of the notify ids. Meaningful in a notify; an invalid id in anything else |
| `value` | the payload. Its shape follows the `requestId` (request / reply) or the `notifyId` (notify) |

Unknown keys are preserved when a frame is decoded, and otherwise ignored.

That a line ends matters: a peer that never sends a newline is bounded rather than
waited on. An implementation caps what it will hold for one frame -- on the server the
cap is `ServerConfiguration::maximumPacketSize`, 1 MiB unless configured otherwise --
and drops a peer that goes past the cap, the same way it drops one whose frame does not
decode.

## Packet types

<!-- protocol-ids: begin -->

| Id | Meaning |
|---|---|
| `TypeRequest` | A request. Always sent by the server, on behalf of the Logic running the room, to the one client that has to answer it |
| `TypeReply` | An answer to the request it names. Always sent by the client that was asked |
| `TypeNotify` | A notification. Either direction; the notify id says which |

<!-- protocol-ids: end -->

A frame that is neither of the three is not a packet: an invalid type cannot be
answered or acted on, and a peer that sends one is dropped.

## Requests and replies

Every request originates in the Logic driving a room -- none of them is the server's own
-- and the server forwards it to the single client that has to answer. A request
therefore travels server to client and its reply travels client to server, both as the
same `requestId`, one `TypeRequest` frame and one `TypeReply` frame.

A reply whose `value` is null is a **refusal**: the client declines to answer, and the
server applies the default reply for that request. Every legal reply value is non-null,
so a null is never ambiguous.

A reply that is well formed but not feasible is not an error: it stands in for what the
refusing client above would have got, so a buggy or malicious agent cannot stall a
phase. Well formed means the payload has the type this page gives it and -- where the
request asks for a number of selections -- no more entries than it asked for; a payload
that breaks either rule is a protocol error instead, which takes the connection down
with it.

A client that neither replies nor refuses within the server's request timer
(`ServerConfiguration::requestTimeout`, 20 seconds by default, extended by a grace
period before the connection is treated as gone) is treated as gone and disconnected. A
timeout of 0 turns that timer off: the server then waits for that client indefinitely.

## Request ids

Request payloads are described from the server's side of the wire (what the client
receives); replies from the client's.

<!-- protocol-ids: begin -->

| Id | Request (server to client) | Reply (client to server) |
|---|---|---|
| `RequestRockPaperScissors` | `{"playerNames": [string], "strivedOrder": int}`. `strivedOrder` 0 asks for an action instead of a strived order; a non-zero one asks for a strived order while that order is still free | `int` -- the choice |
| `RequestActionOrder` | `{"remainedOrders": [int], "maximumOrder": int, "selectionNum": int}` | `[int]`, one entry per selection, so its length must equal `selectionNum`. An entry is either 0 -- yield this selection, taking whatever order is left -- or an order in `1..maximumOrder` to strive for. A longer reply is a protocol error; a shorter one, an order out of range, or a repeated order that is already confirmed falls back to yielding every selection |
| `RequestAction` | `int` -- the current order | `{"action": int, "toPlayer": string, "toPlace": int}`. An action that is not feasible falls back to doing nothing |
| `RequestUpgrade` | `int` -- the upgrade points this round's upgrade may spend | `[int]`, one entry per item bought; its length must equal the request's number of points. A longer reply is a protocol error; a shorter one, or an item for a stat that has no upgrade points left, falls back to spending every point in the order knife, horse, max Hit Points. An empty reply is a deliberate value and falls back the same way |

<!-- protocol-ids: end -->

## Notifies

A notify either originates at the server itself, or at one of the agents the server
runs for the players in a room, or at a client.

- **From the server** -- the version the server announces when a connection arrives, and
  the echo of a ping.
- **From a room's agents, broadcast to every client in that room** -- the room's
  configuration, who is in it, and everything that happens during a game. A client that
  has signed in but is not in a game yet still receives the configuration and the
  arrivals and departures.
- **To the server** -- sent by a client: signing in, declaring the state it holds for
  itself, observing, and the ping.
- **To an agent** -- sent by a client to its own agent on the server side, which is how a
  client speaks and, later, operates.

## Notify ids

<!-- protocol-ids: begin -->

| Id | Direction | Payload |
|---|---|---|
| `NotifyPongServer` | server to client | `int64` -- the epoch-milliseconds timestamp it was pinged with, echoed back |
| `NotifyVersion` | server to client | `{"versionNumber": string, "protocolVersion": int}` |
| `NotifyLogicConfiguration` | broadcast | object, the room's logic configuration (see `QMdmmCore::LogicConfiguration`). Keys that are absent fall back to the receiver's own defaults, so a partial object and an empty one -- the default configuration -- are both accepted; a key that is present but not valid makes the receiver keep the configuration it had |
| `NotifyAgentStateChanged` | broadcast | `{"playerName": string, "agentState": int}` |
| `NotifyPlayerAdded` | broadcast | `{"playerName": string, "screenName": string, "agentState": int}` |
| `NotifyPlayerRemoved` | broadcast | `{"playerName": string}` |
| `NotifyGameStart` | broadcast | empty object |
| `NotifyRoundStart` | broadcast | empty object |
| `NotifyRockPaperScissors` | broadcast | `{playerName: int}` -- the choices, keyed by player name |
| `NotifyActionOrder` | broadcast | `[string]`, a dense array where index `i` is the player taking order `i + 1`; the orders are always contiguous |
| `NotifyAction` | broadcast | `{"playerName": string, "action": int, "toPlayer": string, "toPlace": int}` |
| `NotifyRoundOver` | broadcast | empty object |
| `NotifyUpgrade` | broadcast | `{playerName: [int]}` -- the items each player bought |
| `NotifyGameOver` | broadcast | `[string]` -- the winning player names |
| `NotifySpoken` | broadcast | `{"playerName": string, "content": string}` -- what an agent said |
| `NotifyOperated` | broadcast | not defined yet |
| `NotifyPingServer` | client to server | `int64` -- an epoch-milliseconds timestamp |
| `NotifySignIn` | client to server | `{"playerName": string, "screenName": string, "agentState": int, "lastRoundEventSeq": int}` |
| `NotifyObserve` | client to server | `{"observerName": string, "playerName": string}` -- not defined beyond this yet |
| `NotifyStateChange` | client to server | `{"agentState": int}`, the state the client declares for its own player. The player is identified by the connection, so no name travels. A payload that is not an object, or whose `agentState` is not a number, is a protocol error, as is this notify from a connection that has not signed in |
| `NotifySpeak` | client to agent | `string`, base64-encoded UTF-8 content -- keeps a frame on one line whatever was said |
| `NotifyOperate` | client to agent | not defined yet |

<!-- protocol-ids: end -->

## The handshake

1. A client opens a connection on whichever transport it was configured to use.
2. The server sends `NotifyVersion` at once, naming both its version and the version of
   the wire protocol it speaks.
3. The client sends `NotifySignIn`, with the player name it wants, the screen name to
   display, the state it declares for itself, and the last round event it has applied.
4. The connection is a player's from then on: the server sends the room's notifies, and
   the requests the Logic raises go to the clients that have to answer them.

`agentState` in a sign-in is a self-declaration, not a privilege: the server stores it
as sent, and neither trusts nor enforces the flags that say the player is managed or is
a bot. What the server does own is the flag saying the player is online, which it clears
when the connection goes away and restores when it comes back, and the managed flag,
which a disconnect clears rather than keeps.

A connection that never gets as far as step 3 is not a player's yet, so nothing else
would notice it. The server gives every accepted connection
`ServerConfiguration::signInTimeout` to sign in -- 5 seconds by default, configurable
between 2 and 60 -- and drops one that is still silent when it runs out. The deadline is
always armed; a connection that did sign in is left alone, since the silence of a
signed-in agent is the request timer's business instead.

## Reconnecting

A client that loses its connection may open a new one and sign in again with the same
player name; that is how it rejoins the room it was in rather than becoming a second
player. `lastRoundEventSeq` is what makes the room continue rather than restart: it is
the number of round events the client has applied, and the server replays the ones the
client missed from there. On such a sign-in the declared state is not re-read -- only the
online flag comes back -- so a player that was managed before the drop is not managed
after it and has to say so again.

## Version negotiation

`protocolVersion` in `NotifyVersion` is the version of the wire protocol, and a client
whose own version differs drops the connection and does not retry: the auto-reconnect
loop would run into the same mismatch. `versionNumber` is the version of the program, and
a mismatch there is tolerated -- it says nothing about the wire, which is still
compatible -- where the only trace is the two version numbers on the log, after which the
handshake goes on.

Version 0, the only one implemented so far, is still being developed against rather than
released, so its wire is not frozen: the payload of a given id may change in a later
build without `protocolVersion` moving, and two peers that both announce 0 are not
thereby compatible. A version 0 peer has to match the build it talks to, not the number.
A stable wire begins at version 1, which ships together with program version `1.0.0`;
from there on the payloads of a version do not change, and a change to any of them is a
new version number.

## Protocol errors

A frame that does not decode, is not an object, carries a key of the wrong type or a
fractional or out-of-range id, or goes past the size an implementation holds for one
frame, is refused: the connection it arrived on is closed. A connection is also closed
when a payload breaks the rules this page gives it -- the `NotifyStateChange` cases
above, for instance.

Where this page says a payload is ignored instead, the connection stays up: a key of the
room's configuration that does not parse, and a reply that is feasible-shaped but not
feasible in the game, both leave the player where it is.

For the ids whose payloads are not defined yet the rules end there, and a peer should not
rely on what a server or a client does with them.

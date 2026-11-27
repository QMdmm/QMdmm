# Rules

How a game of QMdmm is played, from a player's seat. [Getting
Started](getting-started.md) covers running the programs and getting into a
game; this page is about what the players then do. Each rule names the
`QMdmmCore` setting or check behind it, so a game that behaves otherwise can be
traced back to the code.

## The places

A room has a Village and a city per seat. The seats are numbered as the room
hands them out at the start of a round, one per player, and a player begins in
the city whose number is their seat (`Player::initialPlace()`, handed out by
`Room::prepareForRoundStart()`).

The Village is adjacent to every city, and no city is adjacent to another city
(`Data::isPlaceAdjacent()`). A move therefore either goes from the Village into
a city, or out of a city into the Village.

A room seats two players or more, and a game begins when the room is full
(`-n` on the server). Its help text carries the default and the reason the
practical ceiling is a small room: the more players throw at once, the likelier
a rock-paper-scissors tie.

## What a player has

- **HP and maximum HP.** HP starts every round at the player's maximum, and
  damage comes off it (`Player::maxHp()`, `Player::hp()`).
- **Knife.** Bought during the game, and the damage `Slash` deals
  (`Player::hasKnife()`, `Player::knifeDamage()`).
- **Horse.** Bought the same way, and the damage `Kick` deals
  (`Player::horseDamage()`).
- **Place.** The Village or a city (`Player::place()`).
- **Upgrade points.** Earned by slaying somebody, and spent once the round is
  over.

## The actions

| Action | Needs | Effect |
|---|---|---|
| `DoNothing` | -- | Pass the turn. |
| `BuyKnife` | Alive, no knife yet, not in the Village | The player now carries a knife. |
| `BuyHorse` | Alive, no horse yet, not in the Village | The player now rides a horse. |
| `Slash` | A knife, both players alive, target in the same place, target not the slasher | Knife damage to the target. In a city, the slasher loses HP as well. |
| `Kick` | A horse, both players alive, target in the same place, not in the Village | Horse damage to the target, who is then sent to the Village if they survived. |
| `Move` | Alive | One step to an adjacent place. |
| `LetMove` | Alive | Pull a player from an adjacent place into this one, or push a player sharing this place to an adjacent one. |

`LetMove` is refused on another player unless `enableLetMove` is on
(`Player::canLetMove()`); on the player themselves it is a `Move`
(`Player::canMove()`). Buying is refused in the Village, and with
`canBuyOnlyInInitialCity` on it is refused everywhere but the player's own
starting city (`Player::canBuyKnife()`, `Player::canBuyHorse()`).

## Attacks and the price of a slash

`Slash` and `Kick` both reach only a player standing in the same place, and
both need the weapon; neither can be aimed at the player themselves.
`Kick` is refused inside the Village (`Player::canKick()`), and a kick that
does not kill sends its target to the Village.

Slashing in a city costs the slasher HP. The amount is worked out from the
slasher's own maximum HP, divided by `punishHpModifier` and rounded by
`punishHpRoundStrategy` (`Player::slashPunishHp()`):

| Strategy | A half of 10 | A half of 7 |
|---|---|---|
| `RoundDown` | 5 | 3 |
| `RoundToNearest45` | 5 | 4 |
| `RoundUp` | 5 | 4 |
| `PlusOne` | 6 | 4 |

In the Village the punish is nothing, and so it is when the modifier is zero or
less. The punish lands on the slasher as damage from the player they slashed,
so a slash whose punish kills the slasher is credited to that player.

## Death

A player whose HP is used up is out for the rest of the round. With
`zeroHpAsDead` on, HP reaching zero is death; with it off, a player at zero HP
is still standing and only a negative HP kills (`Player::deadAtHp()`). Death
lasts for the round: the next round starts with everyone in their seat at full
HP.

## The round

A round opens with every player back in their seat, HP at the maximum, no knife
and no horse, and no upgrade points (`Player::prepareForRoundStart()`).

Play then goes in **action times**, and each one opens with rock-paper-scissors
among the players still alive:

- The shape that beats the other takes the actions. Every player who threw it
  acts once for each player who threw a losing shape
  (`Data::rockPaperScissorsWinners()`).
- Which position in the sequence each of those actions falls in is up to the
  winners: they name the orders they want (`Logic::actionOrderReply()`), and
  winners naming the same order throw again to settle it.
- A throw that leaves no shape beating the others -- everyone throwing the same,
  or a split across the shapes -- is a tie. Nobody won, and the throw is
  repeated. A tie can repeat for as long as the players keep throwing the same,
  which is rock-paper-scissors rather than a missing fallback.

When every order of an action time has been played, a fresh throw opens the
next action time. This repeats until at most one player is left standing
(`Room::isRoundOver()`); the round is then over and the upgrade phase follows.
A player killed before their order comes round loses that action.

## Upgrades

A kill earns the killer one upgrade point (`Player::upgradePoint()`), capped at
what that player can still buy -- the sum of `upgradeKnifeRemainingTimes()`,
`upgradeHorseRemainingTimes()` and `upgradeMaxHpRemainingTimes()` -- so points
never outnumber the upgrades left to spend them on.

Once the round is over, every player holding points is asked to spend them,
including players who did not survive the round. A point buys one step of knife
damage, horse damage or maximum HP (`Player::upgradeKnife()`,
`Player::upgradeHorse()`, `Player::upgradeMaxHp()`), each up to its maximum. All
of a player's points must be spent in that one answer: a list that leaves a
point over is turned down, and the game spends the points for the player
instead.

## Winning

A game ends when a player has nothing left to upgrade: knife damage, horse
damage and maximum HP all at their maximum (`Room::isGameOver()`). Everyone who
got there in the same round wins together. The check is made after the upgrade
phase, so the points earned during the round can be what wins the game, not
only points banked in earlier rounds.

## Which rules a game uses

The engine ships with a set of values for each version of the game:
`LogicConfiguration::defaults()`, which the server plays unless told otherwise,
and `LogicConfiguration::v1()`, which the server picks with
`-1, --use-v1-presets`. Version 1 is the older set: no punish HP, no `LetMove`,
no upgrading of maximum HP, and zero HP is not death. Version 2 adds all of
those, and is what a server started without `-1` plays.

| Rule | `defaults()` | `v1()` |
|---|---|---|
| Knife damage, starting and maximum | 1 and 10 | 1 and 3 |
| Horse damage, starting and maximum | 2 and 10 | 3 and 5 |
| Maximum HP, starting and maximum | 10 and 20 | 7 and 7 |
| `punishHpModifier` | 2 | 0, no punish |
| `punishHpRoundStrategy` | `RoundToNearest45` | `RoundToNearest45`, unused |
| `zeroHpAsDead` | on | off |
| `enableLetMove` | on | off |
| `canBuyOnlyInInitialCity` | off | off |

Either set can be adjusted value by value on the server's command line;
[Command-line programs](command-line.md) lists the options and what each one
sets.

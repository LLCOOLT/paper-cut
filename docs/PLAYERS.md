# Three or four players

Mario's player and up to three more, on one computer or online:

| Player | Plays | In the field | In battle |
|---|---|---|---|
| 1 | Mario | as always | Mario's turns and guards |
| 2 | the partner | its stick walks the partner (Multiplayer → **Second player (co-op)**) | the partner's turns and guards |
| 3 | the follower | the story's follower when there is one (Flavio, Frankly...), else a **companion** | the partner's, when it's their companion's partner |
| 4 | a companion | a companion of their own | likewise |

A **companion** is another partner, in the form the game uses for a partner
tagging along (Ms. Mowz first, then Yoshi, Vivian, Flurrie, Bobbery, Koops,
Goombella: those Mario has, never one already on the field). It comes out
at Mario's side once a room has settled; **L** or **R** gives its player the
next one. Each walks, hops (A), talks to an NPC or hits a block or switch in
reach (B), comes back to Mario's side when it gets far or falls, and follows
Mario while its player leaves the stick alone, as player 2's partner does.
Mario's player calls them all back with Y. "3P" (green) and "4P" (yellow)
show over them, beside "1P" and "2P".

In battle the partner fighting belongs to whoever has it in the field: switch
to Yoshi (the battle menu's partner change) and Yoshi's player takes his
turns and guards. A player with no character in the fight waits.

## With Brothers mode

With Multiplayer → **Brothers mode** on ([BROTHERS.md](BROTHERS.md)), player 2 plays Luigi instead of
the partner, and the others keep their parts:

| Player | In the field | In battle |
|---|---|---|
| 1 | Mario | Mario's turns and guards, and the partner's |
| 2 | Luigi | Luigi's turns and guards |
| 3 | the follower, else a companion | the partner's, when it's their companion's partner |
| 4 | a companion | likewise |

The partner just follows Mario then ("2P" is over Luigi). Luigi and player
4's companion each have a place of their own beside the partner and the
follower, so all five can be out at once; neither is ever saved.

## On one computer

With Multiplayer → **Second player (co-op)** on, a controller in the third or fourth port
joins as player 3 or 4 ("Player 3 joined..."), and leaving takes their
companion away.

## Online

Players 3 and 4 join a room (or the host's address) with **Join as player 3
or 4** (the launcher's Online tab, or the Start menu's), before the host
starts; the room list joins a room whose partner is taken that way. They get
the host's setup as a watcher does and play from there
([ONLINE_RELAY.md](ONLINE_RELAY.md#three-or-four-players)).

- The host's input carries players 3 and 4's: each frame the host sends, it
  puts in the next of their controllers it has. So the players never wait
  for them, and their own input comes a little later than the others'
  (their round trip to the host, plus the relay's batches through a room).
- One who leaves is no controller from the next frame the host sends, in
  every game alike; their companion goes. Nobody else's game ends for it.
- Through a relay their messages are sealed with the watchers' key (the
  whole code, PBKDF2), not J-PAKE: weaker, as the watchers' (they carry only
  the controller).

## Co-op pings

Multiplayer → **Co-op pings** (on by default) lets players 1 and 2 mark a
spot or an enemy with a paper arrow both see, on one computer or online. Hold
**Ping** (Bindings) and aim with the C-stick, then let go; a quick press
marks the nearest enemy, else where the character stands. A click on the picture (a middle
click works over the menus too) or a tap marks that spot. An arrow near an
enemy goes on it and follows it, and stays five seconds. The ping travels in
the controller's input, so both games of an online session, replays and
watchers show the same arrows; the game itself sees nothing of it
(`port/src/common/ping_pc.c`, `port/settings/features/pings.cpp`).

## Saves and replays

A companion is never saved as the story's follower: a save made with one
loads without it. Replays keep all four controllers (`PCRPLAY2`) when
controllers 3 or 4 were there; older replays, and those without them, are
`PCRPLAY1` as before ([REPLAYS.md](REPLAYS.md)).

## For other features

`port/include/port/players.h`: `portCoopPlayers()` (1 to 4) and
`portCoopPlayerHere(player)`, from the frame's controllers as played (the
same in every game of an online session); `portOnlinePlayers()`,
`portOnlineSelf()` and `portOnlinePlayerName()` for a session's own.

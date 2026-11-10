# Playing online

Online play is one part of the **Multiplayer** page (the title menu's
Multiplayer button, the Start menu's Multiplayer tab; an invite link starts
the game on it), the same page you use on one screen. The online part is only
how a friend gets in: once they're in, they take the next seat, as a
controller on your computer does, and two players on one screen can play with
a third online.

## How to play

1. **Online...**, **Host a room**, in a game or from the title. The page
   shows the room's code in large letters (`K7P2-QX9M`); read it out or
   **Copy room code** / **Copy invite link**.
2. Friends: **Online...**, **Join by code**, and type it on the page's own
   keyboard (the stick and A, touch, or the keyboard; **Paste** takes a code
   from the clipboard). Or the **Room list** (rooms whose host has **List my
   room** on), or an invite link (`papercut://join/K7P2-QX9M`), which always
   asks first.
3. What everyone plays, straight in:
   - **The adventure** (no button: the room's own once the host is in a
     file; from the title, the host closes the page and picks **Play**,
     and guests wait in the room until then): the host's file. Everyone is always in the same room; the host's
     world decides. Player 1 is Mario, player 2 the partner (Luigi with
     Brothers mode on), players 3 and 4 companions. A friend on the title
     screen or the file menu comes in on a file of their own that is never
     saved, and the host's world takes over.
   - **Party**: the host's pick from a picture menu: Versus, Raid boss, Boss rush, Quiz party,
     Tattle-card duel, Bingo (on the room, no code of its own) and Dungeon
     master (one of you commands the bosses). No save needed.
4. **Who's who** shows who plays what. Ask for a seat and its player says yes
   with one button; the host can move anyone. A swap happens at the next calm
   moment (in the field, no scene or battle). A guest can play Mario too.
5. **Leave the room** any time: each of you goes on alone with everything you
   have.

**Team bonus:** in a shared battle, when every player who acts in a turn lands
their action command (Nice or better), a "Team bonus!" pop-up and a little
extra Star Power. The host's setting (Multiplayer > Team bonus); the host's
world gives it.

**Watching:** **Online...**, **Watch...**, a room's code or the host's
address: your game follows the host's and shows their battles; you don't play.

**By address:** **Host by address** and **Join by address** work without the
relay: the host forwards TCP port 47700 (the **Port** row), or everyone joins
one Tailscale or ZeroTier network.

**Names:** **Your name** is what the others see. Another player's name is
cleaned before it's shown (at most 31 bytes, no control characters, none of the
game's or the menus' markup).

Everyone needs the same Paper Cut version and a disc of the same release.
Save states, fast-forward, frame advance, warps and the Practice tab's editors
are off while playing online.

## How it works, in short

Each player runs their own game and the games send each other only what
should be shared, as sm64coopdx does for Super Mario 64. Nothing has to stay
identical, so nothing drifts and nothing starts again when a player joins or
comes back after a drop. Up to four play, by address (the host and three
guests) or in a room on the relay ([ONLINE_RELAY.md](ONLINE_RELAY.md)). The
layers below are the engine's:

1. a shared world (the other players drawn as ghosts, the story and the pouch
   shared);
2. the room's enemies and objects shared;
3. battles together (each game runs the battle, the host's world decides, each
   player chooses on their own screen);
4. scenes together.

## Playing together

A room is the host's adventure: every guest that plays (not one that watches)
plays it from the moment they join (`statesync_coop_pc.inc`). Only a party game
the host picks switches it off while it runs: then each game is its own as
before, every player free in their own game, the progress merged both ways,
nobody following; the adventure together comes back when the party game is
over.

- **The host's world.** The guest's game goes where the host's goes (a door
  or a pipe takes everyone along), keeps its own Mario hidden where the
  host's is (its camera follows him), and draws the host's Mario, partner,
  Luigi, follower and companion as the host's game moves them. The room's
  enemies and townsfolk are the host's (layer 2: the host owns every room);
  the host's scenes and talks play on every screen with their text boxes and
  the host's camera (layer 4).
- **The host's progress.** As the guest joins, the host's whole progress (the
  story, the area's flags, the items, coins, Star Pieces and Shine Sprites,
  Mario's level and the partners) takes the place of what the guest's game
  had in memory, and the host's changes come as they're made. Nothing of the
  guest's goes the other way.
- **Seats.** Seat 1 is Mario's, the host's to begin with. Each guest gets the lowest free of
  seats 2 (the partner, or Luigi with Brothers mode on), 3 and 4 (the follower
  or a companion), in the order they came; a controller plugged into the
  host's computer keeps its own seat (two on one computer and a friend
  online). Any player can ask for another seat (its holder answers yes or
  no), and the host can move anyone; a change waits for a calm moment in the
  field. A guest can take Mario's seat: their controller is then the host's
  game's controller 1 (Mario in the field and in battle, where the character
  each player plays goes by seat), and the host's own controller plays the
  seat the guest gave up. The host's game is still the world and still the one
  that saves. Mario's player opens their own game's Start menu (an item used
  there comes out of the shared bag); while it's open Mario stands still.
- **The guest's own character.** A guest in seat 2, 3 or 4 walks the
  character its seat plays (the partner, Luigi, the follower or a companion)
  in its own game, from its own controller, as a second controller on one
  computer walks it ([PLAYERS.md](PLAYERS.md)): it moves on the guest's screen
  in the frame the stick is pushed, whatever the ping. Each frame the guest's
  game sends where it is, and the host's game puts that character there
  (a step past what a walk can do is cut to it; a jump far from where the
  host has it and from Mario is refused). Everything it does to the world is
  still the host's: the host's enemies notice and touch it where it is put,
  and what its buttons do is the host's game's to decide (below). The host holds
  the character (its game moves it, the guest's draws the host's) while its
  field isn't free: a talk, a scene, a menu, a door or pipe, a battle, the
  partner's field move or Luigi's swing, Mario's Y, or the guest's word late.
  Each hold starts a new epoch, and the guest's word counts only with the
  current one, so when a hold ends the character starts where the host's game
  has it.
- **The guest's field actions.** What a guest's buttons do with its own
  character starts on its own screen in the frame they're pressed, too:
  Luigi's hammer swing and his talk (A in front of someone), a partner's or
  companion's talk or block (B), the partner's field move starting (X), a
  swap (L, R) and a pick from the player menu's Character strip or the
  Partner wheel. The guest's game plays the start (the swing, the talk pose
  turned to the NPC, the move's first pose, the new partner) and sends it to
  the host as an action, with where its character was. The host's game does
  it for real from there (the swing's hits on blocks and enemies, the NPC's
  talk script, the block's event, the partner's move, the swap) and answers:
  done, what the swing hit, or refused (the host's field not free, the NPC
  or block not in reach where the host has the character, the partner not
  free), and on a refusal the guest's game takes its own start back (the
  swing stopped, the pose, the partner it had). A talk, a move or a swap
  then holds the character in the host's game as before, so the scene and
  the move show on every screen from the host's game a round trip after the
  guest's own start. X held is the host's X as long as the guest's is down.
  While the guest walks its own character, its controller goes to the host
  without A, B, X, L and R (the actions carry them). The guest playing Mario
  uses items and switches partners from its own Start menu at once, as
  before; the host's game answers those too, and a refused item takes the
  host's whole progress again. `TTYD_SYNC_OWN_SEEK=<frame>:npc:b` (tests)
  walks the guest's character to an NPC and presses B there (`:b`, `:x` or
  `:a` at the nearest enemy without `:npc`), `TTYD_SYNC_ITEM_TEST=<frame>:<item>`
  uses an item as the guest playing Mario.
- **Each guest's controller** still goes to the host's game every frame (with
  the last frames again, so a lost message costs nothing), and the host's game
  plays it as that seat's controller (Mario's, for a guest in seat 1; for
  the others, what the field actions don't carry), the newest that came. The host's word comes
  every frame while a guest plays together, and the guest draws the
  character it plays from the newest word, with no buffer. `TTYD_NET_DELAY_LOG=1`
  (tests) logs each guest press to the moment it shows
  (`tools/port/net_delay.py`).
- **Battles** always take the co-op guests as players, by seat: seat 2 plays
  the partner's (or Luigi's) turns and guards, seats 3 and 4 their
  companions' (layer 3). With the host's setting **Team bonus** on, a turn
  where everyone who acted landed their action command (Nice or better) adds a
  quarter bar of Star Power, and every screen shows it.
- **Saves.** The adventure is the host's: a guest's game never writes its own
  memory card while it plays together (a Save Block says so). Each guest
  keeps a copy of the host's save instead, in `<user>/coop/<host>_<room>.sav`:
  the host's game sends its save as it stands whenever it saves, changes
  rooms (at most every 30 seconds) and every 3 minutes, and the guest's game
  writes it whole and then renames it over the last (a crash never leaves
  half a copy). Leaving takes the guest back to the title, and a copy not
  taken yet is offered there and on the next launch, to put into a save file
  of the player's choice (never over one without asking). Only the host's
  save data crosses, as the 16 KB slot image the game reads, checked as a
  save before it's kept.
- **Single-player runs.** A Hardcore file or a leaderboard run can't host or
  join ("Hardcore files are single-player: they can't play online.").
- **Achievements** count in every player's game, each player's own.

The party modes take the guests out of following while they run (a versus
fight, the raid): each guest's game plays its part as below, and follows the
host again afterwards.

## Leaving and coming back

- **The host leaves.** When the host picks **Leave the room**, its game tells
  every guest at once; when its game closes or goes quiet, each guest waits 10
  seconds for it. Either way the guest's game never just times out: a window
  says "*Name* left the room. Your copy of the adventure is kept." and offers
  **Back to your own adventure** (the file the guest played before joining,
  or the newest on the card, as it was: a guest's game never saves while it
  plays together, nor makes a quick resume or autosave of the host's world)
  and **Title screen**. It comes at the next calm moment (the field, no
  battle or Start menu), with Mario's controls off until then.
- **A guest leaves.** **Leave the room** asks the same two choices.
- **Coming back.** A guest that joins the same room code or address again
  (or whose connection comes back by itself) has the same session id, so the
  host gives it the same player number and seat ("Welcome back: you're 2P
  again."), if no one has taken the seat meanwhile.
- **Players.** The Multiplayer page's **Players...** lists everyone in the
  room: their seat, their computer and their connection (good, fair or poor,
  from the round trip of the games' pings). The host can remove a player
  (asked twice): that guest sees "The host removed you from the room." with
  the same choices, and its game is turned away if it comes back this session.
- **Rules.** As a guest joins, a note says "Playing on *host*'s rules" or
  "Same rules as yours"; **Rules...** lists the rule settings that differ,
  the player's own value then the host's (never display, sound or controls).
- **The online service.** When the relay can't be reached (6 seconds without
  getting in, or 15 without it once in), every way in (Host, Join by code,
  the launcher's, an invite link) says "Can't reach the online service. Try
  again, or play on one computer." with **Try again** and **Back**; never a
  wait without end.
- **Names.** **Your name** (on the Multiplayer page before hosting or joining,
  and the launcher's) starts as the computer's user name, or Player; up to 24
  letters.
- **Co-op copies.** Settings > Saves > **Co-op copies** lists every copy
  kept (the host, the room, when, where in the story), with **Keep as a file**
  (into an empty file of the card, from the launcher) and **Delete**.

## Testing

From the environment (tests; the headless build has no relay, so it uses
addresses, or the local stand-in below):

```text
TTYD_SYNC_HOST=47800            the first player (the host)
TTYD_SYNC_JOIN=[::1]:47800      the second, third and fourth (or 192.168.1.20:47800)
TTYD_SYNC_HOST_ROOM=new         a room on the relay (or a given code)
TTYD_SYNC_JOIN_ROOM=K7P2-QX9M   join it
```

- `TTYD_SYNC_AT=<frames>` connects only after that many frames, so a test can
  join a game already under way.
- `TTYD_SYNC_LOG=<n>` writes a status line every n frames (60 by default).
- `TTYD_SYNC_PACE=<fps>` (tests) keeps a `TTYD_FAST` game to that many frames
  a second, so two unpaced test games stay roughly together.
- `TTYD_NET_FAULTS` (`port/include/port/online_faults.h`): network trouble on
  purpose (drop, dup, reorder, delay, disconnects).
- `TTYD_SYNC_TEST_RELAY=<address>:<port>` (tests): rooms on a local stand-in
  for the relay over plain TCP, nothing sealed, for builds without the
  relay's WebSocket (headless).
- `TTYD_ONLINE_VERSION=<name>` (tests): the build this game says it is.
- `TTYD_ONLINE_PROTOCOL=<n>` (tests): the protocol this game says it plays.
- `TTYD_SYNC_LIST=<name>` (tests): the room the host opens is in the room
  list under that name; `TTYD_SYNC_JOIN_ROOM=listed` (tests, with the local
  stand-in) joins the first room in the list.
- `TTYD_SYNC_PICK=<n>` (tests): the host picks n once linked, as the Online
  screen does (a party game 10-16); -1 tests the layers below alone, each
  player free in their own game, as the older scenarios do.
- `TTYD_SYNC_NAME=<name>` (tests): this player's name (the headless build has
  no Multiplayer page to set it).
- `TTYD_SYNC_MODS=<list>` (tests): the mods this game says it has on.
- `TTYD_SYNC_INVITE=<link>` (tests): the game joins the invite link's room as
  if the player had said yes (with `TTYD_SYNC_TEST_RELAY` on headless).
- `TTYD_SYNC_MENU_AT=<frame>:<pit|tower|versus|raid>[,...]` (tests): the
  engine's starts for the modes chosen at those frames (`versus+N`: the
  opponent stepped N times first, for players 3 and 4). A refusal that can
  clear (not connected yet, the other player busy, a shared battle or scene
  under way) is tried again each frame for up to `TTYD_SYNC_MENU_RETRY`
  frames (3600).
- `TTYD_SYNC_END_PICK=<own|title|retry|back>` (tests): the choice taken a
  second after "where now" comes up (headless always takes one: this, or the
  last).
- `TTYD_SYNC_REMOVE_AT=<frame>:<player>` (tests): the host removes that
  player then, as **Players...** does.

The test pass runner plays two to four of these games together as one
scenario (`online` in [TESTING.md](TESTING.md)); `tests/port/testpass/online_*`
are the soak's sessions and more (players 3 and 4 under faults, scenes with
four, chases, Brothers mode, refusals, an older release's game).

Each game writes `[sync]` lines: where the ghost is, every merge, every
object or enemy shared, every battle's start and end.

## The connection

By address, the host listens and each guest has one TCP connection to it,
made again when it drops (never stopping its game while it tries). The host is
the hub: up to three guests connect to it, and everything between two guests
goes through the host's game, tagged with the player it came from. The host
gives each guest its player number (2, 3, 4, in the order they arrive) in the
answer to its hello, and keeps it for that guest's session id, so a guest that
comes back after a drop is the same player; a fifth game is turned away
("The host's game has four players already"). Or by a room on the relay
([ONLINE_RELAY.md](ONLINE_RELAY.md)): the code is
`ROOM-SECRET`, the relay only ever told the room's half. The host is the hub
there too. The first player to join takes the relay's guest place (player 2);
when the relay says that place is taken, the next ones ask for a "peer" place
(players 3 and 4, two of them). A peer's messages reach the host marked with
its place, and the host's reach that peer alone, so the host keeps one
connection per player exactly as by address. The host and each player agree a
key of their own from the secret half (EC J-PAKE) and everything after it is
sealed (ChaCha20-Poly1305), so the relay can neither read nor change it, and
one player can't read another's messages to the host. The relay counts
messages, so the games send in batches: every 3 frames, and at once when a
game waits for the others. When a player comes into the room the relay tells
the host, and that pair's key is agreed again; a dropped connection takes its
place again with the relay's ticket. If one player's link goes quiet, the host
starts that pair's key again without leaving the room, so the others play on.
A relay from before the peer places turns a third player away: that game says
"That room has its second player, and this relay can't take a third yet (it
needs an update)", and the two in the room play on.

Either way there are two kinds of message:

- **Unreliable**: each player's place, the room's NPCs and objects. The newest
  one wins, and a lost one is replaced by the next.
- **Reliable**: story flags, the pouch, defeats, a shared battle's input. These
  are numbered and kept until the other game acknowledges them, and sent again
  when no acknowledgement comes (go-back-N). They are taken in order and only
  once. After a reconnect, each side sends whatever the other is missing.
  A test game ending at its last frame first pumps until everything it sent
  is acknowledged (at most 4 seconds), so a change made just before the end
  isn't lost with the program.

Each connection has its own numbering, its own log of what wasn't
acknowledged, its own hello and version check, and goes through
`TTYD_NET_FAULTS` on its own (a `disconnect_at` drops all of a game's
connections). The host keeps what it sends to a guest who is away, and sends
it when that guest is back.

**The same protocol, any build, any platform.** The hello carries the disc's
ID, the online protocol (`PORT_SYNC_PROTOCOL`, `port/statesync.h`) and Paper
Cut's version (with its commit for a build that isn't a release). What decides
is the disc and the protocol: two builds of one release a commit apart play
together, and so do a Windows game and an Android one. The protocol is a number
bumped only when the games would no longer agree: a message's layout or
meaning, the battle sync, the progress snapshot, or a rule every game must
apply alike. Two games of different protocols or disc releases don't play
together: the session is refused, the note says plainly which side is older
and must update (with both builds shown under it), and the joining game stops
trying. `tools/port/sync_protocol.py` (run by `testpass.py lint`) keeps a record
of the messages' layouts and fails when they change while the protocol
doesn't. The version still keeps the headless test build apart from the
windowed one.

**Every game the same.** The games are different programs (other compilers,
other layouts), so nothing that is an address of one program crosses:

- Every message is a struct of fixed-width fields (no pointer, no `long`, no
  bit-field), little-endian as every platform Paper Cut runs on, and each
  one's size is checked at compile time (`WIRE_SIZE`, `port/statesync.h`) by
  every compiler that builds the game.
- A shared battle's enemy (its stage, its group, its drop lists, the kinds of
  its units) crosses as its place in the area's battle table, the same data in
  every build, walked in one order; a group made at run time (Master Quest's)
  crosses by value, its units' kinds by their places; the music by its name.
- A command chosen on another player's screen names its weapons and its script
  as what they are: one of the battle's named moves and scripts, an item's, a
  Special's, a unit's data by its tag, or its place in the weapon menu, which
  each game builds again from the same battle.
- The progress crosses as flags, words, item ids and counts; the room's NPCs
  and objects by their names; settings by their names; a party game's state as
  its numbers; random numbers as the game's own generator's state (the port's
  `rand`, the same everywhere).

`TTYD_WIRE_CHECK=1` (tests) searches every message a game sends for anything
that looks like an address of its own program and logs `WIRE:` for each; a
build configured with `CFLAGS=-DTTYD_LAYOUT_PAD` in the environment has its data at
other places, as another program's would, for a test of two differently laid
out games playing together. Each shared battle's log says where each game has
its enemy's group (`layout: ...`): different places, the same group.

**The same mods.** The hello also carries the mods that change the game (those
on, other than texture packs, language packs and Lua-only mods), as lockstep
online's did. Games with different ones are refused the same way ("The other
player has different mods on (both need the same)."); the host's room stays
open for someone else. Lua mods don't refuse anyone: each game's player
messages carry its Lua mods' hash, and while any player playing with it has a
different set, a game's Lua mods are paused (those that only draw on the
screen still draw): a shared battle follows player 1's steps in every game, and a
mod changing the battle in one game only would only bring corrections. The Mods page says which.

Each game has a random session id. A hello with an id the other game hasn't
seen starts a new session: the numbering starts again, and each game merges
the other's whole progress (below).

## Layer 1: a shared world, each player free

**Where each player is.** Every 3 frames (a host with a co-op guest: every frame) each game sends:

- its player's map;
- position and facing;
- the model and pose being played (`a_mario`, `M_R_1`);
- the partner's model, pose and place;
- whether the player is in a battle, in an event (Mario's keys off) or in a
  menu.

The host passes each guest's on to the other guests. Every other player in
the same room is drawn as a **ghost**: their character's own model in their
pose, with their partner, from poses of its own in the game's pose pool
(`statesync_ghost_pc.c`; per player: the player, the partner, the Play as
pick standing in, Brothers mode's Luigi, and for co-op guests the host's
follower and companion; taken only while the pool has room to spare). A co-op
guest has no ghost of its own: the character it plays is the host's. It's drawn as the
partner and Mario are drawn, and it only draws: no game state, no random
numbers. Its place is interpolated 100 ms behind the other game's clock,
between the two samples around that time, and the clocks' offset is measured
from the samples. A ghost leaving the room is let go with its poses.

**The shared progress.** Each frame a game compares its progress with a copy
(the *shadow*), and sends what changed as reliable messages. The other game
applies them, to its own copy too, so they aren't sent back:

| What | A change (live) | A join (whole, both ways) |
|---|---|---|
| Story flags (GSWF, 8,192 bits) | the bits set and cleared, applied as they were | union |
| Story step (GSW 0) | the higher | the higher |
| Story words (GSW 1-2047) | the new value | the higher of each |
| Area flags and words (LSWF, LSW) | as GSWF and GSW, only between games in the same area (a new area clears its own: not sent) | union or the higher, same area only |
| Key items, items, stored items, badges | each item gained or lost (+1, -1 of an id), given or taken the game's own way (`pouchGetItem`, `pouchRemoveItem`, storage when the bag is full) | the more of each id |
| Coins, Star Pieces, Shine Sprites | the difference, added (coins 0 to 999); then the host's counts as they are, to every guest (two players' changes, each stopped at 0, don't add up the same in every order) | the higher |
| Mario's level | the higher level wins with its HP, FP, BP and Star Points; at the same level, the more Star Points | same |
| Star power, rank, boots, hammer | the higher (star powers: union) | same |
| Partners | joined: union (a partner joining one game joins the other); each partner's HP, attack and tech levels the higher | same |
| Each player's own | current HP and FP, equipped badges, which partner is out, where they are | |

With more than two players the host is where the progress meets: a guest's
changes go to the host, which applies them and passes them on to the other
guests (never back to the one they came from). A guest joining merges with the
host, which already holds everyone's progress, and the host passes the new
guest's whole progress on to the others, who merge it too; so every game ends
up with the same.

So all games share one pouch, and items aren't doubled: when one player
picks something up, the other gets it once, and when one uses an item it's
gone from both. A battle's coins and Star Points come across as the
differences they are. A join, even mid-game, needs no restart: the two games
merge what each has. Each `[sync]` status line carries a hash of each game's
story and pouch, and every 300 frames the games compare those hashes
("digest: story same, pouch same").

**What shows a change.** A flag changed in the other game is in this game's
memory straight away. A room that reads that flag when it loads shows the
change the next time it's entered, as when a flag changes in another room in
the original game.

**The ghosts' looks.** Each ghost is the player's Play as pick from this game's own models of it,
wearing their costume (fitted to that pick), with Brothers mode's Luigi beside
them when he walks in their field. **See-through players** under Online...
(this player's own setting) draws every ghost see-through.

## Layer 2: the room's enemies and objects

In a room both players are in, one game **owns** the room's NPCs (enemies and
townsfolk) and objects:

- whoever was there first, among all the players, owns the room;
- a player coming into a room another already owns follows that game;
- if two came at once (both claim it, or none does), the lowest player number
  owns it: the host first.

An owner who is a guest sends its lists to the host, which passes them on to
the guests in that room. Defeats, items taken and objects gone go to every
player, through the host.

The owner sends its NPCs and objects (below). The other game follows them
instead of running its own AI. Their regular events are stopped and frozen,
as the game itself freezes NPCs while a battle starts, and are given back when
this game owns the room again. While this player is in an event of their own
(talking, a scene), its NPCs are its own.

NPCs the owner's game makes while the room runs (a minion thrown, a
spawner's) that this game hasn't got are made here as stand-ins of the same
model, following the owner's like the rest (they can't be touched), and go
when the owner's do. In the owner's game an enemy chases whichever player is
nearest it: while its own find checks and events run, Mario stands where that
player is, and is put back right after; the player it reaches starts the
battle in their own game, which invites the others as any battle does.

These are shared both ways, whoever owns the room:

- **A defeat.** An enemy defeated in either game is defeated in the other: the
  game's own `npcGroupDead`, which plays its defeat and records it.
- **The defeat record.** Each map's record of which enemies stay gone
  (`FieldBattleData.deadInfo`'s mask) is shared, so a room the other player
  enters later has those enemies gone too.
- **An item picked up** in a room both are in is gone from the other's room.
  Items on the ground are matched by name, item id and nearness. The pouch's
  own sync (layer 1) gives the item once.
- **A map object deleted** (a brick broken, a block used up) is deleted in the
  other game.
- **A blue switch pressed** (one press for good: it opens a door, a bridge,
  the Pit's pipe) is pressed in the other game too, its own event and all,
  without Mario's jump.

Items and blocks that are collected for good set their flags (GSWF or LSWF),
which layer 1 shares, so a room loaded later has them gone or spent.

The per-type field lists, as sm64coopdx's sync objects declare theirs:

| Type | Owner → follower, unreliable | Both ways, reliable |
|---|---|---|
| NPC (enemy or townsfolk), every 3 frames, up to 64 | name, x y z, facing, the pose's name, dead, hidden | defeated (map, name, the map's defeat mask) |
| Map object (blocks, switches, springs...), every 15 frames, up to 64 | name, x y z (followed when off by more than 2 units), hidden and invisible flags, its type's state: a chest opened and a ? or badge block emptied show used, a floor switch pressed shows pressed | deleted (map, name) |
| Item on the ground | | picked up (map, name, item id, place) |
| The map's defeats | | `deadInfo` mask per map, union |

## Layer 3: battles together

When a player touches an enemy while others are in the room and free (in the
field, keys on, no menu; playing together, the guests always count as free
unless they're in a menu), the battle runs in all their games, each at its own
pace. Battles sync what matters too: no controller crosses, and no game
waits frame by frame. The game of the player who started the battle decides
it, and the others follow its steps; each player's own choices are made on
their own screen and sent when they make them. Up to four players share a
battle: the one who touched the enemy plays Mario, the next the partner,
players 3 and 4 a companion each.

1. Player A's game (the host's or a guest's) asks for the battle sequence
   (`fbatBattleMode`). At the start of the next frame, before the battle is
   built, A sends every player free in its room an invitation and waits. It
   holds the enemy's name, how it was hit (first strike), A's whole pouch,
   A's partner, and the enemy's battle as A's game set it up: its group,
   stage and drops, and the numbers A's room rolled for it (each game rolls
   its own when a room loads), each part as what it is in the game's data
   ("Every game the same", above). Guests reach each other
   through the host: a guest's battle messages go to the host once, marked
   with whom they're for, and the host passes them on.
2. Each invited player B takes A's pouch (keeping its own), A's partner and
   A's setup of the enemy. It then starts the same battle with its own copy
   of that enemy (the room's same NPC), as the game's own encounter does
   (`fbat` mode 5). At its next frame's start B is where A is: it says so
   (with the partner it had out) and waits.
3. A waits until everyone invited has come, said no, or let 3 seconds pass,
   then sends those who came the start and each one's place: A is player 1
   (Mario); the next places go by co-op seat (seat 2's player the partner, or
   Luigi in Brothers mode; seats 3 and 4 the companions), then by player
   number. Each of players 3 and 4
   gets a companion: the partner they had out in their own game when A's
   party has it, otherwise the next partner A's party has that isn't the
   partner or the other companion. When the battle menu's partner change
   swaps that partner into the fight, it's that player's to play, as in
   co-op's three- and four-player games. The places are the battle's, not the
   connection's player numbers (a guest can be A), so they travel with the
   start.
4. Each game loads the battle at its own pace (files and fades take a
   different number of frames in each game). All then meet in the battle
   sequence, just before the battle's map replaces the field's and the battle
   is built (`seq_battle.c`): A waits for everyone, the others for A. There
   all take A's game clock and random numbers. A game that doesn't get there
   in 8 seconds is left out.
5. From there each game runs the battle itself, at its own pace, with only
   what the battle decides crossing (reliable messages):
   - **A's steps.** At each step of the battle (a phase begins, a phase
     event is over, the next actor is picked, an action is over) A's game
     sends its verdict (who acts next; whether the battle is over; whether
     the phase is), its random numbers, who plays now, and a snapshot of what
     the battle keeps: every unit's HP and FP, its statuses and their turns,
     its moves left; Star Power and the bag's items; each side's clear
     condition. Every other game waits at the same step for it, takes A's
     snapshot where its own differs, and goes on with A's verdict, so every
     game passes the same steps in the same order. As an action's script
     starts, the random numbers come again, so everything in it rolls alike:
     those of the game whose player plays the acting unit (A's for the
     enemies). That game starts the action the moment its player chooses it,
     with no round trip to A and back; the others take its numbers.
   - **Each player's choices.** A unit's choices are its player's: Mario's
     player 1's, the partner's player 2's (or player 3's or 4's when it's
     their companion's partner; Luigi's player 2's in Brothers mode), a
     versus side's its player's, the enemies' moves a dungeon master's.
     That player's game opens the command menu and sends what was chosen
     (the move or item, its target, a tactic) when it's chosen; the other
     games skip the menu and take it. An action command's outcome, a guard
     (or Superguard) and a Stylish press are measured on the screen of the
     player who plays that unit, so the timing is fair to them, and sent as
     the outcome the battle reads; the other games' scripts hold at that
     point until it comes. The menu, the presses and their judging are that
     player's own game's, on its own clock, and never wait on another game,
     so a timed press feels as it does playing alone, whatever the ping
     (every kind: a timed press, a mash, a hold, the stick, a gauge, a
     guard and a Superguard, a Stylish). A Brothers-mode or duo move's two
     halves are each judged on their own player's screen the same way.
   - **An outcome late or lost.** Each game tells A where it is several
     times a second (its step and the outcomes it has passed). An outcome
     a game has passed that hasn't reached A a second later, or one A waits
     on while that game says nothing for a second and a half, is a miss
     (no press: a failed command, no guard, no Stylish). A sends the miss
     as its own, every game takes it, and the player's own game (which went
     on with its own) is put right by A's next step; each game logs it. A
     game that is merely behind (still before that point) is waited for.
   - **The crowd throws nothing** in a shared battle (no mushrooms, no
     rocks or cans), on every screen: each game's crowd would roll its own
     throws while its command menu is open, and the menus stay open for
     different times on each screen.
   - **Idle players in the audience** ([PLAYERS.md](PLAYERS.md#in-the-audience)):
     a player with no unit in the battle asks A's game for an emote or a
     throw; A's game checks it (one throw a turn, three a battle, only while
     a command is being chosen there), does it, and sends it to every game
     as an event (the throw, and when it lands what it did: the HP or FP
     given or taken, by the battle's unit ids). Every game, the asker's too,
     plays it from A's event; the HP or FP is A's, in its next step.
   A game waits only when what it needs hasn't come: another player's turn,
   their outcome, or (a game that isn't A's) A's next step. After half a
   second it says *Waiting for <name>...* (the player it waits on). A snapshot that differs corrects
   the game (each difference is logged: `step N: 2 corrections from player
   1's: unit 3 HP 5->4`); nothing ends a player's sharing over a difference.
   The battle's start and end screens (the tally, a level up) and the
   audience's BINGO reels are each screen's own; what they change is A's
   (its snapshot, its progress afterwards).
   - **A player whose link drops** (5 seconds unreachable, or 20 seconds of
     A waiting on a game that says nothing): A's player plays their units
     meanwhile with A's controller, as one player plays both in
     single-player, and every game is told. Their game plays the steps it
     missed from A's messages, faster while it's behind, and tells A how far
     it has got. Once it has caught up, or at the next phase's start if it's
     still on its way but heard from, its player plays their units again from
     that step on (each step's word says who plays, so every game agrees), and
     A waits for their choices as for anyone's while their game catches up. A
     game that keeps getting further is never taken as one that says nothing.
   - **A leaving** (or unreachable for 10 seconds): every other game finishes
     the battle on its own from where it is, its player playing every unit,
     and the battle ends at the end of the action under way as run away
     (unless A's last step had already ended it). A's game plays its battle
     to its end and shares its outcome as its progress, as any change of its,
     so the battle counts once, from the game that decided it. No other game
     takes over as A: the host is the hub, so a host gone leaves nobody
     linked, and a hand-over can't be made safe against two games each
     thinking they decide.
   - **A game whose session began again** (its game started over) is out of
     the battle; A's player plays its units from there.
   (Until 0.5.0, shared battles ran frame for frame from the same
   controllers, each game waiting where the battle read another's, and a
   hash every 30 frames ended a player's sharing at a difference.)
6. Back in the field, each game ends the battle. Each B gets its own pouch
   back. A's game then sends what the battle changed (coins, items used, Star
   Points) as its usual changes, which the others add to their own.

A battle a script starts (`evt_npc_battle_start`: a boss, a story fight)
goes the same way. Each B starts it as that function does, and afterwards
gives its Mario back his controls, as the script waiting for the battle does
in A's game.

If everyone invited is busy (a menu, an event, another room) or doesn't
answer within 3 seconds, A fights alone.

- **Settings, playing together.** The host's game is the world, so every rule
  of play is the host's, in the field and in battle alike (the guests' games
  show what the host's does), while display, sound and controls stay each
  player's own. Fast travel is the host's too: the host's trip (the Journal's
  map, the warp map) takes everyone along as a door does, whatever the guests
  have set, and a guest can't start one ("Playing together: the host travels,
  and everyone comes along.").
- **Settings.** A shared battle plays on the host's settings in every game:
  the host sends those that change play (not each player's display, sound or
  controls) when the session begins and when they change, and each guest
  plays the battle on them and has its own back after it. Mario's unit in
  each B's game is A's Play as pick for the battle. The field rules that
  change what the shared progress means are the host's for the whole
  session, in the field too: the bag's size, coins up to 9,999, a healing
  item eaten when the bag is full, the EXP and coin multipliers, Level sync,
  level-up healing, partner revival, the difficulty and the remake's balance,
  the badge rules, Master Quest, enemy variants, Chaos mode, the extra badges,
  recipes and prizes, the shop improvements and the battle reward rules. Each
  guest's settings file keeps the player's own meanwhile, and they are back
  when the session is over (no host for a minute, or off line).
  Outside playing together (a party game), fast travel stays each player's
  own setting, and works online only when every player has it on (each game
  tells the others its own); a trip waits for a shared battle or scene to end.
  Ambient sounds take no random numbers from a shared battle (when one starts
  again depends on the computer), so the games' rolls stay closer to A's between steps.
- **A game over** in a shared battle takes the other games to their game over
  too, also when the sharing had ended early (the connection) and the others
  were finishing the battle alone.

**Versus fights.** The host's Party picture menu has **Versus**:
its Versus page's set-up crosses with the start, and both games begin the
fight with it (the host as Mario's side, the guest as Luigi's), meet at the
battle's gate and play it as a shared battle. While a side chooses its
command only its player's controller is read; while it acts, both (the other
side guards); each side's player is their own game's controller, whatever
else is plugged in. Afterwards each game is back in its own field, with the
file's saved words as they were before the fight, and the guest's own versus
set-up is left as it was. (Versus is in the windowed build only; fights to
the end have been played by test scripts, not yet by people.)

**The party modes.** These are in the windowed build only and haven't been
played yet.

- **The quiz party** and **a Tattle-card duel against another player** are
  shows on one screen. The host's game plays the show. Each guest's game
  shows it as it goes and sends what its player presses, which the host's
  game takes as that player's controller. The host starts them. The card
  players' duels stay each game's own.
- **The dungeon master** plays the enemies from its player's controller in a
  shared battle.
- **The raid boss** is started by the host. Each guest's game starts the
  same raid (the same week, boss and player count) and goes to the boss's
  room with the same lent loadout. The host's game waits there for the
  others, up to 15 seconds, then starts the fight. The others join it as one
  shared battle; a guest that isn't invited within 25 seconds fights alone.
  While a raid runs, progress isn't shared either way: the lent loadout isn't
  anyone's. Changes from the other games wait until the raid is over.

**Why not a save state.** A save state would make the two games identical in
one go, but Paper Cut's save states hold the addresses of the process that
made them and are on the Aurora build only (`port/include/port/savestate.h`).
B would also lose its own field and room for the battle and have to be
restored afterwards. A battle is built from very little: the pouch, the
partner, the enemy's setup (the same room's same NPC), how it was hit, the
clock and the random numbers. So that little is sent, and each game keeps its
own field.

## Layer 4: scenes together

This layer is driven by the game where the scene plays:

1. A's game starts a scene: its Mario's keys are off for half a second, outside
   a talk, a battle or a menu.
2. If B is in another room, B is pulled in: warped to A's room, beside A, and
   held there (keys off) while A's scene runs.
3. B sees the scene as A plays it: A's ghost, and A's NPCs (A owns the room,
   layer 2). B's camera is held on A's. Each of A's text boxes opens on B's
   screen too, from B's own copy of the same message, with its speaker's
   tail; it closes when A's does.
4. A's story flags and items come across when it's over. B's own pouch is put
   back as it was when B came, so a scene the room also plays for B can't give
   B anything twice.
5. When A's keys come back, B is let go. Meanwhile B's screen says it's
   watching the other player's scene.

If B is busy (an event of its own, a menu), it's pulled in once it's free, if
the scene is still running.

Playing together, the host's scenes take every co-op guest, in the room too,
and so do the host's talks (a talk starts a "scene" as soon as the host's
keys go off): every guest sees the text boxes and the host's camera. A battle
a scene runs into takes the held guests along.

## The Pit and the Paper Tower

Not online games any more: the Pit of 100 Trials, the Paper Tower, chapter
select and the daily seed are single-player modes (the Modes page). The
engine's support for a run together stays in the code for tests
(`TTYD_SYNC_MENU_AT`), with no row on the Multiplayer page.

## Watchers

A guest can join to watch instead of play: **Watch...** under Online...,
with the room's code or the host's address (`TTYD_SYNC_WATCH=1` for tests).
In a room on the relay a watcher asks for a peer place, never the guest's, so
it doesn't keep a player out of the second place (on a relay from before the
peer places it takes the guest's instead).

- **Following the host.** The watcher's game goes to the host's room
  whenever the host changes rooms. Its own Mario and partner are hidden and
  kept where the host's Mario is, so the camera follows the host, whose ghost
  is drawn there. The watcher's keys stay off.
- **What it shares.** A watcher sends no progress of its own and takes the
  players' progress, as any game in the session does. It owns no room, isn't
  drawn in the players' games, and isn't asked into scenes or versus fights.
- **Battles.** A shared battle in the watcher's room takes it as a watcher.
  It loads the battle with the players and follows player 1's steps and the
  players' choices as every game does. Nobody waits for it, and it sends
  nothing. The host's battle can also be shared
  with watchers alone.

## The room list

The relay's room list ([ONLINE_RELAY.md](ONLINE_RELAY.md), "The room list")
works for these rooms as for 0.4's, with no change to the relay. A host
with **List my room** on gives the relay its whole code, its name, the disc's
ID and a version of `sync-p` and its protocol, `sync-p1` (`&list=`, `&name=`, `&game=`, `&v=`,
`&max=4`), and says `list` every 10 minutes while it hosts. **Room list...**
asks each relay for that disc and that version, so it shows only rooms of
this mode that this build can join (0.4's rooms, and older releases',
never appear in it, nor these rooms in theirs), with how many are in each,
and the players online on the relay. A room is joined from the list as by
its code: the guest's place, or a peer's when it's taken.

## Joining a battle under way

While the games are linked, every battle is a shared one, even when nobody
else is free in the room (player 1 then plays it alone). A game that's free
in the room of a battle under way, or a watcher there, asks player 1's game to
join it:

1. Player 1's game answers with the battle's invitation as it was and its
   start at the gate (the clock and random numbers), and takes the game on as
   a watcher.
2. The joining game starts the same battle from that start.
3. Player 1's game sends it the battle so far: every step, every player's
   choices and outcomes from the start (player 1's game keeps them until the
   next battle). It plays them, then the rest live.
4. Until it has caught up, its frames come up to eight times faster than real
   time (the same pacing a replay's fast-forward uses).

A battle is its start and what was decided in it, so this works without a save
state. The joining player can't act in it: its units were set when it began.

## The soak test

`tools/port/sync_soak.py` plays rounds of headless games together and checks
them. Each round is one kind of session: walking with items and a mid-game
join (`walk`), a shared script battle with two or three games (`battle`,
`battle3`), a player and a watcher (`watch`), a player joining a battle under
way (`late`), and through rooms on a local stand-in for the relay
(`tools/port/sync_test_relay.py`): walking (`relay`), a battle with four
games (`relay4`) and a listed room two games join from the list (`listed`).
Playing together has its own: the guest's stick walking the partner in the
host's game and the host going through a door (`coop`), the host's battle with
the guest as the partner (`coopbattle`), and the same with the guest a
differently laid-out program (`xplat`, with `--exe2` a build configured with
`CFLAGS=-DTTYD_LAYOUT_PAD`). Every round runs with `TTYD_WIRE_CHECK=1` and
fails on a `WIRE:` line or a crash.
With `--faults mix` every other round runs under `TTYD_NET_FAULTS` (drops,
duplicates, reordering, delays; walking by address also loses its
connection twice), each with a fresh seed.

```text
python tools/port/sync_soak.py --exe build-pc-us-headless/ttyd-us.exe \
    --iso orig/G8ME01/game.iso --out sync_soak            one round of each kind
python tools/port/sync_soak.py ... --hours 2 --faults on  for two hours, every round with faults
```

Each game runs with `TTYD_FAST=1`, a fresh user folder and a name of its own.
A round passes when no game logged `DRIFT`, every game linked and learned
every other player's name, and every game ended with the same progress hash
(the story, the area's flags, the pouch, the coins, Mario's level and the
partners). A battle round also needs the host's shared battle to have ended
back in the field. Each round adds one JSON line to `<out>/results.jsonl`, and a
failed round keeps its games' logs. The exit status is 0 when every round
passed and 1 when one failed. `--slot` runs the soak through a script that
hands out a shared slot for test games.

## Not done, and the limits

- **Battles while linked** are paced as shared battles, so Faster battles
  doesn't speed them up.
- **The scene on B's screen** is A's camera, text boxes, ghost and NPCs, not
  the scene played again: A's effects and the NPCs' animations between
  updates aren't there. Playing the scene in lockstep would need both rooms
  to be identical when it starts. A player already in A's room when the scene
  starts isn't held for it.
- **Players 3 and 4** play by address, and in a relay room once the relay
  has its peer places. A shared battle takes everyone free in the room; a scene pulls in every
  player in another room; a versus fight is the host against the player it chooses on the Online
  tab (the others play on). When a guest's game starts a new session in a shared battle the host also
  plays, the host says where that guest's controller ends; when the host only
  passes the messages on, the other guests wait for it until the 30-second
  limit and then finish alone.
- **Flags a game clears again.** These follow the change as it was made. A
  join's merge is a union, so a flag one game had cleared comes back from the
  other.

## Tried (headless, two to four games on one computer)

- **Layer 1.** Two players walking in Rogueport: each game's ghost was where the
  other game said its player was. A guest joining 1,100 frames in merged both
  games' progress both ways: the story, a partner who had joined, items and
  coins. Later changes crossed as they were made, and both games' story and
  pouch hashes ended the same. The same run under `TTYD_NET_FAULTS` (5% of
  messages dropped, duplicates, reordering, delays, two disconnects) also
  ended with the same hashes, after three resumed sessions.
- **Three players.** Three games walking in Rogueport: each game drew both
  others where their own games said they were, and the two guests followed the
  host's NPCs. Two guests joining at different times (1,100 and 1,400 frames),
  each with items, coins and flags of its own, before and after joining: all
  three games' story and pouch hashes ended the same.
- **Layer 2.** Rogueport's 22 NPCs followed the owner's places. A Spiky Goomba
  defeated in a shared battle in the sewers was defeated in both games.
- **Layer 3.** The same enemy touched in the sewers: a battle in lockstep for
  1,622 frames, identical in both games, ending back in the field at the same
  frame in each. The coin it dropped came across to the other player. A
  script battle ran identical for 11,700 frames. Four Goombas' script battle
  shared by three games and by four (players 3 and 4 with their companions),
  also with a guest as player 1 and the host as the partner: identical in
  every game for its 2,700-odd frames, all ending back in the field at the
  same frame, with the same story and pouch hashes. Two-game battles played
  exactly as before.
- **Watchers.** A watcher starting in another room followed the host into
  theirs. It then watched the host's four-Goomba battle, alone with the host
  and also beside a second player, for 2,772 frames. Its battle matched
  player 1's at every check, and it was back in the field with the others.
  The host never drew a ghost for it.
- **Four in a room.** Through a local stand-in for the relay (the same
  places and rules), four games in one room: the host, the guest, and two
  peers, as four players and as three players with a watcher who started in
  another room. The four-Goomba battle was shared by all four for 2,773
  frames with no difference anywhere, and every game ended with the same
  story and pouch hashes. Two games found a listed room in the room list
  and joined it (the second as a peer), and a third game against a relay
  without peer places was told why while the other two played on.
- **Joining under way.** A player in the pause menu when the host's battle
  started joined it 800 frames in. Its game played the battle from its start,
  matched player 1's at every check, and was back in the field at the end.
- **Layer 4.** The X-Nauts scene at Rogueport's harbour pulled the other player
  in from the square. It held them beside the first player for the scene's
  1,289 frames, with that game's 23 NPCs followed, then let them go. Their
  camera followed the first player's, and the scene's four text boxes opened
  on their screen with their speakers and closed with the first player's.
- **Names, mods and invite links.** Three games by address: each learned both
  others' names, the guests' through the host, and a name sent with markup
  in it arrived without it. Two games with different mods: each refused the
  other with the mods note, and the joining game stopped trying. A game
  given an invite link (with a query after the code) joined that relay
  room and played.
- **The soak test.** One round of each kind, every other one under faults:
  all eight passed with no DRIFT and equal progress hashes. With faults on
  every round, a shared battle's waits named the player waited for.
- **Playing together.** The guest's stick walked the partner in the host's
  game (from where it stood to 174 units away and back), and the guest's game
  drew it there a tenth of a second later; the host went through a door and
  the guest's game went along. The host's battle took the guest as the
  partner: the guest's moves, chosen on its own screen, played in the host's
  game with no correction in 40 steps, and the team bonus came twice.
- **Two different programs.** The same battle with the guest a build whose
  data lies elsewhere (its enemy group at another place from its code): the
  enemy crossed as its place in the area's table, the moves by name and print,
  and both games played all 40 steps alike. No message of any round carried
  an address of its program.
- Single-player fingerprints were unchanged.

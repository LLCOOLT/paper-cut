# Online play without a router to set up: rooms and the relay

Players share a **room code**. Behind it, the games meet in a room on Paper
Cut's relay (`port/relay`, `port/src/common/online_ws_pc.c`): a WebSocket over
TLS (Mbed TLS) from each game to the room, carrying the connection's byte
stream. Up to four play in a room: the host, the guest and players 3 and 4;
others can watch by the code. **By address** still works too: the host's port
forwarded, or a VPN such as Tailscale.

What the games send each other, and how each keeps its own game, is
[STATE_SYNC.md](STATE_SYNC.md)'s: online play is "sync what matters". This
page is the room code, the sealing and the relay.

## End to end: what the relay can't see or change

A code is `ROOM-SECRET` (`K7P2-QX9M`). The relay is only ever told the
room's half; the secret half never leaves the games
(`port/src/common/online_seal_pc.c`):

- In the room, the host and each player agree on a key from the secret with
  **EC J-PAKE** (Mbed TLS; P-256, SHA-256): one with the guest, one each with
  players 3 and 4. Watching it gives a relay nothing to guess the secret with
  offline; a relay that meddles gets one guess per try.
- **HKDF** makes the keys from it, one each way. Everything after the two key
  messages is sealed with **ChaCha20-Poly1305**; the nonces are the stream's
  count (it comes in order), so a message replayed or changed doesn't open,
  and the connection is dropped.

So whoever runs a relay sees that players met in a room, when, how much they
sent, and their public addresses (Cloudflare's connection to each); not their
names, their saves or the game. A session by address (no room) is not sealed:
it goes straight to the player you gave your address to.

## The relay

A Cloudflare Worker and a Durable Object per room (`port/relay/src/index.js`):
it passes each player's binary messages to the other and says, in text,
`you <address>`, `peer` and `gone`. It refuses a second host, a second guest,
a third seat (players 3 and 4) and a guest without a host.

### What the free plan holds

The Workers free plan: 100,000 Durable Object requests a day (WebSocket
messages into the relay count 20 to 1; messages out are free; each connection
is 1), and 100,000 Worker requests (each connection and each HTTPS request),
13,000 GB-s of Durable Object time a day (a room between messages is idle and
not billed: at Cloudflare's own 10 ms a message that's over 10 million
messages a day, five times what the requests allow, so the requests are the
limit). When a limit is reached the relay refuses connections until 00:00
UTC; **nothing is charged on the free plan**: no payment method, no upgrade.

Every room's games go through the relay: there is no direct path between
players in a room (by address is the way around the relay). So the relay carries about **2 million messages in a day**, for every room
together. What a room sends (relay messages in a second, from every game in
it; from 0.5.1 on, as the games' own timers make it: each game's log counts
its own, "net: relay messages: ... a minute of play"):

| A room | 0.5.0 | 0.5.1 |
|---|---|---|
| two players together (co-op), walking | 40 | 40 (what a player does still goes every 3 frames) |
| two players together, standing, text, menus | 40 | about 14 (the host's world 4, the guest's character 10) |
| two players together, in a battle | 40 | about 12 (the battle's steps and the heartbeats) |
| two players each in their own game, same room, moving | 40 | about 20 (every 6 frames each) |
| two players each in their own game, in different rooms | 40 | about 5 |
| four players together, walking | 120 | about 80 (the host's one message for all three) |
| four players together, standing, menus | 120 | about 34 |
| a watcher, on top | 40 | 1 to 5 |
| any of these with Direct connection on in both games, when their networks allow it (UDP) | | none after the lobby (about 10 in all) |

Over an evening of play together (about 40% walking, 35% standing, text and
menus, 25% battles): two players about 24 messages a second, 4,300 requests
an hour, so **about 23 hours of two-player rooms a day** (0.5.0: 14), or 46
player-hours; four players about 50 a second, **about 11 hours of
four-player rooms** (0.5.0: 4.6), or 44 player-hours. Players each in their
own game (the party modes) cost less: about 44 hours of two-player rooms a
day (0.5.0: 14). These are what the games' timers make, not yet measured on
the real relay: the dashboard's Durable Object requests a day are the truth.

The relay keeps its own count of its day (below, "To deploy (0.5.1)"):
`GET /budget` answers `budget <requests so far> <the day's limit> <date>`.
Past 80% a new room's host is told so (the game can say "The online service
is busy today"); past 95% new rooms of 0.5.1 games are turned away (as
Cloudflare does when the day is used up) so the rooms playing finish. The
host's game says it once as a notice ("The free relay is busy today: new
rooms may be turned away. Yours plays on.") and on the Multiplayer page,
with the time the day starts again in the player's own time; connecting by
address still works.

Paper notes, leaderboards, bingo rooms and the daily and raid boards run on
the same Worker and account (below), so their requests come out of the same
day.

When the relay is full, the game says so ("full for today ... connecting by
address still works") and stops trying until the player's Try again. A host
doesn't move to another relay by itself.

### More relays

`TTYD_ONLINE_RELAY` (CMake; `$TTYD_ONLINE_RELAY` for tests) holds up to 8
relay addresses separated by spaces, chosen by whoever builds the release;
players can't add any. Each is a free Cloudflare account with its own
allowance, someone you trust to keep it running: with the sessions sealed
end to end, a relay can't read or change them, only stop carrying them. A room's code says which relay it's on: a
hash (FNV-1a) of its room half, over the list's length. All four characters of the room half are random
(923,521 rooms; before 0.5.1 the first named the relay, leaving 29,791), so new rooms spread evenly over
the relays in the list. Both games have the same
list because they must play the same online protocol (`PORT_SYNC_PROTOCOL`,
[STATE_SYNC.md](STATE_SYNC.md)): a release that changes the list bumps it.

`TTYD_ONLINE_SERVICES` (CMake; `$TTYD_ONLINE_SERVICES` for tests) is the address paper notes,
leaderboards, bingo, daily and raid use. Empty (the default), they use the first relay, as before.

### Guards

So that one script can't squat rooms or use up the free day for everyone (`port/relay/src/index.js`):

- **An address's connections.** The Worker counts each address's room connections (30 a minute) and
  other requests (120 a minute) before any Durable Object is asked, and answers 403 "slow" past them (not "busy", which means the day is nearly used: the budget meter).
  With the rate limiting binding (`ROOM_LIMIT`, `HTTP_LIMIT`, commented out in `wrangler.toml`) the
  counts are Cloudflare's; without it, a best-effort count in each Worker's memory. At 30 a minute,
  trying every room half takes one address about 21 days.
- **A connection's messages.** One that sends more than 100 messages a second (a host: 300) or 4 MB a
  second for 3 seconds running, or 1,000 in one second, is closed (1008 "Too fast"), counted with the
  same per-connection count as the budget meter. A game sends
  about 20.
- **The host's say over its places.** The host can have the relay take a game out of its place:
  `drop guest` or `drop peer <N>` (text). A player the host removed is closed (4001 "Removed") and
  its ticket refused for the room's life. A game that held a place for 20 seconds without agreeing
  the key (no code, a squatter) goes with `... block`: closed (4002) and its address kept out of the
  room for 5 minutes. Older relays ignore the words; older games never send them.
- **Placement.** A room's Durable Object is made near its host (a location hint from where
  Cloudflare sees the host's connection); a 0.5.0 or later host is told `where <EDGE> <HINT>`, which
  it only logs.

### Paper Cut's relay

`wss://papercut-relay.llcoolt.workers.dev`, the build's default
(`TTYD_ONLINE_RELAY`), on a Cloudflare account on the Workers Free plan.
Deployed 2026-11-04; the 0.4.0 relay (seats, the room list, notes, boards, bingo, daily and raid) deployed 2026-11-05.

**To deploy (0.5.1): one message for all the host's players, and the day's
count.** `port/relay/src/index.js` adds, for hosts that ask (`&caps=1`, which
only 0.5.1 games send; older games' rooms are as before):
- the host is told `caps split`; it may then send one message for all its
  players: four 0xFD bytes, then for each player `[u8 place: 0xFF the guest,
  0 or 1 a peer][u32 length][bytes]`; the relay sends each part on exactly as
  that host's own message to that player would go (the guest's as it is, a
  peer's as a `TO_PEER` one's body). No stream message starts so (as a length
  it's over the relay's largest message). A part out of bounds or for no
  place closes the host (1007) and nothing of that message goes;
- every room counts what comes in on each connection (in its attachment, so
  hibernation keeps it) and adds it to the `Lobby`'s count for the day every
  2,000 messages on a connection and as it closes (one request each);
  `GET /budget` answers `budget <requests> <limit> <date>` (rooms, their
  connections and the Lobby's counting; notes, boards and bingo are left out);
  `RELAY_DAY_REQUESTS` (a variable) sets another limit than 100,000;
- a new `&caps=1` host is told `busy` past 80% of the day, and turned away
  past 95% (429, as the day used up: the game's "full for today" note); a
  host's reconnect (its ticket) is never turned away, nor any older game.

No migration (the Lobby keeps the count in its storage, `d:<date>`, two days).
A 0.5.1 host on the relay as deployed before: never told `caps split`, it
sends each player's batch on its own as before (`online_relay_diet4_oldrelay`).
`run_compat.sh` checks it ("split host: as it should be").

The same 0.5.1 deploy adds the relay's health report (`GET /health`, next
section). No new binding or migration: the Lobby keeps it. `/health` used to
answer only `Paper Cut relay`; `/` still does, for a quick "is it up".

### The relay's health

So the owner can see whether online play goes well for people, the relay
counts how it goes, per UTC day, for the last 14 days:
`https://papercut-relay.llcoolt.workers.dev/health` (text, newest day first)
or `/health.json` (the same as JSON). It's open to anyone, since it holds
nothing personal, and limited per address like the room list (120 a minute).

**What is counted:** plain numbers only.
- **rooms:** rooms opened (a new host; a host's reconnect isn't a new room),
  listed or private, sync (a 0.5.0 or later host, `&framed=1`) or lockstep
  (0.4.x), split (a 0.5.1 host, `&caps=1`); and room lifetimes: from the first
  player in to the last one out, in buckets (<1 min, 1-5, 5-15, 15-60 min,
  1-3 h, >3 h; a room everyone left and came back to counts twice).
- **peak:** the most players (watchers included, as "players online") and
  rooms online at once that day, from the Lobby's own count.
- **joins** by role: host, guest, seat, peer, watch (reconnects aside).
- **refused,** by reason: no host (a wrong or old code), host taken, full,
  watchers full, today (the day nearly used up), removed or blocked (the
  host's say), slow room (an address's 30 a minute), bad code, bad role, not a
  WebSocket. The relay refuses no game version: an older game is served as
  before, so there's no "old version" refusal.
- **reconnects** (a ticket given back): asked, resumed (and how many of those
  while the old connection was still there), expired (the room or the place
  gone) and refused (removed or blocked).
- **ends,** how each connection ended: clean (1000, 1005), away (1001: a
  restart, a closed page), dropped (1006: no close at all, 1011, 1015),
  replaced (4000: its reconnect took over), removed (4001), blocked (4002),
  flood (1008), too long (1009), bad split (1007), other. The relay has no
  idle timeout of its own; a connection Cloudflare drops shows as dropped.
- **player connections:** how long each lasted (watchers aside), in the
  room-lifetime buckets. A reconnect makes a new connection.
- **guards:** how often each fired: slow (other requests), flood, too long,
  bad split, drop, drop and block, a 0.5.1 host told busy; the line repeats
  the guard-made refusals (slow room, day full, removed and blocked coming back).
- **errors:** exceptions caught in the Worker, a room's connect, message and
  close handlers, and the Lobby. Each is still thrown, as before.

**What is never kept:** room codes, names, addresses (not even hashed),
tickets, user agents, or anything else per player or per room. A room sends
only how many of each thing happened. Nothing comes from the games' own data.

**How to read it.** Each day's block: `rooms`, `room lifetimes` and `peak`
say how much was played; `refused` and `reconnects` how often people couldn't
get in or back; `ends` how sessions finished. A good day has mostly `clean`
ends, few `dropped`, `expired` reconnects rare, `today` at 0 and `errors` at 0.
Many `no host` refusals with few rooms mean codes are mistyped or stale; many
`dropped` ends with many `resumed` reconnects mean connections are flaky but
the games recover; `flood` or `slow` counts mean someone is scripting the
relay. The `now` line is players and rooms online as the request came.

**What it costs.** Counting rides on what the relay sends anyway: each room
keeps its counts in memory and hands them to the Lobby with its report (a
join, a leave, a listing's refresh). Counts with nothing to ride on (a refusal
in a room nobody hosts, a guard firing mid-game) go on their own, at most once
a minute a room, a request counted in the day's meter. The Worker's own
(refusals at the front door, its errors) go with its next request to a room or
the Lobby. The Lobby keeps a record a day (`h:<date>`, one storage value),
written with each report; days past 14 are dropped. Counts are best effort: a
room let go of by Cloudflare with counts not yet handed over loses them.

`sh port/relay/test/run_compat.sh` checks the counts too (`health.mjs`: rooms,
joins, refusals, drops, reconnects, guards and errors played, the report read
back, nothing personal in it). The stand-in relay (`tools/port/sync_test_relay.py`)
answers `/health` and `/health.json` in the same shape, counting nothing.

**To deploy (0.3.0): watching and reconnecting.** `port/relay/src/index.js`
changed after that deploy: rooms take `role=watch` connections (up to 4), the
host's messages that start with four 0xFF bytes go to the watchers, and each
player's connection gets a ticket (`ticket <T>`) that lets it take its place
again after a drop. It has been tried with `wrangler dev` only; deploy it the
usual way (below). Until then, against the relay as deployed: watching through
a room fails ("role: host or guest"), watching by address works, and a
reconnect works only once the relay has noticed the old connection is gone
(it refuses a second host or guest until then). A new build is fine with the
old relay otherwise: the host only sends watchers anything once the relay
says some are there (`watch <N>`), which the old one never does.

**To deploy (0.5.0): players 3 and 4 when each plays their own game.** The
"your own games" online mode ([STATE_SYNC.md](STATE_SYNC.md)) reaches players
3 and 4 as `role=peer` connections (up to 2), which the relay addresses one
by one, unlike seats:
- a peer's messages come to the host wrapped as one of the stream's messages
  of type 0xF1 with the peer's place (as a seat's are, with 0xF0);
- the host's messages that start with three 0xFE bytes and a place go,
  without those four bytes, to that peer alone (no stream message starts so:
  as a length it would be over the relay's largest message);
- the host is told `peer <N>` when the peer in place N comes and `peergone
  <N>` when it leaves; a peer is told `peer` when the host is there and
  `gone` when it leaves;
- a peer takes its place back after a drop with its ticket, as the guest;
- the host asks for its guest's messages framed too (`&framed=1`, which only
  0.5.0 hosts send): it is told `framed`, and each of the guest's messages
  comes to it wrapped as one of the stream's messages of type 0xF2 (its
  length, 0xF2, the message). Every message to such a host is then framed
  by the relay, so a player can't make its bytes pass for another player's
  frames (a guest forging player 3's), nor run a frame on into the next
  player's message: the host reads each player's frames inside that
  player's message alone;
- a peer's message, and a framed host's guest's, is at most 1 MB less 64
  bytes (what the host's receive buffer holds with the wrapping): a longer
  one closes its sender (1009), as an over-long message always has. The
  games send theirs in pieces of at most 256 KB (a resume's backlog of
  snapshots and save images in several messages).

A 0.5.0 host on a relay without the framing (no `framed`) takes the guest's
stream as before, but leaves out any frame of the relay's own kinds (0xF0 and
up) in it: such a relay has no players 3 and 4 to pass for. `run_compat.sh`
also checks the framing (the new worker only: "framed host: as it should
be").

**0.4.x rooms are unaffected, and must stay so.** Players on 0.4.0 and 0.4.1
keep using this relay after 0.5.0 ships, so the relay keeps serving their
lockstep rooms (host, guest, seats, watching, the room list, tickets) exactly
as the 0.4.1 relay does. 0.5.0 retires lockstep online in the game only,
never in the relay. Every change to the worker is an addition old clients
never see.

Run `sh port/relay/test/run_compat.sh` before every deploy (it needs Node and
the 0.4.1 release tag). It loads the 0.4.1 worker and this one in Node with a
stand-in for Cloudflare's runtime, plays the same sessions through both, and
compares everything each connection received and every response:

- a listed room, a guest and its ticket reconnect;
- two seats and a third refused;
- five watchers (the fifth refused);
- the host's messages starting with 0xFE (still the guest's), and every
  leaving;
- the room list, with a 0.4.1 room, a 0.5.0 room and an unlisted one
  reporting in.

Each version's list must show only its own rooms (a 0.4.x client never sees
a 0.5.0 room, nor the other way), in the format 0.4.x reads. It ends with
"the same with both workers" and exits 0; anything else means don't deploy.

No migration is needed. The game asks for a peer place when the relay says
the guest's is taken ("That room is full"), and a watcher in that mode asks
for one first, so it never takes the guest's place from a player. Until it's
deployed, a room in that mode takes two players: a third is told "That room
has its second player, and this relay can't take a third yet (it needs an
update)" and stops trying, while the two in the room play on, and a watcher
takes the guest's place as before. By address any number up to four work.

**To deploy (0.5.1): the guards.** `port/relay/src/index.js` adds the [guards](#guards): the
address counts in the Worker, the message cap, `drop guest` / `drop peer <N>` [`block`], the location
hint and `where`. No migration is needed and every addition is one older games never meet: they
send no `drop`, are never told `where` (0.4.x hosts never ask for `framed`; 0.5.0 hosts ignore words
they don't know), and send far fewer messages than the cap. `run_compat.sh` checks them too (the new
worker only: "guards: as they should be"). The rate limiting binding is optional: try it with
`wrangler deploy` once the lines in `wrangler.toml` are uncommented; if the plan refuses it, leave
them commented (the memory counts work without it).

**To deploy (0.4.0).** `port/relay/src/index.js` and `wrangler.toml` add, in
one deploy:

- **Three or four players**: `role=seat` connections (up to 2: players 3 and
  4). The host's watchers' messages go to them too, each seat's messages go
  to the host (wrapped as a stream message of type 0xF0 with the seat's
  place), and the host is told `seatgone <N>`. A listed room says how many
  play of how many may (`&max=` from the host).
- **The room list**: the `Lobby` Durable Object and `GET /rooms` (below).
- **Paper notes and leaderboards**: the `Notes` and `Board` Durable Objects,
  `GET|POST /notes`, `GET|POST /board`, `GET /replay` and `POST /check`
  ([PAPER_NOTES.md](PAPER_NOTES.md), [LEADERBOARDS.md](LEADERBOARDS.md)).
- **Bingo rooms, daily boards and raid boards**: the `Bingo`, `Daily` and
  `Raid` Durable Objects and `GET /bingo/<ROOM>`, `GET /daily/<YYYYMMDD>`,
  `GET /raid/<WEEK>` (below).

Deploy it the usual way (below); Wrangler applies the migrations it hasn't
yet, in order: `v1` (`Room`), `v2` (`Lobby`), `v3` (`Notes`, `Board`),
`bingo-v1`, `daily-v1`, `raid-v1`. Optionally give it a secret for the
address hashes it keeps (`npx wrangler secret put HASH_SALT`, any long random
text); without one a fixed text is used.

Until then, against the relay as deployed: joining a room as player 3 or 4
fails ("role: host, guest or watch"; joining by address works); the Room list
says "The room list isn't on this relay yet."; the Paper notes page says
"Paper notes aren't on this relay yet." and the Leaderboards section
"Leaderboards aren't on this relay yet." (the old relay answers 404 to all of
these); bingo rooms, the daily board and the raid board can't be reached.
Everything else works as before: a host with "List my room" on only adds
query parameters the old relay passes by.

### Limits and refusals

Every refusal is a status other than 200 and one line, `<why>: <words>`
(`slower`, `today`, `mine`, `own`, `rated`, `gone`, `big`, `bad`, ...); the
games show their own text for it. A room's refusals keep their words ("No one
is hosting that room", "That room is full", ...): the games read them.

- A request's address (query and all) is at most 2,048 characters, a text
  body (a note, a check) at most 256 bytes, a replay at most 1 MB. Only GET
  and POST are taken.
- Who sent something is kept only as a hash of the address with `HASH_SALT`,
  for the daily limits and one vote each; the daily and raid boards add the
  day or the week to it, so their hashes can't be matched with another
  board's.
- A daily or raid board keeps at most 600 names and 1,000 senders: past 600,
  a new name goes on only if it beats the last, which leaves.
- `/daily/` takes only real dates.

What they cost on the free plan: an area's notes are one request when a
player enters it (kept 10 minutes), leaving or rating a note one; a board is
one request when it's opened, an upload or a check one, a replay's download
one. Storage: an area's notes are a few KB; a board keeps its 10 best runs'
replays (compressed, up to 1 MB each), in 96 KB pieces (the key-value API's
values stay under 128 KiB).

### The room list

The Start menu's Multiplayer tab can list a host's room ("List my room", off by
default): open to anyone, so the host gives the relay the code's secret half
with its name, its disc's ID and Paper Cut's version (`&list=`, `&name=`,
`&game=`, `&v=` on its connection). Every room tells the `Lobby` how many are
in it (one request each time someone comes or goes), a listed room while its
host is there and its places aren't all taken; the host refreshes its listing
with a text `list` every 10 minutes, and one not refreshed for 20 is dropped.
`GET /rooms?game=&v=` (only when a player opens the list or presses Refresh,
never polled) answers `rooms <players online>` and a line per listed room of
that release and version, newest first, at most 40:
`<CODE> <PLAYING>/<MAX> <name, percent-encoded>` (`1/4`). A
joining player then joins by that code as by any other. "Your own games"
rooms ([STATE_SYNC.md](STATE_SYNC.md)) are listed the same way, with `v=`
`sync-p` and the online protocol (`sync-p3` for protocol 3: any build of that
protocol can join them) and `max=4`; the list
is asked for with that version, so these rooms and lockstep's (and older
releases') never show in each other's lists, and the relay needs no change
for them. Costs: a listing
fetch is 2 requests (the Worker and the Lobby); each connection and its close
1 more Durable Object request.

### Bingo rooms

Lockout bingo ([MODES.md](MODES.md#bingo-modes-tab)) has rooms of its own on
the same relay: one `Bingo` Durable Object per room code, keeping the room's
card (the seed its first player brought), up to 8 players by name and who
claimed each square first. A player's game asks it with a plain HTTPS
`GET /bingo/<ROOM>?name=&seed=[&claim=<cell>,...]` every 5 seconds while the
room is open, and at once when a square is done; the answer is
`bingo <seed>`, `you <i>`, a `player <i> <name>` line each and a
`claim <cell> <i>` line per square claimed. A claim on a square someone has
changes nothing. A room is forgotten a day after it was made. Costs: 2
requests per ask (the Worker and the room), so two players for an hour are
about 2,900 of the free plan's 100,000 a day.

### Daily results boards

The daily seed ([MODES.md](MODES.md#daily-seed-modes-tab)) keeps a board per
day (UTC): one `Daily` Durable Object for each date. `GET /daily/<YYYYMMDD>`
answers `daily <date> <finished>` and the 50 best as
`<rank> <ms> <name> <game>` lines; with `?name=&ms=&game=&v=` it first takes a
time (the best per name stays, and the header adds that name's rank). A time
is taken only for today or the two days before, between 5 minutes and 7 days;
one address may send under 3 names a day (it's kept as a hash salted with the
date, never the address). A board is dropped 40 days after its first time
(an alarm). Nothing is polled: the board is fetched when a player asks.

### Raid boards

The raid boss ([RAID.md](RAID.md)) keeps a board per week (weeks since Monday
1969-12-29, UTC): one `Raid` Durable Object for each. `GET /raid/<WEEK>`
answers `raid <week> <boss> <clears>` (the week's boss by the same rotation as
the game's) and the 50 best as `<rank> <turns> <frames> <players> <name> <game>`
lines, the fewest turns first; with `?name=&boss=&turns=&frames=&players=&game=&v=`
it first takes a clear (the week's boss, 2 to 4 players, this week or the
last; the best per party name stays). One address may send under 4 names a
week (a hash salted with the week). A board is dropped 120 days after its
first clear.

### Deploying one

On a Cloudflare account on the Workers Free plan (Workers & Pages > Plans):
sign Wrangler in with only what a deploy needs, deploy, sign it out again.

```bash
cd port/relay
npx wrangler login --scopes account:read user:read workers_scripts:write
npx wrangler deploy
npx wrangler logout
```

It prints `https://papercut-relay.<subdomain>.workers.dev` (the subdomain is
the account's, Workers & Pages > Subdomain). The build's address is that with
`wss://`:

```bash
cmake ... -DTTYD_ONLINE_RELAY="wss://papercut-relay.<subdomain>.workers.dev"
```

The free plan's daily requests are the account's, shared by all its Workers:
an account whose other Workers are busy leaves the relay less.

### Testing on this computer

```bash
cd port/relay && npx wrangler dev --port 8787
```

Then games with `TTYD_ONLINE_RELAY=ws://127.0.0.1:8787`, one with
`TTYD_SYNC_HOST_ROOM=<code|new>` and the others `TTYD_SYNC_JOIN_ROOM=<code>`
([STATE_SYNC.md](STATE_SYNC.md) has the rest). `tools/port/sync_test_relay.py`
is a stand-in relay for the tests, and `sh port/relay/test/run_compat.sh`
checks the relay against the one deployed before (0.4.1's), so a deploy
never changes what older Paper Cut releases get.

## What the player sees of the connection

`port/src/common/online_status_pc.c` (`port/include/port/online_status.h`).
None of it changes what the games send each other.

**Joining, step by step.** A joining game goes through five steps, shown
under the picture while they run ("Joining (2/5): making a secure
connection...") and in the Multiplayer page's status:

1. reaching the online service (the relay's room; by address, the host's
   game at its address);
2. making a secure connection (the room's key; by address there is none);
3. checking you have the same version (the hellos: disc, protocol, mods);
4. getting the host's adventure (its whole progress, in co-op);
5. going into the host's world (this game in the host's room).

A party game, or a room where each player plays their own game, needs only
the first three. While the host is still on the title or its files, the
guest waits in the room ("waiting for the host to choose Play or Party...")
and no step's time runs. Each step has a time of its own (15 seconds for
the first three, 30 for the last two); past it, a window says which step
and what to do, with **Keep waiting** (the step's time again), **Copy
diagnostics** and **Leave the room**. A join that fails says at which step,
with the reason, **Try again**, **Copy diagnostics** and **Back**. The
online service not reached at all keeps its own window ("Can't reach the
online service"). The windows never come up over the Start menu or the
title's pages, which show the same in their status.

Every step goes to the log with its time (`join: step 2 key done in 120 ms
(total 460 ms)`), and the whole timeline once the join is through or ends
(`join: done after 2310 ms (ms per step: relay 340, key 120, ...)`).

**Refusals.** When the other game plays another online protocol, the newer
side's words say who must update, with both builds under them. When it's
this game that's older, the Multiplayer page and the window offer **Check
for updates**: the same check as at start (`port/settings/features/
update_check.cpp`), asked now whatever the setting says; once a newer
release is found, the page opens Settings, General, where **New version**
downloads it. A room's connection also names the protocol (`&p=3`), so a
relay could turn another protocol away before the key; the relay passes it
by today.

**The connection's quality.** Each game measures its round trips to the
players it talks to (a guest: the host; the host: each player), and how
long nothing has come from them. The slowest tenth of the last 10 seconds'
round trips decides: good under 100 ms, fair under 250, poor above, and poor
after 2.5 seconds with nothing; a change holds 2 seconds before it shows.
Online, a small icon (three bars) sits in the picture's top right corner in
the field and in battle: Multiplayer settings, **Connection icon**: Always,
When it's bad (the default: fair, poor or down) or Never. While a guest's
link to its host is down, or a room host's link to the relay, it reads
**Reconnecting...**. A player poor for 10 seconds gets one notice ("<name>'s
connection is unstable.", or "The connection to the host is unstable." for
a guest), and another only after 30 good seconds. The Players page shows
each player's round trip beside good, fair or poor, and whether the room
goes through the relay or by address.

**Copy diagnostics.** On the Online... page, beside a failed or slow join,
in the window, and on the settings page (Multiplayer, Online diagnostics):
a short text for a bug report, put on the clipboard and sent nowhere. It
holds the build, the protocol, the disc, the platform, the room's path and
state, each player's round trips, the join's steps and the last 40 online
log lines, with the players' names (P1 to P4) and the room code's secret
half (`****`) left out.

Tests: `$TTYD_JOIN_TIMEOUT_MS` gives every step that time,
`$TTYD_JOIN_PICK=wait|copy|leave|retry|update` chooses in the window a
second after it's up (with `copy`, the text goes to the log as `diag:`
lines), and `$TTYD_SYNC_NO_SNAPSHOT` makes a host that never sends its
progress, so a guest's join stops at step 4.

## Older releases

Paper Cut 0.4.x played online another way (both players in the same game,
in lockstep, with watchers and seats of its own). The relay still serves
them unchanged: their rooms, roles and room list stay as they were, and a
0.4.x room never shows in this release's room list (each lists under its own
version). This release can't join a 0.4.x room, nor the other way round.

## Invite links and the clipboard

`papercut://join/<CODE>` (`papercut://join/K7P2-QX9M`): the Multiplayer page's
**Copy invite link**, beside the room code. The game is the link's handler:
on Windows it writes `HKCU\Software\Classes\papercut` when it starts,
pointing at its own exe (the user's keys only; not from test runs with
`TTYD_FAST`); macOS and iOS declare it in `CFBundleURLTypes`, Linux in the
`.desktop` file (`x-scheme-handler/papercut`, `Exec=papercut %u`), Android
in an intent filter (`TtydActivity` hands the whole link on). The link comes
as an argument (Windows, Linux) or SDL's drop event (macOS, iOS, Android)
(`port/settings/features/invite.cpp`); the launcher starts the game on its Multiplayer page, or the
Start menu's in a running game, offers its room. It never joins by itself.

A room code on the clipboard is pasted from the Multiplayer page's keyboard
(**Paste**); the clipboard is read only then, and never sent anywhere.

## Not done

- **UPnP**: asking the host's router to open a port, for playing by address.
- Tried between games on one computer, through the local relay and the
  stand-in; not yet between two networks, nor with phones.

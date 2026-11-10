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

The Workers free plan: 100,000 requests a day (WebSocket messages into the
relay count 20 to 1; messages out are free; each connection is 1), 13,000
GB-s of Durable Object time a day (a waiting room hibernates: nothing). When
a limit is reached the relay refuses connections until 00:00 UTC; **nothing
is charged on the free plan**: no payment method, no upgrade.

| A game | Messages in, a second | Requests an hour | Relayed play a day |
|---|---|---|---|
| through the relay, batched by 3 | 40 (20 each) | 7,200 | about 14 hours |
| direct (UDP) | none after the lobby (about 10 in all) | none | unlimited |
| watchers (1 to 4) of one game, on top | 2 (the host's batch every 30 frames) | 360 | about 277 hours |

Watchers cost the same however many there are: the host sends each batch
once and the relay copies it to each of them (messages out are free); they
send nothing. Besides the batches: each watcher's connection (1 request),
and for one who joins late, the game so far, one message per 8,192 frames
(2.3 minutes of play). No watcher, no cost: the host sends nothing for them.

A WebRTC call needs its relay about 10-20% of the time; if Paper Cut's
players are alike, the relay's 14 hours carry the few that can't connect
directly while everyone else plays direct.

When the relay is full, the game says so ("full for today ... connecting by
address still works") and a host moves to the **next relay in the list** by
itself.

### More relays

`TTYD_ONLINE_RELAY` (CMake; `$TTYD_ONLINE_RELAY` for tests) holds up to 8
relay addresses separated by spaces, chosen by whoever builds the release;
players can't add any. Each is a free Cloudflare account with its own
allowance, someone you trust to keep it running: with the sessions sealed
end to end, a relay can't read or change them, only stop carrying them. A room's code says which relay it's on: its
first character's place in `PORT_ONLINE_CODE_CHARS`. Both games have the same
list because they must play the same online protocol (`PORT_SYNC_PROTOCOL`,
[STATE_SYNC.md](STATE_SYNC.md)): a release that changes the list bumps it.

### Paper Cut's relay

`wss://papercut-relay.llcoolt.workers.dev`, the build's default
(`TTYD_ONLINE_RELAY`), on a Cloudflare account on the Workers Free plan.
Deployed 2026-11-04; the 0.4.0 relay (seats, the room list, notes, boards, bingo, daily and raid) deployed 2026-11-05.

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
`sync-p` and the online protocol, `sync-p1` (any build of that protocol can join
them) and `max=4`; the list
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

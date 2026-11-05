# Online co-op without a router to set up: rooms, the direct path, the relay

Players share a **room code**. Behind it, Paper Cut finds the cheapest way
between the two games:

1. **Directly, over UDP** (`port/src/common/online_ice_pc.c`, libjuice's ICE,
   as sm64coopdx's CoopNet uses): each game gathers its addresses (on its
   network, its router's public side from Cloudflare's free STUN server
   `stun.cloudflare.com`, IPv6), the lines go through the room, and both send
   at once so their routers let the other in (hole punching). Most home
   connections get through this way. The session's packets then repeat each
   input the other side hasn't confirmed, so a lost one costs nothing.
2. **Through the relay** (`port/relay`, `port/src/common/online_ws_pc.c`),
   when there's no direct way (some mobile networks, routers that change the
   port for each destination): a WebSocket over TLS (Mbed TLS) to the room,
   carrying the connection's byte stream, the input in batches of
   `PORT_ONLINE_BATCH` (3) frames, two frames more of input delay.
3. **By address** (the old way, still there): the host's port forwarded, or a
   VPN such as Tailscale.

The input delay comes from the round trip, measured in the lobby: the host
pings the guest every 0.4 seconds while they're there (the direct path pings
on its own), and the delay is set from the median of the last nine (the
quicker of the first one or two), so one ping caught in a stall can't push it
up to the most.

The host chooses when it starts the game: direct if the path is there, the
relay otherwise, and tells the guest (`MSG_LINK`).

## End to end: what the relay can't see or change

A code is `ROOM-SECRET` (`K7P2-QX9M`). The relay is only ever told the
room's half; the secret half never leaves the two games
(`port/src/common/online_seal_pc.c`):

- In the room, the two games agree on a key from the secret with **EC J-PAKE**
  (Mbed TLS; P-256, SHA-256). Watching it gives a relay nothing to guess the
  secret with offline; a relay that meddles gets one guess per try, and a
  host closes its room after 5 tries with another code (31^4, about 920,000,
  secrets).
- **HKDF** makes four keys from it: each direction, the stream and the UDP
  packets. Everything after the two key messages is sealed with
  **ChaCha20-Poly1305**: the handshake, the host's settings and memory card,
  the direct path's addresses, each frame's input, the goodbye. The stream's
  nonces are its count (it comes in order); each packet carries its own, and
  one not newer than the last is dropped (a repeat). A message that doesn't
  open ends the try: the guest is told to check the code's second half.
- In a room nothing unsealed is taken but the key's two messages and, before
  the game, a goodbye (it can only end the try, as a relay could anyway by
  closing the connection), shown in the game's own words.

So whoever runs a relay sees that two players met in a room, when, how much
they sent, and their public addresses (Cloudflare's connection to each);
not their names, the host's save, the game or the addresses they use
directly. A session by address (no room) is not sealed: it goes straight to
the player you gave your address to.

## The relay

A Cloudflare Worker and a Durable Object per room (`port/relay/src/index.js`):
it passes each player's binary messages to the other and says, in text,
`you <address>`, `peer` and `gone`. It refuses a second host, a second guest,
a third seat (players 3 and 4, below) and a guest without a host.

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
list because they must be the same version.

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

The Start menu's Online tab can list a host's room ("List my room", off by
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
joining player then joins by that code as by any other. Costs: a listing
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

## From the Start menu

The Start menu's Online tab (`port/src/aurora/pause_online_aurora.c`,
`port/include/port/online_menu.h`) runs the same lobby from a game that is
already running. A session must begin with the game, so when the host starts,
both programs start again (as a recovery does) and the session begins from
their start-up: the host hosting the same room (from the title, or from quick
resume's last save), the guest joining it. A launcher's guest of a Start-menu
host joins again by itself; a Start-menu guest of a launcher's host tells it
(`PORT_ONLINE_BYE_REJOIN`) and the host starts by itself once it is back. Not
on phones and tablets (their apps can't start themselves again): there it
says to use the launcher.

While a lobby is under way the Start menu opens on the Online tab, and its
news shows over the picture too, so the host can play on with the menu closed:
the other player here (again every 30 seconds while they wait), gone, or
unable to play (another version, release or mods: the room stays open, its
code the same). The games starting again show a page while they wait for each
other; Esc or B there plays alone instead, and a start that doesn't come is
said in the game. During a session the Start menu is the host's (its
controller drives it in every game): the others' Start says so, and any player
can leave from the settings page (Multiplayer, Online play, Leave the online
game), the others told and playing on alone.

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

Then two games with `TTYD_ONLINE_RELAY=ws://127.0.0.1:8787`, one with
`TTYD_ONLINE_HOST_ROOM=<code>` and the other `TTYD_ONLINE_JOIN_ROOM=<code>`
(8 characters, a dash allowed; `TTYD_ONLINE_NO_ICE=1` on both: the relay
alone). Tried (2026-11-04, Windows, US), sealed: through the relay, 4,048
frames identical, 1,355 messages from the guest (one per 3 frames); direct,
4,048 frames identical, 10 relay messages for the whole session; a host whose
first relay answered "full" opened its room on the second; a closed game
ended the other's session at once ("the other player left"); a guest with
another second half was told to check it while the host kept its room, the
right guest then played; five wrong tries closed the room. Through the
deployed relay (the same day, both games on one computer): through it, 4,048
frames identical at 7 frames of input delay (1,356 messages); direct, 4,048
frames identical at 3 (10 relay messages).

## The modes online

Every game of a session must play the same frames, so a mode plays online
only from what all of them have:

- **Started together.** The Start menu runs in step in every game (the host's
  controller drives it), so chapter select, the Paper Tower, the trophy room,
  battle puzzles, the quiz party and the Tattle-card duels start from its
  Settings tab (Modes) in the same frame everywhere. The full settings page is
  one player's alone: online it says to use the Start menu instead. The
  Westside's card players start a duel when talked to, as offline: the talk is
  the game's own.
- **The host's settings and day.** Daily shop specials, the daily puzzle and
  the Paper Tower's day climb follow the host's settings and the session's
  clock (the host's start), so its date. Chaos mode draws its effects from
  that clock at the same frames in every game.
- **Input.** The quiz party's answers and a card duel's picks are each
  player's own controller, which crosses like any other input.
- **The host's files.** What a mode plays from that is a player's own file
  goes with the host's setup, as named blocks after the randomizer's settings
  (the setup's flag 8, `port/include/port/online_session.h`): the Enemy
  maker's enemies (`enemies.txt`), the Tattle card deck (`cards.txt`) and the
  stickers (`Stickers/<game id>/file<n>.txt`). Every game of the session, the
  host's too, plays from those for the rest of the run; a guest keeps the
  stickers in `<user>/online` and never writes the host's into its own files.
  A block the host didn't send is empty for the session.

Still off online: save states, fast-forward, frame advance, rewind, the
Practice tab's editors, warps and the Twitch chat (one game's alone, by
design); partner skins (a character pack from one player's own folder,
loaded into the game's memory); paper notes (left and rated from the Start menu,
which runs in every game of the session alike: each game would leave the same note); ghost races (a race starts the game again from one player's
recording).

## Watching

A third (and fourth, up to 4 in a room) can join only to watch: the same
room code (or the host's address), then **Watch**. A watcher's game starts
from the host's setup as a guest's does (the run's settings, the host's
memory card in `<user>/online`, or the host's save with Continue), then plays
both players' input as the host played it, in lockstep, a little behind
(`port/src/common/online_net_pc.c`, `online_pc.c`):

- **The players never wait for it.** The host keeps each frame's two inputs
  and sends the watchers a batch every 30 frames (runs of the same input
  packed), with its fingerprints; nothing comes back. By address each
  watcher has its own connection on the host's port (written without
  waiting; one that falls 64 MB behind is let go); through a relay the
  host sends each batch once.
- **A watcher who comes late** gets the game so far first (in messages of
  8,192 frames) and plays it unpaced until it's near ("catching up").
- **Waiting for a batch**, its game is held (the title: "Waiting for the players...");
  once it has had to wait, it waits for a second's input before going on, so
  it doesn't stop at each batch.
- **Fingerprints**: it compares its own with the host's; one that differs is
  told once ("this game drifted from the players'") and it plays on. Nobody's
  game ends for it.
- When the players start again from the host's save (recovery), so does the
  watcher; when the host leaves or ends the session, watching ends.

Through a relay the watchers' messages are sealed too, but not with J-PAKE:
one message has to open for every watcher, and they send nothing, so the key
is the whole code through PBKDF2 (SHA-256, 200,000 rounds; the host makes it
in the lobby, not during the game), ChaCha20-Poly1305 with a random 4 bytes
per program and a count as the nonce. That is weaker than the players' own
channel: a relay that keeps the watchers' messages could try the secret
half's 31^4 values offline (slowly: each try costs the 200,000 rounds). What
they carry is the input, the fingerprints, the players' names and the host's
setup (its settings and memory card).

## Three or four players

Players 3 and 4 ([PLAYERS.md](PLAYERS.md)) join as **seats**: the same code (or the
host's address), then **Join as player 3 or 4**, before the host starts (one
who comes later is told to watch instead). A seat is a watcher that plays:

- it gets what watchers get (the host's setup, with every player's name and
  the seats' ids, so each seat knows its place; then the frames as played,
  with the host's fingerprints), but while there are seats the host sends
  them each frame by address, every `PORT_ONLINE_BATCH` frames through a
  relay, and a seat plays each frame as soon as it has it;
- it sends the host its controller for each frame it plays (`S_PAD`; through
  a relay a batch of 3 at a time, sealed with the watchers' key);
- the host puts the next of each seat's controllers into each frame it sends
  (its input to the guest carries its own and the seats'; the direct UDP
  path's packets carry them too, with fewer frames repeated in each), its
  last again when none has come, and from the frame after a seat leaves, no
  controller there. Every game plays the same frames: the seats never hold
  the players up.

Through a relay a seat costs about 20 messages a second (its batches), and
the host's frames for the seats another 20 while there are seats (one
message for them all, the watchers too). A seat that drops doesn't come back
in that session; the others play on. A recovery (out of step) starts the
seats again with the players, and the host waits for them (up to 20 seconds
after the guest) before it starts.

## Reconnecting

When a session's connection drops (closed, or nothing for 8 seconds), the
game holds and both sides make it again, for up to a
minute; only then does the session end as before. While a game is held, for
the other player or for a reconnect, its window's title says why, and after
half a second a small notice over the picture says it too ("Waiting for
LT...", "Reconnecting..."), for players in full screen. The notice is drawn
over the frame the game had already drawn, and that frame is shown once; none
of the game's own work runs meanwhile, so both games stay in step (a second
notice in the same wait is shown on a dark screen). A player whose window is
put away (hidden, or the app in the background on phones and Macs; a
minimized window on Windows keeps playing) tells the other player, whose
notice then says "LT minimized the game.", and says so again each second:
the wait for them doesn't count as quiet meanwhile, so no reconnect starts.
The ways back:

- **In a room**: the WebSocket to the room again, in the same role, with its
  ticket (the relay lets the new connection take the old one's place); the
  key again (J-PAKE); the direct path tried again, the host choosing the way
  as in the lobby.
- **By address**: the guest connects again; the host takes it on its port
  (it keeps listening through the session, for watchers too).
- Then each says the first of the other's frames it still needs (`RESUME`,
  with the session's token: a hash of the host's setup) and sends the other
  what it's missing from the input it keeps (512 frames), so lockstep goes
  on from the frame where they stopped. "Reconnected: the online game goes
  on."

`TTYD_ONLINE_DROP_AT=<frame>` (tests) drops one side's connection at that
frame; `TTYD_ONLINE_NO_RECONNECT` turns reconnecting off.

## Invite links and the clipboard

`papercut://join/<CODE>` (`papercut://join/K7P2-QX9M`): the Online tab's
**Copy invite link**, beside the room code. The game is the link's handler:
on Windows it writes `HKCU\Software\Classes\papercut` when it starts,
pointing at its own exe (the user's keys only; not from test runs with
`TTYD_FAST`); macOS and iOS declare it in `CFBundleURLTypes`, Linux in the
`.desktop` file (`x-scheme-handler/papercut`, `Exec=papercut %u`), Android
in an intent filter (`TtydActivity` hands the whole link on). The link comes
as an argument (Windows, Linux) or SDL's drop event (macOS, iOS, Android);
the launcher opens on the Online tab with the code filled in and asks "Join
room K7P2-QX9M?" (Join, Watch, Not now). It never joins by itself.

When the Online tab opens and the clipboard holds a room code (8 characters
of `PORT_ONLINE_CODE_CHARS`, a dash allowed, nothing else), it offers "Join
room K7P2-QX9M from the clipboard?". The clipboard is read only then, never
sent anywhere, and a code this player copied isn't offered back.

Tried (2026-11-04, Windows, US, three games on one computer, `wrangler dev` with
this relay; `TTYD_ONLINE_WATCH_ROOM=<code>` / `TTYD_ONLINE_WATCH=<host>:<port>`
start a watcher in tests): through a room, host, guest and a watcher that
joined in the lobby: host and guest 8,367 frames identical, the watcher's
8,341 frames identical to the host's, its fingerprints in step with the
host's batches. **Not yet tried**: watching by address (the run used
127.0.0.1, but a test host listens on `::1` only: use `[::1]:<port>`), a
watcher joining mid-session (the history), the invite link and the
clipboard offer in the launcher, the Windows registry key
(`TTYD_URL_SCHEME_TEST=1` writes `papercut-test` to try it), Android, Apple
and Linux builds of the link handling.

**Reconnecting keeps the games in step** (2026-11-04). With
`TTYD_ONLINE_DROP_AT=2500` on the guest, through a room, the two games had
gone out of step two frames after the reconnect (three runs out of three).
The cause wasn't the stall: `TTYD_FINGERPRINT_PARTS` (which also writes each
frame's controllers as played, the random numbers and the game clock) showed
the same input and the same clock on both sides, and only the game's random
numbers different, from the same frame on in every run. The cause was the
"Reconnected" notice. The game's font takes two of the game's random numbers
for each letter it draws (fontmgr.c, `_JUTFont_DrawPos`, for its noise
texture), and a frame is drawn before its logic runs. The two sides
reconnect a frame apart (the guest stalls on one frame, the host on the
next), so one game drew a frame more of the notice than the other. The
port's own words drawn with the game's font (the co-op notices, the 1P/2P
tags, the settings hint, the battle popups) now put the random numbers back
as they were. That also covers notices one side shows and the other doesn't,
a different language on each side, a watcher's "drifted" notice (which used
to drift it further), and the recovery's "started again" notice.

After the fix, through a room: host and guest identical over every frame both
wrote, 10,331 frames (7,800 of them after the reconnect), and the session
went on to the end of the run. **Not
yet tried**: by address, and with a watcher during the reconnect.

**Paper Cut's text in the game's own menus** (2026-11-05). Those fixes were
per feature, and the game's own code also draws Paper Cut's text: a duo
move's name and help in the battle menu, Director's Cut's badges in the
item boxes and shops, a mod's messages. In a battle with the duo move's help
on screen, one game in English and one in French took different random
numbers from the frame the list opened. The font now tells Paper Cut's text
apart itself (`port/include/port/port_text.h`): every draw made by the port's
code, and every text the game draws that came from the port (a message
`msgSearch` answers from the port's tables, `portTrGame`'s, and the copies
the game makes of them for its boxes, banners and message windows), puts
the random numbers back after; the game's own text takes them as on the
GameCube. The same battle in English and in French: identical over every
frame; the single-player fingerprints unchanged.

## Not done

- **UPnP**: asking the host's router to open a port (for the cases hole
  punching can't cross).
- **Cloudflare's TURN service** (1,000 GB a month free, then $0.05/GB): its
  pages don't say whether it needs a card or stops when the allowance runs
  out, so it's left out.
- Tried between two games on one computer, through the local relay and the
  deployed one; not yet between two networks, nor with phones.

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
`you <address>`, `peer` and `gone`. It refuses a second host, a third player
and a guest without a host.

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
Deployed 2026-11-04.

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
deployed relay (the same day, this PC, both games here): through it, 4,048
frames identical at 7 frames of input delay (1,356 messages); direct, 4,048
frames identical at 3 (10 relay messages).

## Not done

- **UPnP**: asking the host's router to open a port (for the cases hole
  punching can't cross).
- **Cloudflare's TURN service** (1,000 GB a month free, then $0.05/GB): its
  pages don't say whether it needs a card or stops when the allowance runs
  out, so it's left out.
- Tried between two games on one computer, through the local relay and the
  deployed one; not yet between two networks, nor with phones.

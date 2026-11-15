#!/usr/bin/env python3
"""A local stand-in for the online relay's rooms (port/relay/src/index.js), for the
headless build's tests of "sync what matters" by room (sync_soak.py; the games reach it
with $TTYD_SYNC_TEST_RELAY=127.0.0.1:<PORT>: the headless build has no WebSocket or sealing).

TCP, frames [u32 length][u8 kind][data]: kind 0 binary, 1 text, 2 a refusal (then closed).
A client's first frame is the text "OPEN /room/<HALF>?role=host|guest|peer[&ticket=T]" (a host's
may list the room: &list=<SECRET>&name=<NAME>&game=<ID>&v=<VERSION>&max=<2-4>). The room
answers "open", "you <address>", "ticket <T>", and "peer" to both once both are in; it passes
each one's binary frames to the other and says "gone" when one leaves. A ticket takes its place
again (the old connection replaced, no "gone"). Refusals keep the relay's words.
Peers (players 3 and 4, and sync's watchers): up to 2, as the relay's role=peer: the host told
"peer <N>"/"peergone <N>", a peer's messages to the host wrapped [u32 len][0xF1][N][message], the
host's that start FE FE FE N to that peer without the four bytes. A host that opens with
&framed=1 is told "framed", and the guest's messages come to it wrapped too, [u32 len][0xF2]
[message] (so no player's frames can pass for another's); a framed message over MAX_FRAMED closes
its sender.
Every minute each room's log has "rate" lines: the messages each place sent in that minute and in all (each
one in is what Cloudflare counts, a twentieth of a request of the relay's day), by the wall clock (a test's
games run faster than 60 frames a second: the games' own "net: relay messages" lines count by minutes of play).
"OPEN /rooms?game=<ID>&v=<VERSION>": one frame of kind 3, the relay's room list text
("rooms <online>" then "<CODE> <PLAYING>/<MAX> <NAME>" for each listed room with a free place).
"OPEN /health" (or /health.json): one frame of kind 3, the relay's health report in its shape (who's here now,
today's block empty: the stand-in counts nothing).

test_relay.py PORT [--kill ROLE:SECONDS ...] [--quiet ROLE:SECONDS[:FAIL] ...] [--log FILE] [--no-peer] [--no-framed]
              [--forge SECONDS] [--full-today] [--no-caps] [--budget N]
              [--hostile SEED:PERCENT[:ROLES[:UNTIL[:FROM]]]]
              [--lifetime SECONDS]
--kill: that role's connection closed after that many seconds from its first open (a drop).
--go FILE: --kill's, --quiet's, --zombie's, --forget's and --seconds' times count from when FILE appears (testpass's "relay_go": a game's log line).
--quiet: that role's connection goes quiet that many seconds from its first open, as the deployed relay's
did mid-session: never closed, nothing more passed to it or from it (the others' messages to it lost), and
FAIL seconds later (default 10) reset, so the game sees no close, then a read failure, unless it gave up on
the quiet first. ROLE: host, guest, peer0 or peer1 (players 3 and 4's places); a ticket takes the place again.
--no-peer: role=peer refused as the relay before peers does ("role: ...", 400).
--no-framed: peers, but a host's &framed=1 not answered, as the relay with peers from before the framing.
--full-today: every room's open refused as the relay is when Cloudflare's daily requests are used up (a 429:
"The relay is full for today ..."), to see the games stop asking.
--never-answer: every connection taken and never answered (its bytes read and dropped): a relay behind a dead uplink, as
portable as a test can make one (an unrouted address is refused at once on some networks); the Aurora build's
WebSocket (TTYD_ONLINE_RELAY=ws://127.0.0.1:<PORT>) stays connecting until its upgrade gives up.
--hostile SEED:PERCENT[:ROLES[:UNTIL[:FROM]]]: a hostile peer (docs/TESTING.md, "Online fuzzing"): of every binary message
passed on to ROLES (comma-separated, default all: host, guest, peer0, peer1), PERCENT go with a mutated copy
(seeded by SEED) as well, or in its place: the relay message's room frames and the messages inside them
(the test relay's are unsealed) truncated, grown, bytes flipped, lengths that lie, types changed, counts and
floats set to edge values, NaN or infinity; old messages replayed, duplicated, held back and sent after
newer ones; junk text ("peer 7", tickets, random bytes), empty and too-big messages. Each one is logged.
UNTIL: only until that many seconds from the relay's start (then the games must be playing together again);
FROM: only from that many seconds on (the games' keys agreed first: a key's round mutated is a wrong code).
--no-caps: a host's &caps=1 not answered, as the relay before 0.5.1: no "caps split" (one message for all its
players: SPLIT FD FD FD FD then [u8 place: FF the guest, 0/1 a peer][u32 length][bytes] parts, each passed on
as the host's own message to that player), no "busy".
--budget N: the relay's day is N requests, counted as index.js does (each connection one, each message in a
twentieth): a new caps host is told "busy" past 80% and refused past 95% (a 429's words; its reconnect isn't).
--net SPEC: every connection's frames to it held as a real link would (tools/port/netshape.py: latency, jitter,
TCP-like loss, bursts, stalls, bandwidth); --net-role ROLE=SPEC for one role's alone (over --net); --seed N.
A spec's stalls count from the role's first open (a reconnect keeps the same schedule).
--restart SECONDS[:DOWN]: that long after the first host's open the relay restarts: every connection cut, every
room and ticket forgotten, nothing listening for DOWN seconds (default 3), then rooms anew (a deploy, an eviction).
--halfopen ROLE:SECONDS: that role's connection goes silent both ways and stays open for good (no reset: the
game must notice by itself). --zombie ROLE:SECONDS: the relay forgets that role's connection (its place free,
the others told it's gone) but leaves its socket open and silent. --forget ROLE:SECONDS[:HOLD]: the relay no longer
knows that role's ticket and keeps its place taken HOLD seconds more (default 15), as a real relay keeps a vanished
game's connection a while: with --quiet at the same moment, its tries are turned away as a new game's (host or
guest); the game must join afresh, not ask with that ticket for ever. --drop-word WORD:N: the
relay's word WORD (e.g. framed) never sent the Nth time (a host's reconnect's welcome without it: the host must
learn the relay again, never take it for an old one from one missing word). --short ROLE:SECONDS: a message of
three bytes (a length's start, nothing after) to that role that long after its first open: the game must not wait
on it for good (every later message would be read out of step). --big ROLE:SECONDS: one message of the relay's
biggest (1 MB less 64 bytes: a frame of a kind no game takes, zeros after; its first 1000 bytes 0.3 s before
the rest) to that role that long after its first open: the game must read it whole and play on (the stand-in pipe's reading stopped for good on one).
Every 5 seconds (while they change) a "counts" line: the frames into the relay by role and kind.
--lifetime SECONDS: each connection closed that long after it was let in, as the relay's old 6-hour cut did:
"close 1009" (the games take it as "come back now", as a restart).
--forge SECONDS: that long after the guest is first in, a message as the guest's carrying a FROM_PEER
frame for place 0 (a key agreement's first step) goes to the host (the host must leave it out).

0.5.1's guards, as the relay's (port/relay/src/index.js): a host's text "drop guest" / "drop peer <N>"
takes that game out (a refusal frame "Removed: ..." and the close; the host told "gone" / "peergone N";
its ticket refused after), "... block" (a squatter) closes it without words and keeps its address out
BLOCK_SECONDS (here: the stand-in's own attackers' mark, &x=..., since every game is on 127.0.0.1).
--squat SECONDS: that long after the host is first in, an attacker takes the guest's place without the code
(sends garbage, never a key round), and comes back each second for 90 s: the host must take it out.
--flood SECONDS: that long after the host is first in, an attacker in a peer's place sends 400 messages a
second; closed as the relay closes a connection over FLOOD_MESSAGES a second FLOOD_SECONDS seconds running.
--guess N: two seconds after the host is first in, an attacker tries N random room halves, held to the
relay's ROOM_OPENS_A_MINUTE an address; its tally logged ("guess: ...").
"""
import argparse
import asyncio
import random
import re
import collections
import json
import os
import secrets
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import netshape  # noqa: E402
from urllib.parse import parse_qs, quote, urlparse

LOG = sys.stdout


def log(*a):
    print("[relay] %.2f" % time.time(), *a, file=LOG, flush=True)


class Room:
    def __init__(self):
        self.conn = {"host": None, "guest": None}
        self.ticket = {"host": None, "guest": None}
        self.list = None  # the host's listing: (code, name, game, v, max)
        self.framed = False  # the host asked for the guest's messages framed (&framed=1)
        self.refused = set()  # tickets the host had taken out ("drop ...")
        self.mark = {}  # a place's connection's &x= (the stand-in's attackers), for "drop ... block"
        # the messages each place sent in (what Cloudflare counts: a twentieth of a request each), this minute
        # and in all, logged as "rate" lines (rate_logger)
        self.counts = {}
        self.total = {}
        self.since = time.time()
        self.split = False  # the host asked for one message for all its players (&caps=1): "caps split"

    def playing(self):
        return sum(1 for k, c in self.conn.items() if c is not None)

    def count(self, place):
        self.counts[place] = self.counts.get(place, 0) + 1
        self.total[place] = self.total.get(place, 0) + 1


def rate_line(half, room, label, counts, seconds):
    n = sum(counts.values())
    per = ", ".join("%s %d" % (k, counts[k]) for k in sorted(counts))
    return "rate %s %s: %d messages in over %.0f s (%s): %.0f a minute, %.0f requests of the relay's day an hour" % (
        half, label, n, seconds, per or "none", n * 60.0 / max(seconds, 1.0), n * 3600.0 / 20.0 / max(seconds, 1.0))


def day_used():
    """the relay's day so far as index.js counts it: each connection a request, each message in a twentieth"""
    return CONNECTS[0] + sum(sum(rm.total.values()) for rm in ROOMS.values()) / 20.0


CONNECTS = [0]


def split_parts(data):
    """a SPLIT message's parts [(place, bytes)], or None when it's a bad one (index.js closes the host)"""
    parts, at = [], 4
    while at < len(data):
        if len(data) - at < 5:
            return None
        place, n = data[at], struct.unpack_from("<I", data, at + 1)[0]
        at += 5
        if n == 0 or n > len(data) - at or (place != 0xFF and place > 1):
            return None
        parts.append((place, data[at:at + n]))
        at += n
    return parts or None


async def rate_logger():
    """every minute, each room's messages in that minute and in all (the games' own count is the log's
    "net: relay messages" lines; this is the relay's side of it, by the wall clock)"""
    while True:
        await asyncio.sleep(60)
        for half, room in list(ROOMS.items()):
            if room.counts:
                log(rate_line(half, room, "last minute", room.counts, 60.0))
                log(rate_line(half, room, "in all", room.total, time.time() - room.since))
                room.counts = {}


NO_PEER = False
NO_FRAMED = False  # --no-framed: &framed=1 not answered (the relay with peers from before the framing)
FORGE = None  # --forge SECONDS: that long after the guest's first open, a forged frame as its own (forger)
FULL_TODAY = False  # --full-today: every room's open refused as the relay's 429 (its day used up)
NEVER_ANSWER = False  # --never-answer: every connection taken and never answered (a game's WebSocket upgrade waits for good)
NO_CAPS = False  # --no-caps: a host's &caps=1 not answered (the relay before 0.5.1: no split, no "busy")
BUDGET = None  # --budget N: the relay's day is N requests (index.js's count: "busy" at 80%, new hosts refused at 95%)
SPLIT = b"\xfd\xfd\xfd\xfd"
FROM_GUEST = 0xF2
MAX_FRAMED = (1 << 20) - 64  # a guest's (framed) or a peer's message at most, as the relay's MAX_FRAMED


SQUAT = None  # --squat SECONDS
FLOOD = None  # --flood SECONDS
GUESS = 0     # --guess N
PORT = 0
BLOCK_SECONDS = 300           # the relay's BLOCK_MS
FLOOD_MESSAGES = 100          # the relay's, a second
FLOOD_SECONDS = 3
ROOM_OPENS_A_MINUTE = 30      # the relay's, an address
BLOCKED = {}  # an attacker's mark -> until (time.time())
OPENS = {}    # an attacker's mark -> [times of its opens]
REMOVED = b"Removed: the host removed you from the room."
HELD_BACK = b"The relay is holding this network's tries back a moment: it tries again by itself."
DROP = re.compile(r"^drop (guest|peer ([01]))( block)?$")


ROOMS = {}
KILLS = []
QUIETS = []  # --quiet: (role, seconds, fail seconds)
DEAF = set()  # the connections gone quiet (quieter): nothing sent to them, what they send dropped
FIRST = {}
LIFETIME = None  # --lifetime SECONDS
NET = ""  # --net: every connection's link
NET_ROLE = {}  # --net-role ROLE=SPEC
SEED = 1
LINKS = {}  # writer -> Link (its shaped frames)
ROLE_START = {}  # (half, role) -> its first open (ms): a role's stalls keep one schedule over reconnects
CONNS = set()  # every writer still open (--restart cuts them)
ZOMBIES = []  # --zombie: (role, seconds)
FORGETS = []  # --forget: (role, seconds, hold seconds)
HELD_PLACE = {}  # --forget: role -> until when its place stays taken (its old connection, as kept a while)
SHORTS = []  # --short: (role, seconds)
BIGS = []  # --big: (role, seconds)
WLOCKS = {}  # --big: writer -> asyncio.Lock held while its split frame goes out
DROP_WORDS = {}  # --drop-word: word (bytes) -> [the time it's dropped, the times so far]
COUNTS = collections.Counter()  # frames into the relay by role and kind (relay_bridge.py's request estimate)
SERVER = None


def now_ms():
    return time.monotonic() * 1000.0


class Link:
    """one connection's frames to it, let go at their release times (netshape.Shaper), in order"""

    def __init__(self, w, spec, name, start):
        self.w = w
        self.sh = netshape.Shaper(spec, SEED, name)
        self.sh.start = start
        self.name = name
        self.q = collections.deque()
        self.ev = asyncio.Event()
        self.task = asyncio.ensure_future(self.pump())

    def put(self, data):
        self.q.append((self.sh.delay(now_ms(), len(data)), data))
        self.ev.set()

    async def pump(self):
        while True:
            while not self.q:
                self.ev.clear()
                await self.ev.wait()
            t, data = self.q[0]
            d = t - now_ms()
            if d > 0:
                await asyncio.sleep(d / 1000.0)
                continue
            self.q.popleft()
            if self.w.is_closing():
                return
            if self.w in DEAF:
                continue
            self.w.write(data)
            try:
                if not await drained(self.w):
                    return
            except ConnectionError:
                return

    def close(self):
        self.task.cancel()
        log("net", self.name, self.sh.stats())


def shape(w, half, role):
    """the connection's link (--net, --net-role), if any"""
    spec = NET_ROLE.get(role, NET)
    if not spec or w in LINKS:
        return
    start = ROLE_START.setdefault((half, role), now_ms())
    LINKS[w] = Link(w, spec, f"{half}/{role}", start)


def unshape(w):
    link = LINKS.pop(w, None)
    if link is not None:
        link.close()


ROLE_OF = {}  # a connection's role (--hostile's ROLES)
HOSTILE = None  # --hostile: (random.Random, percent, roles or None, until: time.time() or None, from: time.time())
HISTORY = []  # --hostile: the last messages passed on (replays)
STOPPED = []  # --hostile: its UNTIL passed (said once)
HELD = {}  # --hostile: a message held back per connection (sent after the next: reordered)
EDGE32 = [0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFF, 0x10000, 0xFF, 0x100, 16, 255, 0xFFFFFFFE]
# NaN, +infinity, -infinity, the largest float, 1e10
FLOATS = [struct.pack("<I", v) for v in (0x7FC00000, 0x7F800000, 0xFF800000, 0x7F7FFFFF, 0x501502F9)]


def mutate_bytes(rng, b):
    """one mutation of a byte string (any level's)"""
    b = bytearray(b)
    op = rng.randrange(11)
    if op == 0 and b:
        del b[rng.randrange(len(b)):]
    elif op == 1:
        b += bytes(rng.randrange(256) for _ in range(rng.choice([1, 8, 64, 700, 5000])))
    elif op in (2, 3) and b:
        for _ in range(rng.randrange(1, 9)):
            b[rng.randrange(len(b))] ^= 1 << rng.randrange(8)
    elif op == 4 and b:
        b[rng.randrange(len(b))] = rng.choice([0, 1, 2, 3, 4, 7, 8, 15, 16, 0x7F, 0x80, 0xFE, 0xFF])
    elif op in (5, 6) and len(b) >= 4:
        at = rng.randrange(len(b) // 4) * 4
        b[at:at + 4] = struct.pack("<I", rng.choice(EDGE32))
    elif op == 7 and len(b) >= 4:
        at = rng.randrange(len(b) // 4) * 4
        b[at:at + 4] = rng.choice(FLOATS)
    elif op == 8 and b:
        at = rng.randrange(len(b))
        for k in range(at, min(len(b), at + rng.randrange(1, 33))):
            b[k] = rng.randrange(256)
    elif op == 9 and HISTORY:
        o = rng.choice(HISTORY)
        cut = rng.randrange(len(b) + 1)
        b = b[:cut] + o[rng.randrange(len(o) + 1):]
    else:
        b = bytearray(rng.randrange(256) for _ in range(rng.choice([0, 1, 4, 5, 9, 40, 600])))
    return bytes(b)


def frames_of(data):
    """a relay message's room frames ([u32 len][kind][data]...) as (kind, data), None when it isn't that"""
    out, at = [], 0
    while len(data) - at >= 5:
        n, kind = struct.unpack_from("<IB", data, at)
        if n == 0 or len(data) - at - 4 < n:
            return None
        out.append((kind, data[at + 5:at + 4 + n]))
        at += 4 + n
    return out if at == len(data) and out else None


def pack_frames(frames, lie=None):
    return b"".join(struct.pack("<IB", ((len(d) + 1) if lie is None else lie) & 0xFFFFFFFF, k) + d
                    for k, d in frames)


def session_start(data):
    """a relay message holding a key's round (room frame kinds 1 and 2) or a hello (game message 1), maybe inside
    the relay's wrapping: left as it is (garbled, they're another code's or another version's, which end the
    try for good: the in-process fuzzer's layers h and k try those)"""
    frames = frames_of(data) or []
    for kind, d in frames:
        if kind in (0xF1, 0xF2) and session_start(d[1:] if kind == 0xF1 else d):
            return True
        if kind in (1, 2) or (kind == 3 and len(d) >= 5 and d[4] == 1):
            return True
    return False


def hostile_mutant(rng, data):
    """a mutated copy of a relay message: at its own level, a room frame's, or a game message's in one"""
    frames = frames_of(data)
    if frames and len(frames) == 1 and frames[0][0] in (0xF1, 0xF2) and rng.randrange(4):
        # the relay's own wrapping (a framed host's): the player's message inside it mutated
        kind, d = frames[0]
        head, inner = (d[:1], d[1:]) if kind == 0xF1 else (b"", d)
        m, how = hostile_mutant(rng, inner)
        return pack_frames([(kind, head + m)]), how + " (inside the relay's wrapping)"
    level = rng.randrange(4) if frames else 0
    if level == 0:
        return mutate_bytes(rng, data), "message"
    frames = list(frames)
    i = rng.randrange(len(frames))
    kind, d = frames[i]
    if level == 1:
        what = rng.randrange(5)
        if what == 0:
            frames[i] = (rng.randrange(256), d)
        elif what == 1:
            frames.insert(rng.randrange(len(frames) + 1), frames[i])  # (a frame twice)
        elif what == 2:
            rng.shuffle(frames)
        elif what == 3:
            return pack_frames(frames, lie=rng.choice([0, 1, len(d) + 2, 0xFFFFFFFF, 1 << 20])), "frame length"
        else:
            frames[i] = (kind, mutate_bytes(rng, d))
        return pack_frames(frames), "frames"
    # a game message in a sealed frame (kind 3; the test relay's aren't sealed): [u32 len][type][payload]
    if kind == 3 and len(d) >= 5:
        typ = d[4]
        body = d[5:]
        if level == 2:
            typ = rng.choice([typ, typ, rng.randrange(256), 2, 3, 4, 10, 11, 12, 13, 14, 15])
            body = mutate_bytes(rng, body)
        elif typ == 4 and len(body) >= 5:
            # a reliable one: its inner type and payload mutated, its number kept
            body = body[:4] + bytes([rng.choice([body[4], rng.randrange(256)])]) + mutate_bytes(rng, body[5:])
        else:
            body = mutate_bytes(rng, body)
        inner = struct.pack("<IB", len(body) + 1, typ) + body
        if rng.randrange(8) == 0:
            inner = struct.pack("<I", rng.choice(EDGE32)) + inner[4:]
        frames[i] = (kind, inner)
        return pack_frames(frames), "game message type %d" % typ
    frames[i] = (kind, mutate_bytes(rng, d))
    return pack_frames(frames), "frame kind %d" % kind


async def hostile_send(w, role, data):
    """--hostile: this message (on its way to ROLE) as it is, and maybe a mutant too, or in its place"""
    rng, percent, roles, until, start = HOSTILE
    if time.time() < start:
        await send(w, 0, data, plain=True)
        return
    if until is not None and time.time() > until:
        if HELD:
            for held in list(HELD.values()):
                await send(w, 0, held, plain=True)
            HELD.clear()
        if not STOPPED:
            STOPPED.append(1)
            log("hostile: stopped")
        await send(w, 0, data, plain=True)
        return
    if roles is not None and role not in roles:
        await send(w, 0, data, plain=True)
        return
    HISTORY.append(data)
    if len(HISTORY) > 200:
        del HISTORY[:100]
    held = HELD.pop(id(w), None)
    if rng.random() * 100 >= percent:
        await send(w, 0, data, plain=True)
        if held is not None:
            await send(w, 0, held, plain=True)
        return
    if held is not None:
        await send(w, 0, held, plain=True)
    if session_start(data):
        await send(w, 0, data, plain=True)
        return
    what = rng.randrange(12)
    if what <= 5:
        m, how = hostile_mutant(rng, data)
        log("hostile:", role, "a mutant (%s), %d bytes," % (how, len(m)), "in its place" if what == 0 else "too")
        if what != 0:
            await send(w, 0, data, plain=True)
        await send(w, 0, m, plain=True)
    elif what == 6:
        log("hostile:", role, "an old message again")
        await send(w, 0, data, plain=True)
        await send(w, 0, rng.choice(HISTORY), plain=True)
    elif what == 7:
        log("hostile:", role, "twice")
        await send(w, 0, data, plain=True)
        await send(w, 0, data, plain=True)
    elif what == 8:
        log("hostile:", role, "held back (after the next)")
        HELD[id(w)] = data
    elif what == 9:
        t = rng.choice([b"peer 7", b"peer -1", b"peergone 9", b"peer x", b"framed", b"you x", b"open",
                        b"ticket " + bytes(rng.randrange(97, 123) for _ in range(rng.randrange(200))),
                        bytes(rng.randrange(1, 256) for _ in range(rng.randrange(120)))])
        if rng.randrange(20) == 0:
            t = rng.choice([b"peer", b"gone", b"peergone 0", b"peer 0"])  # (real ones restart a key: rarely)
        log("hostile:", role, "a text:", t[:40])
        await send(w, 0, data, plain=True)
        await send(w, 1, t)
    elif what == 10:
        log("hostile:", role, "an empty message")
        await send(w, 0, data, plain=True)
        await send(w, 0, b"", plain=True)
    else:
        n = rng.choice([(1 << 20) - 64, (1 << 20) - 4096, 300000])
        log("hostile:", role, "a big message of %d bytes" % n)
        await send(w, 0, data, plain=True)
        await send(w, 0, bytes(rng.randrange(256) for _ in range(256)) * (n // 256), plain=True)


async def send(w, kind, data, plain=False):
    if w is None or w.is_closing() or w in DEAF:
        return
    if kind == 1 and data in DROP_WORDS:
        # --drop-word: that word's Nth time (to anyone) never goes out, as if lost on the way
        DROP_WORDS[data][1] += 1
        if DROP_WORDS[data][1] == DROP_WORDS[data][0]:
            log("drop-word:", data.decode(errors="replace"), "(time %d) not sent" % DROP_WORDS[data][1])
            return
    if HOSTILE is not None and kind == 0 and not plain:
        await hostile_send(w, ROLE_OF.get(w, "?"), data)
        return
    if w in LINKS:
        LINKS[w].put(struct.pack("<IB", len(data) + 1, kind) + data)
        return
    try:
        # (a connection half-closed meanwhile, as a restart or a kill leaves it: write_eof already called, a
        # RuntimeError the client's task died of, its traceback in the run's output)
        lock = WLOCKS.get(w)  # (--big's split frame: nothing else into the stream between its parts)
        if lock is not None:
            async with lock:
                w.write(struct.pack("<IB", len(data) + 1, kind) + data)
                await drained(w)
        else:
            w.write(struct.pack("<IB", len(data) + 1, kind) + data)
            await drained(w)
    except (ConnectionError, RuntimeError):
        pass


STALL_S = 20  # a game that reads nothing of what waits for it this long: its connection closed


async def drained(w):
    """what was written gone out to the game; a game that stopped reading (its buffers full, STALL_S) has its
    connection closed and said so, as a real relay lets go of a connection it can't write to. Awaited for ever,
    one game's stall stopped its room: the other's words and messages to it waited behind it, the other game's
    own reading with them (a host that stopped reading kept its guest out of the room for good)"""
    try:
        await asyncio.wait_for(w.drain(), STALL_S)
        return True
    except asyncio.TimeoutError:
        log("stalled:", ROLE_OF.get(w, "?"), "read nothing for %d s: its connection closed" % STALL_S)
        w.close()
        return False


async def read_frame(r):
    head = await r.readexactly(5)
    n, kind = struct.unpack("<IB", head)
    data = await r.readexactly(n - 1) if n > 1 else b""
    return kind, data


async def close_with(w, code, why):
    """a WebSocket close as the relay's, with its code: the games' stand-in pipe reads "close <code>" """
    if w.is_closing():
        return
    log("close", code, why)
    await send(w, 2, ("close %d" % code).encode())
    try:
        w.write_eof()  # (this side's end first: the game reads the close before the socket goes)
    except Exception:
        pass
    await asyncio.sleep(0.3)
    w.close()


async def lifetimer(w, seconds):
    """--lifetime: this connection closed that long after it was let in (1009, the old 6-hour cut)"""
    await asyncio.sleep(seconds)
    await close_with(w, 1009, "lifetime")


def let_in(w):
    """a room connection let in: timed for --lifetime"""
    if LIFETIME is not None:
        asyncio.ensure_future(lifetimer(w, LIFETIME))


GO = None  # --go FILE: the timed kills, quiets, zombies, forgets and --seconds count from that file's appearance


async def gone_go():
    """--go: wait for the go file (a testpass online scenario's "relay_go": a game's log line reached), so the
    timed faults count from a moment of the games', never from wall-clock start (a loaded machine's slower join
    took a fixed 6 s kill before the guest was in)"""
    while GO is not None and not os.path.exists(GO):
        await asyncio.sleep(0.05)


async def killer(role, seconds, w):
    await gone_go()
    await asyncio.sleep(seconds)
    if not w.is_closing():
        log("kill", role)
        w.close()


async def quieter(role, seconds, fail, w):
    """--quiet: the connection silent both ways (not closed), then reset: what the deployed relay did"""
    await gone_go()
    await asyncio.sleep(seconds)
    if w.is_closing():
        return
    log("quiet", role)
    DEAF.add(w)
    await asyncio.sleep(fail)
    if not w.is_closing():
        log("reset", role, "(quiet %.1f s)" % fail)
        w.transport.abort()


async def zombie(role, seconds, room, w):
    """--zombie: the relay forgets the connection (its place free, the others told) but never closes it"""
    await gone_go()
    await asyncio.sleep(seconds)
    if w.is_closing() or room.conn.get(role) is not w:
        return
    log("zombie", role)
    DEAF.add(w)
    room.conn[role] = None
    if role.startswith("peer"):
        await send(room.conn["host"], 1, ("peergone %s" % role[4:]).encode())
    else:
        await send(room.conn["guest" if role == "host" else "host"], 1, b"gone")
        if role == "host":
            for n in (0, 1):
                await send(room.conn.get("peer%d" % n), 1, b"gone")


async def forgetter(role, seconds, hold, room):
    """--forget: the relay no longer knows that role's ticket (another one is its place's now), and the place
    stays taken HOLD seconds, as a real relay keeps a vanished game's connection: the game's tries in that
    while are turned away as a new game's would be"""
    await gone_go()
    await asyncio.sleep(seconds)
    room.ticket[role] = secrets.token_hex(8)
    HELD_PLACE[role] = time.time() + hold
    log("forget", role, "(its ticket no longer takes its place; the place held %.0f s)" % hold)


async def liar(role, seconds, w):
    """--short: a message of three bytes to that role, the start of a length and nothing after (a message cut
    or garbled on the way): read as a stream, every later message is out of step"""
    await asyncio.sleep(seconds)
    log("short: three bytes to", role)
    await send(w, 0, b"\x10\x00\x00", plain=True)


async def bigger(role, seconds, w):
    """--big: one message of the relay's biggest to that role (a frame of kind 0xEE, its length right, zeros)"""
    await asyncio.sleep(seconds)
    n = (1 << 20) - 64
    log("big: a message of %d bytes to" % n, role)
    msg = struct.pack("<IB", n - 4, 0xEE) + bytes(n - 5)
    frame = struct.pack("<IB", len(msg) + 1, 0) + msg
    if w.is_closing() or w in LINKS:
        await send(w, 0, msg, plain=True)
        return
    # (its first 1000 bytes a moment before the rest, as a slow link brings it: the game's reads of the rest then
    # end off its 64 KB steps, where a buffer of only 1 MB stopped short of the frame's end for good)
    lock = WLOCKS.setdefault(w, asyncio.Lock())
    async with lock:
        try:
            w.write(frame[:1000])
            await drained(w)
            await asyncio.sleep(0.3)
            w.write(frame[1000:])
            await drained(w)
        except (ConnectionError, RuntimeError):
            pass


def start_faults(role, w, room=None):
    """the role's --kill, --quiet, --halfopen, --zombie, --forget, --short and --big, from its first open"""
    for br, bs in BIGS:
        if br == role:
            asyncio.ensure_future(bigger(role, bs, w))
    for sr, ss in SHORTS:
        if sr == role:
            asyncio.ensure_future(liar(role, ss, w))
    for fr, fs, fh in FORGETS:
        if fr == role and room is not None:
            asyncio.ensure_future(forgetter(role, fs, fh, room))
    for kr, ks in KILLS:
        if kr == role:
            asyncio.ensure_future(killer(role, ks, w))
    for qr, qs, qf in QUIETS:
        if qr == role:
            asyncio.ensure_future(quieter(role, qs, qf, w))
    for zr, zs in ZOMBIES:
        if zr == role and room is not None:
            asyncio.ensure_future(zombie(role, zs, room, w))


async def forger(room, seconds):
    """--forge: a message as the guest's own that carries a frame of the relay's kind FROM_PEER for place 0
    (a guest passing for player 3, whose message holds a key agreement's first step), sent to the host as
    the guest's are: wrapped (FROM_GUEST) to a framed host, as it is to one that isn't."""
    await asyncio.sleep(seconds)
    inner = struct.pack("<IB", 33, 1) + bytes(range(32))  # (R_PAKE1 and 32 bytes)
    forged = struct.pack("<IBB", len(inner) + 2, 0xF1, 0) + inner
    host = room.conn["host"]
    if host is None:
        return
    log("forge: a frame for peer place 0 inside the guest's message", "(framed)" if room.framed else "(not framed)")
    if room.framed:
        await send(host, 0, struct.pack("<IB", len(forged) + 1, FROM_GUEST) + forged)
    else:
        await send(host, 0, forged)  # (as the guest's own message: a frame of kind 0xF1 at its top)


async def drop_cmd(room, half, text):
    """a host's "drop guest" / "drop peer <N>" [block]: as the relay's Room.drop"""
    m = DROP.match(text)
    if not m:
        return
    key = "guest" if m.group(1) == "guest" else "peer" + m.group(2)
    block = m.group(3) is not None
    w = room.conn.get(key)
    if w is None:
        # (left already, as a told removed player's game does at once: its last ticket refused all the same,
        # as the relay's Room.drop)
        if not block and room.ticket.get(key):
            room.refused.add(room.ticket[key])
        log(half, key, "drop asked: nobody there" + ("" if block else " (its last ticket refused)"))
        return
    room.conn[key] = None  # (out now: its own end tells no one again)
    if block:
        if room.mark.get(key):
            BLOCKED[room.mark[key]] = time.time() + BLOCK_SECONDS
    elif room.ticket.get(key):
        room.refused.add(room.ticket[key])
    log(half, key, "dropped by the host" + (" (block)" if block else ""))
    if not block:
        await send(w, 2, REMOVED)
    w.close()
    await send(room.conn["host"], 1, b"gone" if key == "guest" else ("peergone " + m.group(2)).encode())


def held_back(mark):
    """the stand-in's attackers only (a mark): blocked by a host, or over the relay's opens a minute"""
    if mark is None:
        return False
    now = time.time()
    if BLOCKED.get(mark, 0) > now:
        return True
    opens = [t for t in OPENS.get(mark, []) if now - t < 60] + [now]
    OPENS[mark] = opens
    return len(opens) > ROOM_OPENS_A_MINUTE


async def attacker_open(path):
    """one of the stand-in's attackers: a connection to this stand-in with the OPEN line"""
    r, w = await asyncio.open_connection("127.0.0.1", PORT)
    w.write(struct.pack("<IB", len(path) + 6, 1) + ("OPEN " + path).encode())
    await w.drain()
    return r, w


async def squatter(half, seconds):
    """--squat: the guest's place taken without the code: garbage the host can't read as frames, no key round;
    back each second after it's taken out, for 90 s"""
    await asyncio.sleep(seconds)
    until = time.time() + 90
    while time.time() < until:
        try:
            r, w = await attacker_open("/room/%s?role=guest&x=squat" % half)
        except OSError:
            return
        start = time.time()
        how = "closed"
        inside = False
        try:
            while True:
                if inside:
                    w.write(struct.pack("<IB", 65, 0) + b"\xff" * 64)  # (a frame's length past the message)
                    await w.drain()
                try:
                    kind, data = await asyncio.wait_for(read_frame(r), 0.5)
                except asyncio.TimeoutError:
                    continue
                if kind == 1 and data == b"open":
                    inside = True
                    log("squat: in the guest's place")
                elif kind == 2:
                    how = "refused: " + data.decode(errors="replace")
                    break
        except Exception:
            pass
        log("squat: out after %.1f s (%s)" % (time.time() - start, how))
        w.close()
        await asyncio.sleep(1.0)


async def flooder(half, seconds):
    """--flood: a peer's place taken, 400 messages a second sent until the stand-in closes it"""
    await asyncio.sleep(seconds)
    try:
        r, w = await attacker_open("/room/%s?role=peer&x=flood" % half)
    except OSError:
        return
    sent = 0
    start = time.time()
    ended = asyncio.ensure_future(r.read())  # (what the stand-in says, read until it closes)
    try:
        while time.time() - start < 30 and not ended.done():
            w.write((struct.pack("<IB", 17, 0) + b"\xff" * 16) * 40)
            sent += 40
            await w.drain()
            await asyncio.wait([ended], timeout=0.1)
    except Exception:
        pass
    log("flood: the attacker sent %d messages in %.1f s" % (sent, time.time() - start))
    w.close()


async def guesser(n):
    """--guess: N random room halves tried as the guest's, as fast as the relay lets one address"""
    chars = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"  # (PORT_ONLINE_CODE_CHARS)
    await asyncio.sleep(2.0)
    asked = found = held = 0
    for _ in range(n):
        half = "".join(random.choice(chars) for _ in range(4))
        try:
            r, w = await attacker_open("/room/%s?role=guest&x=guess" % half)
            kind, data = await asyncio.wait_for(read_frame(r), 2.0)
            if kind == 2 and data == HELD_BACK:
                held += 1
            elif kind == 2:
                asked += 1
            else:
                asked += 1
                found += 1
                log("guess: FOUND a hosted room", half)
            w.close()
        except Exception:
            held += 1
    days = 31 ** 4 / ROOM_OPENS_A_MINUTE / 60 / 24
    log("guess: %d tries: %d asked a room, %d found a host, %d held back; at %d a minute an address, every room "
        "half takes %.0f days" % (n, asked, found, held, ROOM_OPENS_A_MINUTE, days))


async def client(r, w):
    CONNS.add(w)
    try:
        await client_in(r, w)
    finally:
        CONNS.discard(w)
        unshape(w)


async def client_in(r, w):
    if NEVER_ANSWER:
        # --never-answer: the connection taken (the OS's accept: the game's connect returns) and never answered, its bytes
        # read and dropped, as a relay behind a dead uplink: the game's link stays "still connecting" for good
        log("never-answer: a connection taken, never answered")
        try:
            while await r.read(4096):
                pass
        except Exception:
            pass
        return
    try:
        kind, data = await read_frame(r)
    except Exception:
        w.close()
        return
    COUNTS["open"] += 1
    line = data.decode(errors="replace")
    if kind != 1 or not line.startswith("OPEN "):
        w.close()
        return
    u = urlparse(line[5:])
    q = parse_qs(u.query)
    if u.path == "/rooms":
        # the relay's list (index.js's Lobby): every room's count, the listed ones with a place
        game, v = q.get("game", [""])[0], q.get("v", [""])[0]
        online = sum(rm.playing() for rm in ROOMS.values())
        lines = ["rooms %d" % online]
        for rm in ROOMS.values():
            if (rm.list and rm.conn["host"] is not None and rm.list[2] == game and rm.list[3] == v
                    and rm.playing() < rm.list[4]):
                lines.append("%s %d/%d %s" % (rm.list[0], rm.playing(), rm.list[4], quote(rm.list[1], safe="")))
        log("list asked", game, v, "->", len(lines) - 1, "rooms")
        text = "".join(x + chr(10) for x in lines)
        await send(w, 3, text.encode())
        try:
            await read_frame(r)  # (until the client closes)
        except Exception:
            pass
        w.close()
        return
    if u.path in ("/health", "/health.json"):
        # the relay's health report (index.js's Lobby.health), its shape only: who's here now; the stand-in
        # keeps no day's counts
        online = sum(rm.playing() for rm in ROOMS.values())
        n = sum(1 for rm in ROOMS.values() if rm.playing() > 0)
        day = time.strftime("%Y-%m-%d", time.gmtime())
        if u.path == "/health.json":
            text = json.dumps({"relay": "Paper Cut relay", "now": {"players": online, "rooms": n},
                               "days": [{"day": day, "counts": {}, "peak": {"players": online, "rooms": n}}]}) + chr(10)
        else:
            text = ("Paper Cut relay: health, per UTC day (counts only: nothing personal)" + chr(10) +
                    "now: %d players in %d rooms" % (online, n) + chr(10) + chr(10) +
                    day + " (today, so far)" + chr(10) + "  nothing (the stand-in relay counts nothing)" + chr(10))
        log("health asked")
        await send(w, 3, text.encode())
        try:
            await read_frame(r)  # (until the client closes)
        except Exception:
            pass
        w.close()
        return
    half = u.path.split("/")[-1]
    role = q.get("role", [""])[0]
    caps = role == "host" and q.get("caps", [""])[0] == "1" and not NO_CAPS
    if caps and BUDGET is not None and q.get("ticket", [None])[0] is None and day_used() >= BUDGET * 0.95:
        log(half, role, "refused: the day's count %.0f of %d" % (day_used(), BUDGET))
        await send(w, 2, b"The relay is full for today: it opens again at midnight UTC. Connecting by address still works.")
        w.close()
        return
    CONNECTS[0] += 1
    if FULL_TODAY:
        log(half, role, "refused: full for today")
        await send(w, 2, b"The relay is full for today: it opens again at midnight UTC. Connecting by address still works.")
        w.close()
        return
    ticket = q.get("ticket", [None])[0]
    mark = q.get("x", [None])[0]  # (the stand-in's own attackers)
    room = ROOMS.setdefault(half, Room())
    if role in ("guest", "peer") and ticket is not None and ticket in room.refused:
        log(half, role, "refused: its ticket was taken out by the host")
        await send(w, 2, REMOVED)
        w.close()
        return
    if held_back(mark):
        log(half, role, "refused: held back (%s)" % mark)
        await send(w, 2, HELD_BACK)
        w.close()
        return
    if role not in ("host", "guest", "peer") or (role == "peer" and NO_PEER):
        await send(w, 2, b"role: host, guest, seat or watch" if NO_PEER else b"role: host, guest or peer")
        w.close()
        return
    if role == "host" and "list" in q and room.conn["host"] is None:
        mx = min(max(int(q.get("max", ["2"])[0] or 2), 2), 4)
        room.list = (half + q["list"][0], q.get("name", [""])[0][:24], q.get("game", [""])[0],
                     q.get("v", [""])[0], mx)
        log(half, "listed", room.list[1], room.list[3], "max", mx)
    if role == "peer":
        # players 3 and 4 ("sync what matters"): a place each, kept by ticket (as the relay's peers)
        await peer_client(r, w, half, room, ticket, mark)
        return
    other = "guest" if role == "host" else "host"
    reconnect = ticket is not None and ticket == room.ticket.get(role)
    if not reconnect:
        if role == "host" and room.conn["host"] is not None:
            await send(w, 2, b"That room already has a host")
            w.close()
            return
        if role == "guest" and room.conn["host"] is None:
            await send(w, 2, b"No one is hosting that room")
            w.close()
            return
        if role == "guest" and (room.conn["guest"] is not None or HELD_PLACE.get(role, 0) > time.time()):
            # (--forget: the place's old connection as a real relay keeps a vanished one's a while)
            await send(w, 2, b"That room is full")
            w.close()
            return
        if role == "host" and HELD_PLACE.get(role, 0) > time.time():
            await send(w, 2, b"That room already has a host")
            w.close()
            return
    if role == "host":
        # (the guest's messages wrapped too: FROM_GUEST; --no-framed: as the relay before it, never)
        room.framed = q.get("framed", [""])[0] == "1" and not NO_FRAMED
        room.split = caps
    old = room.conn[role]
    room.conn[role] = w
    ROLE_OF[w] = role
    room.mark[role] = mark
    shape(w, half, role)
    if old is not None:
        log(half, role, "replaced (ticket)")
        old.close()
    if not reconnect:
        room.ticket[role] = secrets.token_hex(8)
    log(half, role, "in" + (" again" if reconnect else ""))
    let_in(w)
    await send(w, 1, b"open")
    await send(w, 1, b"you 127.0.0.1:1")
    await send(w, 1, ("ticket " + room.ticket[role]).encode())
    if role == "host" and room.framed:
        await send(w, 1, b"framed")
    if role == "host" and room.split:
        await send(w, 1, b"caps split")
        if BUDGET is not None and day_used() >= BUDGET * 0.8:
            await send(w, 1, b"busy")
    if room.conn[other] is not None:
        await send(w, 1, b"peer")
        await send(room.conn[other], 1, b"peer")
    if role == "host":
        for n in (0, 1):
            p = room.conn.get("peer%d" % n)
            if p is not None:
                await send(p, 1, b"peer")
                await send(w, 1, ("peer %d" % n).encode())
    key = role + half
    if key not in FIRST:
        FIRST[key] = True
        start_faults(role, w, room)
        if role == "guest" and FORGE is not None:
            asyncio.ensure_future(forger(room, FORGE))
        if role == "host" and SQUAT is not None:
            asyncio.ensure_future(squatter(half, SQUAT))
        if role == "host" and FLOOD is not None:
            asyncio.ensure_future(flooder(half, FLOOD))
        if role == "host" and GUESS > 0:
            asyncio.ensure_future(guesser(GUESS))
    try:
        while True:
            kind, data = await read_frame(r)
            COUNTS["%s %s" % (role, "bin" if kind == 0 else "text")] += 1
            if w in DEAF:
                continue  # (gone quiet: what it sends goes nowhere)
            room.count(role)
            if kind == 1 and role == "host" and room.conn["host"] is w:
                await drop_cmd(room, half, data.decode(errors="replace"))
                continue
            if kind == 0 and role == "guest" and (room.framed and len(data) > MAX_FRAMED):
                log(half, role, "a message too long:", len(data))
                break  # (closed, as the relay's 1009)
            if kind == 0 and role == "guest" and room.framed:
                await send(room.conn[other], 0, struct.pack("<IB", len(data) + 1, FROM_GUEST) + data)
            elif kind == 0 and role == "host" and room.split and data[:4] == SPLIT:
                parts = split_parts(data)
                if parts is None:
                    log(half, role, "a bad split message: closed")
                    break  # (as index.js's 1007)
                for place, part in parts:
                    await send(room.conn["guest"] if place == 0xFF else room.conn.get("peer%d" % place), 0, part)
            elif kind == 0:
                if role == "host" and len(data) >= 4 and data[:3] == b"\xfe\xfe\xfe" and data[3] in (0, 1):
                    await send(room.conn.get("peer%d" % data[3]), 0, data[4:])
                else:
                    await send(room.conn[other], 0, data)
    except Exception as e:
        if not isinstance(e, asyncio.IncompleteReadError):
            log(half, role, "ended:", type(e).__name__, e)
    if room.conn[role] is w:
        room.conn[role] = None
        log(half, role, "gone")
        await send(room.conn[other], 1, b"gone")
        if role == "host":
            for n in (0, 1):
                await send(room.conn.get("peer%d" % n), 1, b"gone")
    w.close()


async def peer_client(r, w, half, room, ticket, mark=None):
    slot = None
    for n in (0, 1):
        if ticket is not None and room.ticket.get("peer%d" % n) == ticket:
            slot = n  # (its place again)
    if slot is None:
        if room.conn["host"] is None:
            await send(w, 2, b"No one is hosting that room")
            w.close()
            return
        free = [n for n in (0, 1) if room.conn.get("peer%d" % n) is None]
        if not free:
            await send(w, 2, b"That room has as many players as it can")
            w.close()
            return
        slot = free[0]
        room.ticket["peer%d" % slot] = secrets.token_hex(8)
    key = "peer%d" % slot
    old = room.conn.get(key)
    room.conn[key] = w
    ROLE_OF[w] = key
    room.mark[key] = mark
    shape(w, half, key)
    if old is not None:
        old.close()
    log(half, key, "in" + (" (%s)" % mark if mark else ""))
    let_in(w)
    if key + half not in FIRST:
        FIRST[key + half] = True
        start_faults(key, w, room)
    await send(w, 1, b"open")
    await send(w, 1, b"you 127.0.0.1:1")
    await send(w, 1, ("ticket " + room.ticket[key]).encode())
    if room.conn["host"] is not None:
        await send(w, 1, b"peer")
        await send(room.conn["host"], 1, ("peer %d" % slot).encode())
    rate = {"second": int(time.time()), "n": 0, "streak": 0, "last": -2}
    try:
        while True:
            kind, data = await read_frame(r)
            COUNTS["%s %s" % (key, "bin" if kind == 0 else "text")] += 1
            if w in DEAF:
                continue  # (gone quiet: what it sends goes nowhere)
            room.count(key)
            if mark is not None:
                # (the relay's FLOOD_MESSAGES cap, kept to the stand-in's attackers: a test game, sped up,
                # isn't held to real time)
                s = int(time.time())
                if s != rate["second"]:
                    over = rate["n"] > FLOOD_MESSAGES
                    rate["streak"] = (rate["streak"] + 1 if rate["last"] == rate["second"] - 1 else 1) if over else 0
                    rate["last"] = rate["second"] if over else rate["last"]
                    rate.update(second=s, n=0)
                rate["n"] += 1
                if rate["streak"] >= FLOOD_SECONDS:
                    log(half, key, "closed: too fast (%s)" % mark)
                    break
            if kind == 0 and len(data) > MAX_FRAMED:
                log(half, key, "a message too long:", len(data))
                break  # (closed, as the relay's 1009)
            if kind == 0:
                await send(room.conn["host"], 0, struct.pack("<IBB", len(data) + 2, 0xF1, slot) + data)
    except Exception as e:
        if not isinstance(e, asyncio.IncompleteReadError):
            log(half, key, "ended:", type(e).__name__, e)
    if room.conn.get(key) is w:
        room.conn[key] = None
        log(half, key, "gone")
        await send(room.conn["host"], 1, ("peergone %d" % slot).encode())
    w.close()


async def restarter(at, down, port):
    """--restart: AT seconds after the first host's open, everything cut and forgotten, DOWN seconds closed"""
    global SERVER
    while not any(k[1] == "host" for k in ROLE_START) and not any(k.startswith("host") for k in FIRST):
        await asyncio.sleep(0.1)
    await asyncio.sleep(at)
    log("restart: every connection cut, rooms forgotten, closed for %.1f s" % down)
    SERVER.close()
    cut = list(CONNS)
    for w in cut:
        # (a deploy's WebSocket close first, then this side's end: the game reads the close before the cut, which
        # a reset at once could lose; the games take it as "come back now")
        try:
            await send(w, 2, b"close 1012")
            w.write_eof()
        except Exception:
            pass
    await asyncio.sleep(0.3)
    for w in cut:
        try:
            w.transport.abort()
        except Exception:
            pass
    ROOMS.clear()
    DEAF.clear()
    await asyncio.sleep(down)
    SERVER = await asyncio.start_server(client, "127.0.0.1", port)
    log("restart: listening again", port)


async def counter():
    """the frames into the relay so far, every 5 seconds while they change"""
    last = None
    while True:
        await asyncio.sleep(5)
        now = dict(COUNTS)
        if now != last:
            log("counts", " ".join("%s=%d" % kv for kv in sorted(now.items())))
            last = now


async def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--kill", nargs="*", default=[])
    ap.add_argument("--quiet", nargs="*", default=[])
    ap.add_argument("--log", default=None)
    ap.add_argument("--seconds", type=int, default=900)
    ap.add_argument("--no-peer", action="store_true")
    ap.add_argument("--no-framed", action="store_true")
    ap.add_argument("--forge", type=float, default=None)
    ap.add_argument("--full-today", action="store_true")
    ap.add_argument("--never-answer", action="store_true")
    ap.add_argument("--hostile", default=None)
    ap.add_argument("--squat", type=float, default=None)
    ap.add_argument("--flood", type=float, default=None)
    ap.add_argument("--guess", type=int, default=0)
    ap.add_argument("--no-caps", action="store_true")
    ap.add_argument("--budget", type=int, default=None)
    ap.add_argument("--net", default="")
    ap.add_argument("--net-role", nargs="*", default=[])
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--restart", default=None)
    ap.add_argument("--halfopen", nargs="*", default=[])
    ap.add_argument("--zombie", nargs="*", default=[])
    ap.add_argument("--forget", nargs="*", default=[])
    ap.add_argument("--drop-word", nargs="*", default=[])
    ap.add_argument("--short", nargs="*", default=[])
    ap.add_argument("--big", nargs="*", default=[])
    ap.add_argument("--lifetime", type=float, default=None)
    ap.add_argument("--go", default=None)
    a = ap.parse_args()
    global NET, SEED, SERVER
    NET, SEED = a.net, a.seed
    netshape.parse(NET)  # (a typo fails at once, never a run with no faults)
    for k in a.net_role:
        role, spec = k.split("=", 1)
        netshape.parse(spec)
        NET_ROLE[role] = spec
    for k in a.halfopen:
        role, s = k.split(":")
        QUIETS.append((role, float(s), 1e9))
    for k in a.zombie:
        role, s = k.split(":")
        ZOMBIES.append((role, float(s)))
    for k in a.forget:
        parts = k.split(":")
        FORGETS.append((parts[0], float(parts[1]), float(parts[2]) if len(parts) > 2 else 15.0))
    for k in a.short:
        role, s = k.split(":")
        SHORTS.append((role, float(s)))
    for k in a.big:
        role, s = k.split(":")
        BIGS.append((role, float(s)))
    for k in a.drop_word:
        word, _, n = k.rpartition(":")
        DROP_WORDS[word.encode()] = [int(n), 0]
    global NO_PEER, NO_FRAMED, FORGE, FULL_TODAY, NO_CAPS, BUDGET, HOSTILE
    if a.hostile:
        parts = a.hostile.split(":")
        HOSTILE = (random.Random(int(parts[0])), float(parts[1]) if len(parts) > 1 else 5.0,
                   set(parts[2].split(",")) if len(parts) > 2 and parts[2] else None,
                   time.time() + float(parts[3]) if len(parts) > 3 and parts[3] else None,
                   time.time() + (float(parts[4]) if len(parts) > 4 and parts[4] else 0.0))
    global NO_PEER, NO_FRAMED, FORGE, FULL_TODAY, NO_CAPS, BUDGET, LIFETIME
    global SQUAT, FLOOD, GUESS, PORT, GO
    GO = a.go
    LIFETIME = a.lifetime
    SQUAT, FLOOD, GUESS, PORT = a.squat, a.flood, a.guess, a.port
    FULL_TODAY = a.full_today
    global NEVER_ANSWER
    NEVER_ANSWER = a.never_answer
    NO_CAPS = a.no_caps
    BUDGET = a.budget
    NO_PEER = a.no_peer
    NO_FRAMED = a.no_framed
    FORGE = a.forge
    if a.log:
        LOG = open(a.log, "w", encoding="utf-8")
    for k in a.kill:
        role, s = k.split(":")
        KILLS.append((role, float(s)))
    for k in a.quiet:
        parts = k.split(":")
        QUIETS.append((parts[0], float(parts[1]), float(parts[2]) if len(parts) > 2 else 10.0))
    SERVER = await asyncio.start_server(client, "127.0.0.1", a.port)
    log("listening", a.port, "net", NET or "-", " ".join("%s=%s" % kv for kv in NET_ROLE.items()))
    asyncio.ensure_future(counter())
    asyncio.ensure_future(rate_logger())
    if a.restart:
        at, _, down = a.restart.partition(":")
        asyncio.ensure_future(restarter(float(at), float(down or 3), a.port))
    await gone_go()
    if GO is not None:
        log("go", GO)
    await asyncio.sleep(a.seconds)
    log("counts", " ".join("%s=%d" % kv for kv in sorted(COUNTS.items())))
    SERVER.close()


if __name__ == "__main__":
    asyncio.run(main())

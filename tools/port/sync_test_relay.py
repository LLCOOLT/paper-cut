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
"OPEN /rooms?game=<ID>&v=<VERSION>": one frame of kind 3, the relay's room list text
("rooms <online>" then "<CODE> <PLAYING>/<MAX> <NAME>" for each listed room with a free place).

test_relay.py PORT [--kill ROLE:SECONDS ...] [--quiet ROLE:SECONDS[:FAIL] ...] [--log FILE] [--no-peer] [--no-framed]
              [--forge SECONDS] [--full-today]
--kill: that role's connection closed after that many seconds from its first open (a drop).
--quiet: that role's connection goes quiet that many seconds from its first open, as the deployed relay's
did mid-session: never closed, nothing more passed to it or from it (the others' messages to it lost), and
FAIL seconds later (default 10) reset, so the game sees no close, then a read failure, unless it gave up on
the quiet first. ROLE: host, guest, peer0 or peer1 (players 3 and 4's places); a ticket takes the place again.
--no-peer: role=peer refused as the relay before peers does ("role: ...", 400).
--no-framed: peers, but a host's &framed=1 not answered, as the relay with peers from before the framing.
--full-today: every room's open refused as the relay is when Cloudflare's daily requests are used up (a 429:
"The relay is full for today ..."), to see the games stop asking.
--forge SECONDS: that long after the guest is first in, a message as the guest's carrying a FROM_PEER
frame for place 0 (a key agreement's first step) goes to the host (the host must leave it out).
"""
import argparse
import asyncio
import secrets
import struct
import sys
import time
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

    def playing(self):
        return sum(1 for k, c in self.conn.items() if c is not None)


NO_PEER = False
NO_FRAMED = False  # --no-framed: &framed=1 not answered (the relay with peers from before the framing)
FORGE = None  # --forge SECONDS: that long after the guest's first open, a forged frame as its own (forger)
FULL_TODAY = False  # --full-today: every room's open refused as the relay's 429 (its day used up)
FROM_GUEST = 0xF2
MAX_FRAMED = (1 << 20) - 64  # a guest's (framed) or a peer's message at most, as the relay's MAX_FRAMED


ROOMS = {}
KILLS = []
QUIETS = []  # --quiet: (role, seconds, fail seconds)
DEAF = set()  # the connections gone quiet (quieter): nothing sent to them, what they send dropped
FIRST = {}


async def send(w, kind, data):
    if w is None or w.is_closing() or w in DEAF:
        return
    w.write(struct.pack("<IB", len(data) + 1, kind) + data)
    try:
        await w.drain()
    except ConnectionError:
        pass


async def read_frame(r):
    head = await r.readexactly(5)
    n, kind = struct.unpack("<IB", head)
    data = await r.readexactly(n - 1) if n > 1 else b""
    return kind, data


async def killer(role, seconds, w):
    await asyncio.sleep(seconds)
    if not w.is_closing():
        log("kill", role)
        w.close()


async def quieter(role, seconds, fail, w):
    """--quiet: the connection silent both ways (not closed), then reset: what the deployed relay did"""
    await asyncio.sleep(seconds)
    if w.is_closing():
        return
    log("quiet", role)
    DEAF.add(w)
    await asyncio.sleep(fail)
    if not w.is_closing():
        log("reset", role, "(quiet %.1f s)" % fail)
        w.transport.abort()


def start_faults(role, w):
    """the role's --kill and --quiet, from its first open"""
    for kr, ks in KILLS:
        if kr == role:
            asyncio.ensure_future(killer(role, ks, w))
    for qr, qs, qf in QUIETS:
        if qr == role:
            asyncio.ensure_future(quieter(role, qs, qf, w))


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


async def client(r, w):
    try:
        kind, data = await read_frame(r)
    except Exception:
        w.close()
        return
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
    half = u.path.split("/")[-1]
    role = q.get("role", [""])[0]
    if FULL_TODAY:
        log(half, role, "refused: full for today")
        await send(w, 2, b"The relay is full for today: it opens again at midnight UTC. Connecting by address still works.")
        w.close()
        return
    ticket = q.get("ticket", [None])[0]
    room = ROOMS.setdefault(half, Room())
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
        await peer_client(r, w, half, room, ticket)
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
        if role == "guest" and room.conn["guest"] is not None:
            await send(w, 2, b"That room is full")
            w.close()
            return
    if role == "host":
        # (the guest's messages wrapped too: FROM_GUEST; --no-framed: as the relay before it, never)
        room.framed = q.get("framed", [""])[0] == "1" and not NO_FRAMED
    old = room.conn[role]
    room.conn[role] = w
    if old is not None:
        log(half, role, "replaced (ticket)")
        old.close()
    if not reconnect:
        room.ticket[role] = secrets.token_hex(8)
    log(half, role, "in" + (" again" if reconnect else ""))
    await send(w, 1, b"open")
    await send(w, 1, b"you 127.0.0.1:1")
    await send(w, 1, ("ticket " + room.ticket[role]).encode())
    if role == "host" and room.framed:
        await send(w, 1, b"framed")
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
        start_faults(role, w)
        if role == "guest" and FORGE is not None:
            asyncio.ensure_future(forger(room, FORGE))
    try:
        while True:
            kind, data = await read_frame(r)
            if w in DEAF:
                continue  # (gone quiet: what it sends goes nowhere)
            if kind == 0 and role == "guest" and (room.framed and len(data) > MAX_FRAMED):
                log(half, role, "a message too long:", len(data))
                break  # (closed, as the relay's 1009)
            if kind == 0 and role == "guest" and room.framed:
                await send(room.conn[other], 0, struct.pack("<IB", len(data) + 1, FROM_GUEST) + data)
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


async def peer_client(r, w, half, room, ticket):
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
    if old is not None:
        old.close()
    log(half, key, "in")
    if key + half not in FIRST:
        FIRST[key + half] = True
        start_faults(key, w)
    await send(w, 1, b"open")
    await send(w, 1, b"you 127.0.0.1:1")
    await send(w, 1, ("ticket " + room.ticket[key]).encode())
    if room.conn["host"] is not None:
        await send(w, 1, b"peer")
        await send(room.conn["host"], 1, ("peer %d" % slot).encode())
    try:
        while True:
            kind, data = await read_frame(r)
            if w in DEAF:
                continue  # (gone quiet: what it sends goes nowhere)
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
    a = ap.parse_args()
    global NO_PEER, NO_FRAMED, FORGE, FULL_TODAY
    FULL_TODAY = a.full_today
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
    server = await asyncio.start_server(client, "127.0.0.1", a.port)
    log("listening", a.port)
    async with server:
        await asyncio.sleep(a.seconds)


if __name__ == "__main__":
    asyncio.run(main())
